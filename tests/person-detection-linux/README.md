# ESP32 Person detection web (tflite-micro)

This project is a showcase **how well the esp32 can detect people in images**. It is only really possible trough ESP-IDF as there are no well supported compatability layers for other frameworks.

---

## Overview

The esp32 hosts an access point and a web interface (usually found at 192.168.4.1). It recieves a picture (or part of a picture) from the web and returns the confidence of a person being there.
This script is local and the entire model works on the esp32.

---

## Hardware Setup

| Component | Description |
|------------|-------------|
| **Microcontroller** | ESP32 (ESP-IDF framework) |

---

## Wiring

The esp32 only needs to be connected to power. This is usually achieved trough plugging in the micro-USB. Of course a 5V connection can also be used directly.

---

### Example Setup

![esp32](../images/esp32.jpg)

---

## How It Works

The script uses a pretrained model from the [esp32 tflite-micro example](TODO insert link here) to detect people.
