# NWT-Ball-Launcher

A ball launcher project with computer vision tracking and motor control.

## Project Structure

- [`raspi-tests/`](raspi-tests/) - Raspberry Pi tests and implementations
  - [`camera-stepper/`](raspi-tests/camera-stepper/) - Camera and stepper motor integration
  - [`person-tracking/`](raspi-tests/person-tracking/) - Person detection and tracking
  - [`servo/`](raspi-tests/servo/) - Servo motor control
  - [`stepper-motor/`](raspi-tests/stepper-motor/) - Stepper motor tests
- [`esp32-tests/`](esp32-tests/) - ESP32 firmware tests
  - [`servo/`](esp32-tests/servo/) - ESP32 servo control
  - [`stepper-motor/`](esp32-tests/stepper-motor/) - ESP32 stepper motor control

## Hardware

- Raspberry Pi (for vision and control)
- ESP32 (for motor control)
- Stepper motors
- Servo motors
- Camera module

## Getting Started

See the README files in each subdirectory for specific setup instructions.