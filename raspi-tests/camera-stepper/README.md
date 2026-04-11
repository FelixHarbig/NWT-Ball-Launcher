# Raspberry Pi Stepper Motor Tracking With Camera Test

This project is a **Python test script** to control a **28BYJ-48 stepper motor** to track a person.
It demonstrates person tracking capabilities of a Raspberry Pi.

---

## ⚙️ Overview

The main.py test script detects a person using a YOLO11n model and tells the stepper where to move to.

The conversion.py script is needed to convert a .pt file into a .onnx file. This makes the detection run a bit faster.

---

## Hardware Setup

| Component               | Description   |
| ----------------------- | ------------- |
| **Controller**    | Rasperry Pi   |
| **Stepper Motor** | 28BYJ-48 (5V) |
| **Driver Board**  | ULN2003AN     |
| **Camera**        | USB-Camera    |

---

## Usage

1. Install requirements: `pip install -r requirements.txt`
2. Run `conversion.py` to generate `yolo11n.onnx`
3. Run `main.py` with camera connected
   - Optional ESP32 control: `--esp32_ip 192.168.4.1 --esp32_port 80`
   - ESP32 WebSocket path is `/ws` (handled by the script)

> Note: If using multiple cameras, update `cv2.VideoCapture(0)` to the correct device index.

## 🔌 Wiring

**TODO**
See the code for more details
Planned: two steppers and some sensor to detect when rotation should stop

---

