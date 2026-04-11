import argparse
import cv2
import threading
import time
import math
import sys
import random
import asyncio
from ultralytics import YOLO
import numpy as np
from pathlib import Path
import json
import contextlib

try:
    import websockets
    WEBSOCKETS_AVAILABLE = True
except ImportError:
    print("WebSockets library not available. Run: pip install websockets")
    WEBSOCKETS_AVAILABLE = False

try:
    import termios
    import tty
except ImportError:
    print("Windows boooo")

parser=argparse.ArgumentParser()
parser.add_argument("--demo", action="store_true", help="Run in simulation mode with dummy GPIO")
parser.add_argument("--no_view", action="store_true", help="Run headlessly without showing the CV2 window")
parser.add_argument("--calibrate", action="store_true", help="Run max step calibration")
parser.add_argument("--esp32_ip", type=str, default="192.168.4.1", help="ESP32 IP address for WebSocket connection")
parser.add_argument("--esp32_port", type=int, default=80, help="ESP32 WebSocket port")
parser.add_argument("--no_esp32", action="store_true", help="Disable ESP32 car control")
args=parser.parse_args()

demo_mode = args.demo
show_frame = not args.no_view 
calibration_mode = args.calibrate
esp32_ip = args.esp32_ip
esp32_port = args.esp32_port
enable_esp32 = not args.no_esp32 and WEBSOCKETS_AVAILABLE

print(f"[Config] Demo Mode: {demo_mode}, Show Video: {show_frame}, Manual Calibration: {calibration_mode}, ESP32: {enable_esp32} ({esp32_ip}:{esp32_port})")

if not demo_mode:
    import RPi.GPIO as GPIO
    import termios
else:
    class DummyGPIO:
        BCM = "BCM"
        OUT = "OUT"

        def setmode(self, *a, **k): pass
        def setwarnings(self, *a, **k): pass
        def setup(self, *a, **k): pass
        def output(self, *a, **k): pass
        def add_event_detect(self, *a, **k): pass
        def cleanup(self, *a, **k): pass

        def IN(self, *a, **k): pass
        def PUD_UP(self, *a, **k): pass
        def BOTH(self, *a, **k): pass


        class PWM:
            def __init__(self, pin, freq): pass
            def start(self, dc): pass
            def ChangeDutyCycle(self, dc): pass
            def stop(self): pass

    GPIO = DummyGPIO()


# ==========================================
# CONFIGURATION
# ==========================================
MODEL_PATH = "yolo11n.onnx" # Use your converted NCNN model folder
CENTER_TOLERANCE = 30  
LEAD_FACTOR = 5
STEP_DELAY = 0.0015
SERVO_PIN = 12 # PIN
HALL_EFFECT_X_PIN = 16
HALL_EFFECT_Y_PIN = 26

CONFIG_FILE = Path("config.json")
if CONFIG_FILE.exists():
    with open(CONFIG_FILE, "r") as f:
        data = json.load(f)
        try:
            MAX_STEPS_X = int(data.get("max_steps_x"))
            MAX_STEPS_Y = int(data.get("max_steps_y")) # excact names needed in the config.json
            PISTON_RETRACT_STEPS = int(data.get("piston_retract_steps", 512))
            if not MAX_STEPS_X or not MAX_STEPS_X:
                assert ValueError("Your config file is missing values")
        except Exception as e:
            print(f"Something is wrong with your config file. Fix that. Error: ", e)
            sys.exit(1)

# --- GPIO Pins ---
X_PINS = [23, 24, 25, 8]
Y_PINS = [17, 27, 22, 10]
PISTON_PINS = [5, 6, 7, 20]  # New pins for piston stepper motor

# ==========================================
# 1. HARDWARE INTERFACE
# ==========================================
GPIO.setmode(GPIO.BCM)
GPIO.setwarnings(False)

class StepperMotor:
    def __init__(self, pins):
        self.pins = pins
        self.step_index = 0
        self.seq = [[1,0,0,1], [1,0,0,0], [1,1,0,0], [0,1,0,0], 
                    [0,1,1,0], [0,0,1,0], [0,0,1,1], [0,0,0,1]]
        for pin in self.pins:
            GPIO.setup(pin, GPIO.OUT)
            
    def step(self, direction):
        self.step_index = (self.step_index + direction) % 8
        step_val = self.seq[self.step_index]
        for i in range(4):
            GPIO.output(self.pins[i], step_val[i])

    def stop(self):
        for pin in self.pins:
            GPIO.output(pin, 0)

motor_x = StepperMotor(X_PINS)
motor_y = StepperMotor(Y_PINS)
motor_piston = StepperMotor(PISTON_PINS)  # Piston stepper motor

# Servo

GPIO.setup(SERVO_PIN, GPIO.OUT)
servo_pwm = GPIO.PWM(SERVO_PIN, 50)
servo_pwm.start(2.5)

# ==========================================
# 2. SHARED STATE
# ==========================================
class TurretState:
    def __init__(self):
        self.lock = threading.Lock()
        self.target_dx = 0
        self.target_dy = 0
        self.is_tracking = False
        self.is_firing = False
        self.at_sensor_dx = False
        self.at_sensor_dy = False
        self.position_x = 0
        self.position_y = 0
        self.running = False
        self.calibrating = True

        self.manual_active = False
        self.last_input_time = 0
        self.manual_vx = 0
        self.manual_vy = 0

        # Piston state
        self.piston_retracting = False
        self.piston_trigger = False  # Signal to trigger piston retraction

        # ESP32 Car state
        self.esp32_dx = 0
        self.esp32_dy = 0
        self.esp32_found = False

state = TurretState()

# Hall effect sensor https://www.raspberrypi-spy.co.uk/2015/09/how-to-use-a-hall-effect-sensor-with-the-raspberry-pi/

def sensorCallback(channel):
    is_triggered = not GPIO.input(channel)
    with state.lock:
        calibrating = state.calibrating
        at_x = state.at_sensor_dx
        at_y = state.at_sensor_dy

    if channel == HALL_EFFECT_X_PIN:
        if not (calibrating and not is_triggered and at_x):
            with state.lock: state.at_sensor_dx = is_triggered
        print(f"X Sensor: {is_triggered}")
    elif channel == HALL_EFFECT_Y_PIN:
        if not (calibrating and not is_triggered and at_y):
            with state.lock: state.at_sensor_dy = is_triggered
        print(f"Y Sensor: {is_triggered}")

GPIO.setup(HALL_EFFECT_X_PIN , GPIO.IN, pull_up_down=GPIO.PUD_UP)
GPIO.add_event_detect(HALL_EFFECT_X_PIN, GPIO.BOTH, callback=sensorCallback, bouncetime=50)
GPIO.setup(HALL_EFFECT_Y_PIN , GPIO.IN, pull_up_down=GPIO.PUD_UP)
GPIO.add_event_detect(HALL_EFFECT_Y_PIN, GPIO.BOTH, callback=sensorCallback, bouncetime=50)

# Calibration


def calibrate_motor():
    print("Starting calibration")
    while state.calibrating:
        with state.lock:
            at_sensor_dx, at_sensor_dy = state.at_sensor_dx, state.at_sensor_dy
        
        if demo_mode:
            if random.randrange(500) == 499:
                with state.lock:
                    state.at_sensor_dx = True
                    print("Demo at sensor x")
                    continue
            elif random.randrange(500) == 499:
                with state.lock:
                    state.at_sensor_dy = True
                    print("Demo at sensor y")
                    continue

        if at_sensor_dx and at_sensor_dy:
            motor_x.stop()
            motor_y.stop()
            with state.lock:
                state.running = True
                state.calibrating = False
                state.position_x = 0
                state.position_y = 0
            print("Calibration finished")
            break
        
        if not at_sensor_dx: motor_x.step(-1)
        if not at_sensor_dy: motor_y.step(-1)
        
        time.sleep(STEP_DELAY)

# Credit to https://github.com/HackerShackOfficial/Tracking-Turret
@contextlib.contextmanager
def raw_mode(file):
    """
    Magic function that allows key presses.
    :param file:
    :return:
    """
    old_attrs = termios.tcgetattr(file.fileno())
    new_attrs = old_attrs[:]
    new_attrs[3] = new_attrs[3] & ~(termios.ECHO | termios.ICANON)
    try:
        termios.tcsetattr(file.fileno(), termios.TCSADRAIN, new_attrs)
        yield
    finally:
        termios.tcsetattr(file.fileno(), termios.TCSADRAIN, old_attrs)

def keyboard_listener():
    """ Runs in a background thread to capture keypresses without blocking motors """
    print("Input listener started.")
    with raw_mode(sys.stdin):
        while state.manual_active:
            ch = sys.stdin.read(1) 
            
            with state.lock:
                state.last_input_time = time.time()
                
                if ch == 'w': state.manual_vy = 1
                elif ch == 's': state.manual_vy = -1
                elif ch == 'a': state.manual_vx = 1
                elif ch == 'd': state.manual_vx = -1
                elif ch == '\n' or ch == 'q': 
                    state.manual_active = False # Stop loop
                    return

def calibrate_steps():
    print("========================================")
    print("Starting step calibration")
    print("========================================\n")
    print("Moving to origin")
    calibrate_motor()
    print("Input 'w' and 's' for up/down\nInput 'a' and 'd' for left/right\nInput Enter to save and finish")
    
    state.manual_active = True
    kb_thread = threading.Thread(target=keyboard_listener, daemon=True)
    kb_thread.start()

    try:
        while state.manual_active:

            with state.lock:
                if time.time() - state.last_input_time > 0.15:
                    state.manual_vx = 0
                    state.manual_vy = 0
                
                vx = state.manual_vx
                vy = state.manual_vy

            if vx != 0:
                motor_x.step(vx)
                with state.lock: state.position_x += vx
            else:
                motor_x.stop()

            if vy != 0:
                motor_y.step(vy)
                with state.lock: state.position_y += vy
            else:
                motor_y.stop()
                
                with state.lock:
                    px, py = state.position_x, state.position_y
            
                if px % 50 == 0 and py % 50 == 0:
                    # \r overwrites the line
                    sys.stdout.write(f"\rCurrent Steps -> X: {px}  Y: {py}   ")
                    sys.stdout.flush()
                
                time.sleep(STEP_DELAY)

    except KeyboardInterrupt:
        pass

    motor_x.stop()
    motor_y.stop()
    
    with state.lock:
        final_x = state.position_x
        final_y = state.position_y

    print(f"\n\nSaving Configuration:")
    print(f"MAX_STEPS_X: {final_x}")
    print(f"MAX_STEPS_Y: {final_y}")
    
    config_data = {"max_steps_x": final_x, "max_steps_y": final_y}
    
    try:
        with open(CONFIG_FILE, "w") as f:
            json.dump(config_data, f)
        print("Config.json updated successfully.")
    except Exception as e:
        print(f"Failed to save config: {e}")

# ==========================================
# 3. MOTOR THREAD
# ==========================================

class ESP32Client:
    """WebSocket client for ESP32 car control"""
    
    def __init__(self, ip_address, port):
        self.ip_address = ip_address
        self.port = port
        self.websocket = None
        self.connected = False
        self.running = False
    
    async def connect(self):
        """Connect to ESP32 WebSocket server"""
        try:
            self.websocket = await websockets.connect(f"ws://{self.ip_address}:{self.port}/ws", ping_interval=None)
            self.connected = True
            print(f"[ESP32] Connected to {self.ip_address}:{self.port}")
            return True
        except Exception as e:
            print(f"[ESP32] Connection failed: {e}")
            self.connected = False
            return False
    
    async def disconnect(self):
        """Disconnect from ESP32"""
        if self.websocket:
            await self.websocket.close()
        self.connected = False
    
    async def send_track_info(self, dx, dy, found):
        """Send tracking information to ESP32"""
        if not self.connected or not self.websocket:
            return
        
        try:
            message = {
                "type": "track",
                "dx": dx,
                "dy": dy,
                "found": found,
                "timestamp": int(time.time() * 1000)
            }
            await self.websocket.send(json.dumps(message))
        except Exception as e:
            print(f"[ESP32] Send error: {e}")
            self.connected = False
    
    async def send_manual(self, left, right):
        """Send manual motor control to ESP32"""
        if not self.connected or not self.websocket:
            return
        
        try:
            message = {
                "type": "manual",
                "left": left,
                "right": right,
                "timestamp": int(time.time() * 1000)
            }
            await self.websocket.send(json.dumps(message))
        except Exception as e:
            print(f"[ESP32] Send error: {e}")
            self.connected = False
    
    async def get_status(self):
        """Request status from ESP32"""
        if not self.connected or not self.websocket:
            return None
        
        try:
            message = {"type": "status_req", "timestamp": int(time.time() * 1000)}
            await self.websocket.send(json.dumps(message))
            response = await asyncio.wait_for(self.websocket.recv(), timeout=1.0)
            return json.loads(response)
        except Exception as e:
            return None


# Global ESP32 client instance
esp32_client = None


def esp32_worker():
    """Worker thread for ESP32 WebSocket communication"""
    global esp32_client
    
    if not enable_esp32:
        print("[ESP32] Disabled, skipping connection")
        return
    
    print(f"[ESP32] Starting worker, connecting to {esp32_ip}:{esp32_port}...")
    
    # Create new event loop for this thread
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    
    client = ESP32Client(esp32_ip, esp32_port)
    esp32_client = client
    
    async def run_client():
        """Run the WebSocket client"""
        while state.running:
            if not client.connected:
                connected = await client.connect()
                if not connected:
                    await asyncio.sleep(2)
                    continue
            
            # Get current tracking info from state
            with state.lock:
                dx = state.esp32_dx
                dy = state.esp32_dy
                found = state.esp32_found
            
            # Send tracking info to ESP32
            await client.send_track_info(dx, dy, found)
            
            # Small delay between sends
            await asyncio.sleep(0.1)
    
    try:
        loop.run_until_complete(run_client())
    except Exception as e:
        print(f"[ESP32] Worker error: {e}")
    finally:
        loop.run_until_complete(client.disconnect())
        loop.close()
        print("[ESP32] Worker stopped")
        
def motor_worker():
    print("[System] Motor Thread Started.")
    while state.running:
        with state.lock:
            dx, dy, tracking = state.target_dx, state.target_dy, state.is_tracking
            curr_x, curr_y = state.position_x, state.position_y

        if tracking and abs(dx) < CENTER_TOLERANCE and abs(dy) < CENTER_TOLERANCE:
            motor_x.stop()
            motor_y.stop()
            time.sleep(0.05) 
            continue

        if not tracking:
            motor_x.stop(); motor_y.stop()
            time.sleep(0.1); continue
        
        # Calculating where to step to
        step_x = 0
        step_y = 0

        # Whether right or left
        if dx > CENTER_TOLERANCE: step_x = 1
        elif dx < -CENTER_TOLERANCE: step_x = -1
        
        # Whether up or down
        if dy > CENTER_TOLERANCE: step_y = 1
        elif dy < -CENTER_TOLERANCE: step_y = -1

        # If next desired position is over (or "under") allowed position, don't move
        if step_x == 1 and curr_x >= MAX_STEPS_X: step_x = 0
        if step_x == -1 and curr_x <= 0: step_x = 0
        
        if step_y == 1 and curr_y >= MAX_STEPS_Y: step_y = 0
        if step_y == -1 and curr_y <= 0: step_y = 0

        # Move motors towards desired position
        if step_x != 0:
            motor_x.step(step_x)
            with state.lock: state.position_x += step_x
        else:
            motor_x.stop()

        if step_y != 0:
            motor_y.step(step_y)
            with state.lock: state.position_y += step_y
        else:
            motor_y.stop()

        time.sleep(STEP_DELAY) # Sleep for both motors, they get executed in parallel

def servo_worker():
    print("[System] Servo Thread Started.")
    while state.running:
        should_fire = False
        piston_retracting = False
        with state.lock:
            should_fire = state.is_firing
            piston_retracting = state.piston_retracting

        # Don't fire if piston is still retracting
        if should_fire and not piston_retracting:
            # Move to 90 degrees
            servo_pwm.ChangeDutyCycle(7.5)
            time.sleep(0.5) # Wait for servo to reach position

            # Move back to 0 degrees
            servo_pwm.ChangeDutyCycle(2.5)
            time.sleep(0.5) # Wait for servo to reach position

            # Trigger piston retraction after shot
            with state.lock:
                state.is_firing = False
                state.piston_trigger = True
        else:
            servo_pwm.ChangeDutyCycle(0)
            time.sleep(0.1)

def piston_worker():
    """Worker thread for piston stepper motor - retracts after a shot"""
    print("[System] Piston Thread Started.")
    while state.running:
        trigger = False
        with state.lock:
            trigger = state.piston_trigger
            state.piston_trigger = False

        if trigger:
            with state.lock:
                state.piston_retracting = True
            print("[Piston] Retracting...")
            
            # Retract piston by moving stepper motor
            for _ in range(PISTON_RETRACT_STEPS):
                if not state.running:
                    break
                motor_piston.step(1)  # Move forward/retract direction
                time.sleep(STEP_DELAY)
            
            motor_piston.stop()
            
            with state.lock:
                state.piston_retracting = False
            print("[Piston] Retraction complete.")
        else:
            time.sleep(0.05)

# ==========================================
# 4. VISION THREAD
# ==========================================
def get_target_person(boxes, frame_center, current_track_id):
    cx, cy = frame_center
    closest_box, target_id = None, None
    min_dist = float('inf')
    
    # Priority: Locked ID
    if current_track_id is not None:
        for box in boxes:
            if box.id is not None and int(box.id) == current_track_id:
                return box, current_track_id

    # Fallback: Nearest to center
    for box in boxes:
        bx, by, _, _ = box.xywh[0].cpu().numpy()
        dist = math.hypot(bx - cx, by - cy)
        if dist < min_dist:
            min_dist = dist
            closest_box = box
            if box.id is not None: target_id = int(box.id)
                
    return closest_box, target_id

def vision_loop():
    model = YOLO(MODEL_PATH, task="detect")
    cap = cv2.VideoCapture(0)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    if not cap.isOpened():
        print("[Error] Camera failed to open (cv2.VideoCapture(0)).")
        return

    tracked_id = None
    history = {}
    print("[System] Vision Loop Started.")
    shown_first_frame = False

    # ---------- Person Masking Helpers ----------
    def _segment_person(roi_bgr):
        """
        Segment the person using GrabCut with a center foreground seed.
        Returns a binary mask (uint8 0/255) same size as roi.
        """
        h, w = roi_bgr.shape[:2]
        if h < 10 or w < 10:
            return np.ones((h, w), dtype="uint8") * 255

        mask = np.zeros((h, w), np.uint8)
        bg_model = np.zeros((1, 65), np.float64)
        fg_model = np.zeros((1, 65), np.float64)

        # Initialize with rectangle
        rect = (1, 1, max(2, w - 2), max(2, h - 2))

        # Seed a center ellipse as sure-foreground to reduce background inclusion
        seed_mask = np.zeros((h, w), np.uint8)
        center = (w // 2, int(h * 0.45))
        axes = (max(4, int(w * 0.2)), max(6, int(h * 0.3)))
        cv2.ellipse(seed_mask, center, axes, 0, 0, 360, 255, -1)

        try:
            cv2.grabCut(roi_bgr, mask, rect, bg_model, fg_model, 2, cv2.GC_INIT_WITH_RECT)
            # Promote seeded center to foreground
            mask[seed_mask == 255] = cv2.GC_FGD
            cv2.grabCut(roi_bgr, mask, None, bg_model, fg_model, 2, cv2.GC_INIT_WITH_MASK)
            mask = np.where((mask == cv2.GC_FGD) | (mask == cv2.GC_PR_FGD), 255, 0).astype("uint8")
        except Exception:
            mask = np.ones((h, w), dtype="uint8") * 255

        # Morph cleanup
        kernel = np.ones((5, 5), np.uint8)
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)

        # Keep only largest contour to reduce background
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        if contours:
            largest = max(contours, key=cv2.contourArea)
            cleaned = np.zeros_like(mask)
            cv2.drawContours(cleaned, [largest], -1, 255, -1)
            mask = cleaned

        # Slightly erode to avoid grabbing background edges
        mask = cv2.erode(mask, np.ones((3, 3), np.uint8), iterations=1)
        return mask

    def _find_largest_contour(mask):
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        if not contours:
            return None
        return max(contours, key=cv2.contourArea)

    def _detect_head(contour, roi_bgr):
        x, y, w, h = cv2.boundingRect(contour)
        search_height = int(h * 0.5)
        head_region = roi_bgr[y:y + search_height, x:x + w]
        if head_region.size == 0:
            return None

        gray = cv2.cvtColor(head_region, cv2.COLOR_BGR2GRAY)
        gray = cv2.GaussianBlur(gray, (5, 5), 0)
        edges = cv2.Canny(gray, 50, 150)

        contours, _ = cv2.findContours(edges, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        best_head = None
        best_score = 0

        for cnt in contours:
            area = cv2.contourArea(cnt)
            if area < 200:
                continue
            perimeter = cv2.arcLength(cnt, True)
            if perimeter == 0:
                continue
            circularity = 4 * np.pi * area / (perimeter ** 2)
            if len(cnt) >= 5:
                (_, _), (MA, ma), _ = cv2.fitEllipse(cnt)
                aspect_ratio = min(MA, ma) / max(MA, ma)
            else:
                aspect_ratio = 0
            score = circularity * aspect_ratio
            if score > best_score and 0.4 < circularity < 1.2:
                best_score = score
                best_head = cnt

        if best_head is not None:
            best_head[:, 0, 0] += x
            best_head[:, 0, 1] += y
        return best_head

    def _detect_upper_body(contour):
        x, y, w, h = cv2.boundingRect(contour)
        points = contour.reshape(-1, 2)
        rows = {}
        for px, py in points:
            rows.setdefault(py, []).append(px)
        widths = []
        for py in sorted(rows.keys()):
            xs = rows[py]
            widths.append((py, max(xs) - min(xs)))
        if len(widths) < 10:
            return None
        widths = sorted(widths)
        top_section = widths[: len(widths) // 2]
        shoulder_y = max(top_section, key=lambda x: x[1])[0]
        return x, y, w, shoulder_y - y

    def _mask_hit(mask, cx, cy, radius=8, min_ratio=0.55):
        """
        Returns True if a circle around (cx, cy) overlaps foreground mask enough.
        """
        h, w = mask.shape[:2]
        x1 = max(0, cx - radius)
        y1 = max(0, cy - radius)
        x2 = min(w, cx + radius + 1)
        y2 = min(h, cy + radius + 1)
        if x2 <= x1 or y2 <= y1:
            return False
        roi = mask[y1:y2, x1:x2]
        # Circular mask
        yy, xx = np.ogrid[:roi.shape[0], :roi.shape[1]]
        cy2 = cy - y1
        cx2 = cx - x1
        circle = (xx - cx2) ** 2 + (yy - cy2) ** 2 <= radius ** 2
        total = np.count_nonzero(circle)
        if total == 0:
            return False
        fg = np.count_nonzero(roi[circle] > 0)
        return (fg / total) >= min_ratio

    while True:
        ret, frame = cap.read()
        if not ret:
            print("[Error] Failed to read camera frame.")
            break

        h, w, _ = frame.shape
        center_x, center_y = w // 2, h // 2

        results = model.track(frame, persist=True, imgsz=640, classes=[0], verbose=False)

        # DRAW STATIC CROSSHAIR
        cv2.line(frame, (center_x - 20, center_y), (center_x + 20, center_y), (255, 255, 255), 2)
        cv2.line(frame, (center_x, center_y - 20), (center_x, center_y + 20), (255, 255, 255), 2)
        cv2.circle(frame, (center_x, center_y), CENTER_TOLERANCE, (255, 255, 255), 1)

        target_box = None
        if results[0].boxes:
            target_box, tracked_id = get_target_person(results[0].boxes, (center_x, center_y), tracked_id)

        with state.lock:
            if target_box and tracked_id is not None:
                # 1. Coordinates
                tx, ty, tw, th = target_box.xywh[0].cpu().numpy()
                x1, y1, x2, y2 = target_box.xyxy[0].cpu().numpy().astype(int)
                
                # 2. Prediction
                pred_x, pred_y = tx, ty
                if tracked_id in history:
                    lx, ly = history[tracked_id]
                    pred_x = tx + (tx - lx) * LEAD_FACTOR
                    pred_y = ty + (ty - ly) * LEAD_FACTOR
                history[tracked_id] = (tx, ty)
                
                # 3. State Update
                state.target_dx = int(pred_x - center_x)
                state.target_dy = int(pred_y - center_y)
                state.is_tracking = True
                
                # Update ESP32 state for car control (thread-safe)
                if enable_esp32:
                    state.esp32_dx = int(pred_x - center_x)
                    state.esp32_dy = int(pred_y - center_y)
                    state.esp32_found = True

                # 4. DRAWING + MASKING
                is_locked = abs(state.target_dx) < CENTER_TOLERANCE and abs(state.target_dy) < CENTER_TOLERANCE
                color = (0, 255, 0) if is_locked else (0, 165, 255) # Green if locked, Orange if moving
                
                # Build a refined mask for the target person ROI
                roi_x1 = max(0, x1)
                roi_y1 = max(0, y1)
                roi_x2 = min(w, x2)
                roi_y2 = min(h, y2)
                person_mask = None
                contour = None
                head = None
                upper_body = None
                if roi_x2 > roi_x1 and roi_y2 > roi_y1:
                    roi = frame[roi_y1:roi_y2, roi_x1:roi_x2]
                    if roi.size > 0:
                        person_mask = _segment_person(roi)
                        contour = _find_largest_contour(person_mask)
                        if contour is not None and show_frame:
                            head = _detect_head(contour, roi)
                            upper_body = _detect_upper_body(contour)

                # Bounding Box
                cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)
                # Prediction Dot
                cv2.circle(frame, (int(pred_x), int(pred_y)), 5, (0, 0, 255), -1)
                # Label
                label = f"ID:{tracked_id} {' LOCKED' if is_locked else ' TRACKING'}"
                cv2.putText(frame, label, (x1, y1 - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)
                # Line from center to target
                cv2.line(frame, (center_x, center_y), (int(pred_x), int(pred_y)), color, 1)

                # Optional: draw refined contour to visualize masking
                if show_frame and contour is not None:
                    cv2.drawContours(frame[roi_y1:roi_y2, roi_x1:roi_x2], [contour], -1, (0, 255, 0), 1)
                if show_frame and head is not None:
                    cv2.drawContours(frame[roi_y1:roi_y2, roi_x1:roi_x2], [head], -1, (255, 0, 0), 1)
                if show_frame and upper_body is not None:
                    ux, uy, uw, uh = upper_body
                    cv2.rectangle(frame[roi_y1:roi_y2, roi_x1:roi_x2], (ux, uy), (ux + uw, uy + uh), (0, 165, 255), 1)

                # Fire only if center is on-person, not just inside bounding box
                on_person = False
                if person_mask is not None:
                    if roi_x1 <= center_x < roi_x2 and roi_y1 <= center_y < roi_y2:
                        local_x = center_x - roi_x1
                        local_y = center_y - roi_y1
                        on_person = _mask_hit(person_mask, local_x, local_y, radius=10, min_ratio=0.6)

                if on_person:
                    print(f"[DEBUG] Target Met: Pos({int(tx)}, {int(ty)})")
                    # Only fire if not already firing and piston is not retracting
                    if not state.is_firing and not state.piston_retracting:
                        state.is_firing = True

            else:
                state.is_tracking = False
                # Update ESP32 state - person not found (thread-safe)
                if enable_esp32:
                    state.esp32_found = False
                if len(history) > 50: history.clear()
        if show_frame:
            try:
                cv2.imshow("Turret View", frame)
                if not shown_first_frame:
                    print("[Video] OpenCV window created.")
                    shown_first_frame = True
            except cv2.error as e:
                print(f"[Error] OpenCV display failed: {e}")
                break
        if cv2.waitKey(1) & 0xFF == ord('q'):
            state.running = False
            break

    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    if calibration_mode:
        try:
            calibrate_steps()
        except Exception as e:
            print("Something went wrong during manual calibration. Error:", e)
        finally:
            print("Manual calibration finished. Exiting... ")
            sys.exit(1)
    try:
        calibrate_motor()

        m_thread = threading.Thread(target=motor_worker, daemon=True)
        m_thread.start()
        servo_thread = threading.Thread(target=servo_worker, daemon=True)
        servo_thread.start()
        piston_thread = threading.Thread(target=piston_worker, daemon=True)
        piston_thread.start()
        
        # Start ESP32 worker thread if enabled
        if enable_esp32:
            esp32_thread = threading.Thread(target=esp32_worker, daemon=True)
            esp32_thread.start()
        
        vision_loop()
    except KeyboardInterrupt:
        print("Stopped the script")
    finally:
        state.running = False
        GPIO.cleanup()
        print("Script finished, GPIO cleaned")
