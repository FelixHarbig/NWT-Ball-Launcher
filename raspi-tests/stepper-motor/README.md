# Raspberry Pi Stepper Motor Test (28BYJ-48 + ULN2003AN)

This project is a **simple Python test script** to control a **28BYJ-48 stepper motor** using a **ULN2003AN driver board**.
It demonstrates basic step sequencing via GPIO output pins, resulting in a loop of a full 360° motor rotation and a full -360° motor rotation.

---

## ⚙️ Overview

The full_step.py test script rotates the 28BYJ-48 stepper motor **one full turn (512 steps)** by sequentially energizing the ULN2003 driver inputs.

The stepper.py test script does this using half-steps rather than full-steps.

It’s ideal for:

- Verifying hardware connections
- Testing GPIO output timing

---

## Hardware Setup

| Component               | Description   |
| ----------------------- | ------------- |
| **Controller**    | Rasperry Pi   |
| **Stepper Motor** | 28BYJ-48 (5V) |
| **Driver Board**  | ULN2003AN     |

---

## 🔌 Wiring

| Raspberry Pi GPIO | ULN2003 Pin | Stepper Coil |
| ----------------- | ----------- | ------------ |
| GPIO 23           | IN1         | Coil A       |
| GPIO 24           | IN2         | Coil B       |
| GPIO 25           | IN3         | Coil C       |
| GPIO 8            | IN4         | Coil D       |
| 5V                | VCC         | Motor VCC    |
| GND               | GND         | Motor GND    |

---

## How It Works

Each GPIO pin drives one input of the ULN2003 board, which in turn activates a motor coil.
By cycling through the correct sequence with short delays, the motor advances one step at a time.

```c
seq = [
    [1, 0, 0, 1],
    [1, 0, 0, 0],
    [1, 1, 0, 0],
    [0, 1, 0, 0],
    [0, 1, 1, 0],
    [0, 0, 1, 0],
    [0, 0, 1, 1],
    [0, 0, 0, 1]
]
```
