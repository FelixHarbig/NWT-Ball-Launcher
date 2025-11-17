# NWT Ball Launcher - Camera Control System

This project is the main control system for the NWT Ball Launcher. It uses an AI-Thinker ESP32-CAM module to perform person detection and control the firing mechanism, which consists of a servo and a stepper motor.

## Features

- **Person Detection:** Uses a TensorFlow Lite model to detect people in the camera's view.
- **Multi-Stage Scanning:** Implements a sophisticated scanning logic to find and confirm targets:
    1.  **Full Scan:** Scans the entire 96x96 frame. If a person is found with high confidence, it proceeds to the final check.
    2.  **2x2 Grid Scan:** If the full scan fails, it divides the view into 4 quadrants and scans each one.
    3.  **3x3 Grid Scan:** Once a person is roughly located, it divides the view into 9 sections for fine-grained tracking.
    4.  **Target Lock & Fire:** If a person is confidently detected in the center of the 3x3 grid, the firing sequence is initiated.
- **Motor Control:**
    - **Stepper Motor:** Loads the firing mechanism by rotating a set number of times.
    - **Servo Motor:** Acts as the trigger, moving to a "fire" position and then resetting.
- **Web Interface:**
    - The ESP32-CAM hosts a Wi-Fi Access Point (`NWT-Ball-Launcher`).
    - Connect to the Wi-Fi (password: `fireaway`) and navigate to `http://192.168.4.1` in a web browser.
    - A live, grayscale video stream from the camera is displayed.

## Pin Layout (AI-Thinker ESP32-CAM)

It is crucial to connect the components to the correct GPIO pins.

| Component          | GPIO Pin | ESP32-CAM Label | Notes                               |
| ------------------ | -------- | --------------- | ----------------------------------- |
| **Camera**         | -        | -               | Internal connection (see `sdkconfig.defaults`) |
| **Servo Motor**    | `GPIO 2` | `U0RXD`         | Signal wire for the servo           |
| **Stepper IN1**    | `GPIO 12`| `HS2_DATA2`     |                                     |
| **Stepper IN2**    | `GPIO 13`| `HS2_DATA3`     |                                     |
| **Stepper IN3**    | `GPIO 14`| `HS2_CLK`       |                                     |
| **Stepper IN4**    | `GPIO 15`| `HS2_CMD`       |                                     |
| **Ready Sensor**   | `GPIO 4` | `HS2_DATA1`     | Digital sensor for "ready to fire" state |
| **MicroSD Card**   | -        | -               | The MicroSD slot cannot be used if using the stepper motor pins. |

**IMPORTANT:** The GPIO pins used for the stepper motor (`12`, `13`, `14`, `15`) are the same pins often used for the MicroSD card slot on the ESP32-CAM. You cannot use the SD card and the stepper motor with this pin configuration.

## Functionality

1.  **Initialization:** The system initializes the camera, motors, Wi-Fi, and the person detection model. The servo moves to its default position (`10°`).
2.  **Scanning:** The main loop continuously captures frames from the camera and runs the detection algorithm.
3.  **Targeting:** If a person is found, the system tries to lock on by scanning the 3x3 grid.
4.  **Firing:** If the person is locked in the center grid and the "ready" sensor is active, the following sequence occurs:
    a. The stepper motor turns twice to load the mechanism.
    b. The servo turns to the "fire" position (`170°`).
    c. The servo returns to the default position after a short delay.

## Building and Flashing

This project is built using the ESP-IDF framework.

1.  **Setup ESP-IDF:** Make sure you have the Espressif IoT Development Framework installed and configured.
2.  **Custom Model:** Place your `person_detect_model_data.cc` and `person_detect_model_data.h` files in the `main/` directory, replacing the placeholder files.
3.  **Build:** Navigate to this directory (`camera/`) and run `idf.py build`.
4.  **Flash:** Connect the ESP32-CAM to your computer via a USB-to-serial adapter, put it in bootloader mode, and run `idf.py -p /dev/ttyUSB0 flash monitor` (replace `/dev/ttyUSB0` with your serial port).

## Configuration

Key parameters can be adjusted in the source code:

- **`main/motor_control.h`**: Change servo angles, motor GPIO pins, and sensor pin.
- **`main/person_detection.h`**: Adjust the confidence threshold for detection.
- **`main/web_server.h`**: Change the Wi-Fi SSID and password.
