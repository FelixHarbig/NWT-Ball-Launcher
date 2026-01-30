# Person Tracking Test

This test tracks a person in real time from a camera feed.

It opens a window with a live camera feed and draws a green rectangle around detected persons.

---



## ⚙️ Overview

The script tracks a person using the small yolo11n model.

Useful for testing hardware performance and camera functionality.

---



## Usage

1. Create a Python virtual environment
2. Install dependencies: `pip install ultralytics opencv-python numpy`
3. Run the script - the YOLO model will download automatically

If the model doesn't download automatically, run: `yolo detect predict model=yolo11n.pt`
