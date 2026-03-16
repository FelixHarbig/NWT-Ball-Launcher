# ESP32 Car Control System

ESP32-based autonomous car with WiFi control, tank drive motors, and ultrasonic obstacle avoidance.

## Overview

This project implements an ESP32 car control system that:
- Hosts a WiFi Access Point for Raspberry Pi connection
- Runs a WebSocket server for real-time communication
- Controls two motors using tank drive
- Uses 3 HC-SR04 ultrasonic sensors for obstacle detection
- Implements autonomous behaviors: tracking, searching, and obstacle avoidance

## Hardware Requirements

- ESP32 Dev Board
- 2x DC Motors (tank drive configuration)
- Motor Driver (L298N, L293D, or similar)
- 304 Ultrasonic Sensors
- Power Supplyx HC-SR (7.4V LiPo recommended)
- Chassis/Frame

## Pin Configuration

| Component | Pin | Description |
|-----------|-----|-------------|
| Left Motor IN1 | 32 | Direction pin 1 |
| Left Motor IN2 | 33 | Direction pin 2 |
| Left Motor PWM | 25 | Speed control |
| Right Motor IN1 | 26 | Direction pin 1 |
| Right Motor IN2 | 27 | Direction pin 2 |
| Right Motor PWM | 14 | Speed control |
| US Trig Middle | 5 | Middle sensor trigger |
| US Echo Middle | 18 | Middle sensor echo |
| US Trig Left | 4 | Left sensor trigger |
| US Echo Left | 19 | Left sensor echo |
| US Trig Right | 16 | Right sensor trigger |
| US Echo Right | 17 | Right sensor echo |
| Status LED | 2 | Built-in LED |

## Installation

### Prerequisites

1. Arduino IDE with ESP32 board support
2. Required libraries:
   - WiFi (built-in)
   - WebSocketServer (from library manager)
   - ArduinoJson (from library manager)

### Upload

1. Open `esp32-car.ino` in Arduino IDE
2. Select your ESP32 board from Tools > Board
3. Select the appropriate port
4. Upload the sketch

## WiFi Connection

After upload, the ESP32 creates an Access Point:
- **SSID**: ESP32-Car
- **Password**: 12345678
- **IP Address**: 192.168.4.1
- **WebSocket Port**: 80

## Communication Protocol

### Raspi → ESP32 Messages

**Track Command** (person detected):
```json
{
    "type": "track",
    "dx": 150,
    "dy": -30,
    "found": true,
    "timestamp": 1699999999999
}
```

**Track Command** (person not found):
```json
{
    "type": "track",
    "found": false,
    "timestamp": 1699999999999
}
```

**Manual Control**:
```json
{
    "type": "manual",
    "left": 50,
    "right": 50,
    "timestamp": 1699999999999
}
```

**Status Request**:
```json
{
    "type": "status_req",
    "timestamp": 1699999999999
}
```

### ESP32 → Raspi Messages

**Status Response**:
```json
{
    "type": "status",
    "left_speed": 50,
    "right_speed": 30,
    "obstacle_middle": 45,
    "obstacle_left": 120,
    "obstacle_right": 80,
    "mode": "tracking",
    "timestamp": 1699999999999
}
```

## Behavior Modes

### Stopped Mode
- Motors stopped
- Status LED blinks slowly (2s interval)
- Default state on startup

### Tracking Mode
- Active when person is detected
- Follows person based on dx offset
- 50% PWM speed
- Status LED blinks fast (500ms interval)

### Searching Mode
- Active when no person detected and timeout occurs
- Drives forward with periodic turns
- 30% PWM speed
- Status LED blinks medium (1s interval)

### Manual Mode
- Active when manual control commands received
- Direct motor speed control
- Status LED blinks very fast (200ms interval)

### Obstacle Avoidance Mode
- Active when obstacle detected (<30cm)
- Backs away then turns
- Status LED blinks fastest (100ms interval)

## Obstacle Avoidance

- **Close Distance**: <30cm - Back away from obstacle
- **Warning Distance**: <50cm - Turn away from obstacle

## Search Pattern

When no person is detected, the car:
1. Moves forward for 2 seconds
2. Turns left or right for 1.5 seconds
3. Alternates turn direction
4. Repeats

## Timeout

- **Command Timeout**: 2 seconds
- If no commands received within timeout, car switches to search mode

## Key Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| Tracking Speed | 50% PWM | Speed when tracking person |
| Search Speed | 30% PWM | Speed when searching |
| Center Tolerance | 30 pixels | dx threshold for centering |
| Obstacle Close | 30 cm | Distance to back away |
| Obstacle Warning | 50 cm | Distance to turn away |
| Command Timeout | 2000 ms | Time before search mode |

## Files

- `esp32-car.ino` - Main Arduino sketch
- `config.h` - Pin definitions and constants

## Future Enhancements

- Web interface for manual control
- Camera stream integration
- Additional sensors
- Speed control via web interface

## License

MIT License
