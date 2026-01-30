# Raspberry Pi Servo Motor Test

Simple test script to control a servo motor using PWM on the Raspberry Pi.

## Hardware Setup

| Component        | Description     |
| ---------------- | --------------- |
| **Controller**   | Raspberry Pi    |
| **Servo**        | Standard SG90 or similar |

## Wiring

| Raspberry Pi GPIO | Servo |
| ----------------- | ----- |
| GPIO 12           | Signal |
| 5V                | VCC   |
| GND               | GND   |

## Usage

```bash
python main.py
```

The servo will sweep through its range continuously until you press Ctrl+C.
