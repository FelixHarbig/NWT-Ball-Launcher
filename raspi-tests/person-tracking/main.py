import cv2
import time
import numpy as np
from ultralytics import YOLO

TARGET_W = 640
TARGET_H = 360
CONFIDENCE = 0.4

DETECTION_TIMEOUT = 0.5     # seconds to keep last boxes alive
PRINT_INTERVAL = 0.2        # seconds
CAMERA_INDEX = 0


def letterbox_fixed(frame):
    h, w = frame.shape[:2]
    scale = min(TARGET_W / w, TARGET_H / h)
    nw, nh = int(w * scale), int(h * scale)

    resized = cv2.resize(frame, (nw, nh), interpolation=cv2.INTER_LINEAR)

    canvas = np.zeros((TARGET_H, TARGET_W, 3), dtype=np.uint8)
    dx = (TARGET_W - nw) // 2
    dy = (TARGET_H - nh) // 2
    canvas[dy:dy + nh, dx:dx + nw] = resized

    return canvas, scale, dx, dy

def realtime_camera_tracking(model, show=True):
    cap = cv2.VideoCapture(CAMERA_INDEX)
    if not cap.isOpened():
        raise RuntimeError("Cannot open camera")

    # Force fixed inference size
    model.overrides["imgsz"] = (TARGET_H, TARGET_W)
    model.overrides["verbose"] = False
    model.overrides["device"] = "cpu"

    last_boxes = []          # [(tid, x, y, w, h)]
    last_detect_time = 0.0
    last_print_time = 0.0
    prev_frame_time = 0.0

    print("Starting real-time camera tracking... Press Q to quit.")

    while True:
        # Always grab the most recent frame (drop old ones)
        for _ in range(2):
            cap.grab()
        ret, frame = cap.read()
        if not ret:
            break

        now = time.time()
        annotated = frame.copy()

        # ---------------- INFERENCE ----------------
        lb, scale, dx, dy = letterbox_fixed(frame)

        results = model.track(
            lb,
            conf=CONFIDENCE,
            persist=True,
            tracker="bytetrack.yaml"
        )

        r = results[0]
        new_boxes = []

        if r.boxes is not None and r.boxes.id is not None:
            boxes = r.boxes.xywh.cpu().numpy()
            ids = r.boxes.id.cpu().numpy().astype(int)
            classes = r.boxes.cls.cpu().numpy().astype(int)

            for (x, y, bw, bh), tid, cls in zip(boxes, ids, classes):
                name = model.names[int(cls)]
                if name.lower() != "person":
                    continue

                # Undo letterbox
                x = (x - dx) / scale
                y = (y - dy) / scale
                bw /= scale
                bh /= scale

                new_boxes.append((tid, x, y, bw, bh))

        if new_boxes:
            last_boxes = new_boxes
            last_detect_time = now

        if now - last_detect_time < DETECTION_TIMEOUT:
            for tid, x, y, bw, bh in last_boxes:
                x1 = int(x - bw / 2)
                y1 = int(y - bh / 2)
                x2 = int(x + bw / 2)
                y2 = int(y + bh / 2)

                cv2.rectangle(annotated, (x1, y1), (x2, y2), (0, 255, 0), 2)
                cv2.putText(annotated, f"ID {tid}", (x1, y1 - 6), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)

        if now - last_print_time > PRINT_INTERVAL:
            for tid, x, y, bw, bh in last_boxes:
                print(
                    f"[{now:.2f}] "
                    f"ID {tid}: x={x:.1f}, y={y:.1f}, w={bw:.1f}, h={bh:.1f}"
                )
            last_print_time = now

        if show:
            fps = 1/(now-prev_frame_time) if prev_frame_time else 0
            prev_frame_time = now
            cv2.putText(annotated, str(round(fps,2)), (7,70), cv2.FONT_HERSHEY_SIMPLEX, 3, (100, 255, 0), 3, cv2.LINE_AA)
            cv2.imshow("YOLO Real-Time Camera Tracking", annotated)
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    model = YOLO("yolo11n.pt")
    print("Loaded classes:", model.names)
    realtime_camera_tracking(model, show=True)
