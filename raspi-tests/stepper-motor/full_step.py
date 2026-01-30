import RPi.GPIO as GPIO
import time

# ---------------- GPIO PINS (BCM) ----------------
IN1 = 23
IN2 = 24
IN3 = 25
IN4 = 8

# ---------------- SETUP ----------------
GPIO.setmode(GPIO.BCM)
GPIO.setup(IN1, GPIO.OUT)
GPIO.setup(IN2, GPIO.OUT)
GPIO.setup(IN3, GPIO.OUT)
GPIO.setup(IN4, GPIO.OUT)

# ---------------- MOTOR CONSTANTS ----------------
STEPS_PER_REVOLUTION = 512 # 2048   # FULL STEP for 28BYJ-48
DELAY = 0.003                 # seconds (tune for torque vs speed)

# ---------------- FULL-STEP SEQUENCE (2-phase) ----------------
# This is TRUE full-step, not half-step
SEQ = [
    (1, 1, 0, 0),
    (0, 1, 1, 0),
    (0, 0, 1, 1),
    (1, 0, 0, 1),
]

# ---------------- STEP FUNCTION ----------------
def step(step_state):
    GPIO.output(IN1, step_state[0])
    GPIO.output(IN2, step_state[1])
    GPIO.output(IN3, step_state[2])
    GPIO.output(IN4, step_state[3])
    time.sleep(DELAY)

# ---------------- ROTATION FUNCTIONS ----------------
def rotate_forward(steps):
    for _ in range(steps):
        for s in SEQ:
            step(s)

def rotate_backward(steps):
    for _ in range(steps):
        for s in reversed(SEQ):
            step(s)

# ---------------- MAIN LOOP ----------------
try:
    while True:
        # One full revolution clockwise
        rotate_forward(STEPS_PER_REVOLUTION)
        time.sleep(2)

        # One full revolution counter-clockwise
        rotate_backward(STEPS_PER_REVOLUTION)
        time.sleep(2)

except KeyboardInterrupt:
    print("\nStopped by user")

finally:
    GPIO.cleanup()
