# ESP32 Stepper Motor Test (28BYJ-48 + ULN2003AN)

This project is a **simple ESP-IDF test script** to control a **28BYJ-48 stepper motor** using a **ULN2003AN driver board**.  
It demonstrates basic step sequencing via GPIO output pins, resulting in a full 360° motor rotation.

---

## ⚙️ Overview

This test script rotates the 28BYJ-48 stepper motor **one full turn (512 steps)** by sequentially energizing the ULN2003 driver inputs.

It’s ideal for:
- Verifying hardware connections  
- Testing GPIO output timing  
- Getting started with ESP-IDF stepper control

---

## Hardware Setup

| Component | Description |
|------------|-------------|
| **Microcontroller** | ESP32 (ESP-IDF framework) |
| **Stepper Motor** | 28BYJ-48 (5V) |
| **Driver Board** | ULN2003AN |
| **Power Supply** | 5V DC |

---

## 🔌 Wiring

| ESP32 GPIO | ULN2003 Pin | Stepper Coil |
|-------------|--------------|---------------|
| GPIO 27 | IN1 | Coil A |
| GPIO 26 | IN2 | Coil B |
| GPIO 25 | IN3 | Coil C |
| GPIO 33 | IN4 | Coil D |
| 5V | VCC | Motor VCC |
| GND | GND | Motor GND |

---

### Example Setup

![Stepper](../images/stepper.jpg)

---

## How It Works

Each GPIO pin drives one input of the ULN2003 board, which in turn activates a motor coil.  
By cycling through the correct sequence with short delays (`ets_delay_us(2000)`), the motor advances one step at a time.

```c
for (int i = 0; i < 512; i++) {
    // Sequence 1 → 2 → 3 → 4
    gpio_set_level(GPIO_NUM_27, 1);
    gpio_set_level(GPIO_NUM_26, 0);
    gpio_set_level(GPIO_NUM_25, 0);
    gpio_set_level(GPIO_NUM_33, 0);
    ets_delay_us(2000);
    // ...
}
```
