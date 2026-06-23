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

## System flow diagram

```mermaid
flowchart TB
    subgraph RPi["Raspberry Pi — raspi-tests/camera-stepper/main.py"]
        direction TB
        RSTART(["main.py start"]) --> ARGS["Parse args:<br/>--demo, --calibrate,<br/>--no_esp32, etc."]
        ARGS --> RCALIB{"--calibrate?"}
        RCALIB -->|Yes| RCALIB_STEPS["calibrate_steps()<br/>Auto-home to hall sensors,<br/>then W/A/S/D manual move,<br/>Enter to save config.json, exit"]
        RCALIB -->|No| RHOME["calibrate_motor()<br/>Step X/Y toward hall sensors<br/>until both trigger → origin (0,0)"]
        RHOME --> RTHREADS["Spawn daemon threads:"]

        subgraph Vision["vision_loop (main thread)"]
            V1["Open camera 640×480,<br/>load YOLO11 model (ONNX)"] --> V2["Read frame from /dev/video0"]
            V2 --> V3["YOLO .track() infer<br/>classes=[0] (person)<br/>ByteTrack tracker, persist=True"]
            V3 --> V4["get_target_person()<br/>Priority: locked track ID<br/>Fallback: nearest to center"]
            V4 --> V5{"Person found?"}
            V5 -->|No| VNO["state.is_tracking = False<br/>state.esp32_found = False<br/>(stale history cleared @50)"]
            V5 -->|Yes| V6["LEAD_FACTOR prediction<br/>pred_x = tx + (tx - last_x) * 5<br/>pred_y = ty + (ty - last_y) * 5"]
            V6 --> V7["state.target_dx = pred_x - center_x<br/>state.target_dy = pred_y - center_y<br/>state.is_tracking = True<br/>esp32_dx/dy/found update"]
            V7 --> V8["_segment_person()<br/>GrabCut on ROI bounding box<br/>(downscaled 2×, ellipse seed)"]
            V8 --> V9["_find_largest_contour()<br/>→ _detect_head()<br/>→ _detect_upper_body()"]
            V9 --> V10["_mask_hit(center, r=10, ≥60%)<br/>Check crosshair on person mask"]
            V10 --> V11{"On person &<br/>not firing &<br/>not retracting?"}
            V11 -->|Yes| VFIRE["state.is_firing = True"]
            V11 -->|No| VDRAW["Draw bounding box, contour,<br/>head, body, FPS overlay, crosshair"]
            VNO --> VDRAW
            VFIRE --> VDRAW
            VDRAW --> VSHOW["cv2.imshow('Turret View')"]
            VSHOW --> VQ{"Key 'q'?"}
            VQ -->|No| V2
            VQ -->|Yes| VEXIT["cap.release(),<br/>cv2.destroyAllWindows()"]
        end

        subgraph Mtr["motor_worker (turret steppers)"]
            MW["Loop while running:"] --> MW_READ["Read state:<br/>target_dx, target_dy, is_tracking,<br/>position_x, position_y"]
            MW_READ --> MW_TRACK{"is_tracking &&<br/>(|dx|>CENTERTOL<br/>or |dy|>CENTERTOL)?"}
            MW_TRACK -->|No| MW_STOP["motor_x.stop()<br/>motor_y.stop()"]
            MW_TRACK -->|Yes| MW_DIR["Compute step dir:<br/>dx>30 → +1, dx<-30 → -1<br/>dy>30 → +1, dy<-30 → -1"]
            MW_DIR --> MW_CLAMP["Clamp to [0, MAX_STEPS_X/Y]"]
            MW_CLAMP --> MW_STEP["Step X and Y motors<br/>update state.position_x/y"]
            MW_STEP --> MW_READ
            MW_STOP --> MW_SLEEP["sleep 50-100ms"] --> MW_READ
        end

        subgraph Svo["servo_worker"]
            SV["Loop:"] --> SV_READ["Read state.is_firing<br/>state.piston_retracting"]
            SV_READ --> SV_FIRE{"should_fire &<br/>not retracting?"}
            SV_FIRE -->|Yes| SV_SHOOT["Servo 0°→90° (duty 7.5, 0.5s)"]
            SV_SHOOT --> SV_RET["Servo 90°→0° (duty 2.5, 0.5s)"]
            SV_RET --> SV_TRIG["state.is_firing = False<br/>state.piston_trigger = True"]
            SV_FIRE -->|No| SV_IDLE["Servo duty 0, sleep 0.1s"]
            SV_IDLE --> SV_READ
            SV_TRIG --> SV_READ
        end

        subgraph Pst["piston_worker"]
            PS["Loop:"] --> PS_READ["Read state.piston_trigger"]
            PS_READ --> PS_TRIG{"Trigger?"}
            PS_TRIG -->|No| PS_SLP["sleep 50ms"] --> PS_READ
            PS_TRIG -->|Yes| PS_RETRACT["state.piston_retracting = True<br/>Step piston motor PISTON_RETRACT_STEPS"]
            PS_RETRACT --> PS_DONE["state.piston_retracting = False<br/>state.piston_trigger = False"]
            PS_DONE --> PS_READ
        end

        subgraph WSC["esp32_worker"]
            EWS["Loop: create asyncio event loop"] --> EWS_CONN["Connect:<br/>ws://192.168.4.1:80/ws"]
            EWS_CONN --> EWS_READ["Read state:<br/>esp32_dx, dy, esp32_found,<br/>is_tracking, pos_x, pos_y"]
            EWS_READ --> EWS_TRACK{"is_tracking?"}
            EWS_TRACK -->|No| EWS_NF["send_track(found=False)"]
            EWS_TRACK -->|Yes| EWS_LIMIT{"Turret at step limit<br/>& target beyond?"}
            EWS_LIMIT -->|Yes| EWS_MOVE["send_track(dx=real, dy=real,<br/>found=True, hold=False)"]
            EWS_LIMIT -->|No| EWS_HOLD["send_track(dx=0, dy=0,<br/>found=True, hold=True)"]
            EWS_NF --> EWS_SLP["sleep 0.1s"] --> EWS_READ
            EWS_MOVE --> EWS_SLP
            EWS_HOLD --> EWS_SLP
        end
    end

    subgraph ESP32["ESP32-Car — esp32-car.ino"]
        direction TB
        EBOOT(["ESP32 boot"]) --> ESETUP["setup()"]
        ESETUP --> EINIT["Serial 115200<br/>initMotors()<br/>initSensors()<br/>pinMode(LED, OUTPUT)"]
        EINIT --> EWIFI["initWiFi()<br/>WiFi.mode(WIFI_AP)<br/>SSID: ESP32-Car<br/>IP: 192.168.4.1"]
        EWIFI --> EWS_SRV["initWebSocket()<br/>AsyncWebServer :80<br/>ws handler on /ws"]
        EWS_SRV --> ELOOP["loop()"]

        ELOOP --> EWS_CLN["ws.cleanupClients()"]
        EWS_CLN --> ESM["updateState()"]
        ESM --> ECHECK_TO["checkTimeout()<br/>>2000ms no cmd?"]
        ECHECK_TO -->|Timeout| E_SEARCH["MODE_SEARCHING"]

        subgraph CarSM["Car State Machine"]
            direction TB
            ESM_SW{"currentMode?"}
            ESM_SW -->|STOPPED| ESTOP["stopMotors()"]
            ESM_SW -->|TRACKING| ETRACK["(passive — set in<br/>processTrackCommand)"]
            ESM_SW -->|SEARCHING| ESEARCH["executeSearch()"]
            ESM_SW -->|MANUAL| EMAN["(passive — set in<br/>processManualCommand)"]
            ESM_SW -->|OBSTACLE_AVOID| EOA["performObstacleAvoidance()"]
        end

        subgraph WS_Handler["WebSocket Message Handler"]
            EWS_H["on WebSocket data:"] --> E_PARSE["processWebSocketMessage()<br/>deserialize JSON"]
            E_PARSE --> E_TYPE{"type:"}
            E_TYPE -->|"track"| E_TRACK["processTrackCommand<br/>(dx, dy, found, hold)"]
            E_TYPE -->|"manual"| E_MAN["processManualCommand<br/>(left, right)"]
            E_TYPE -->|"status_req"| E_STAT["sendStatus()<br/>{type:status, speeds,<br/>obstacle distances, mode}"]
        end

        subgraph Track_Decision["Tracking Decision Tree"]
            ET["processTrackCommand()"] --> ETF{"found?"}
            ETF -->|No| ETS["MODE_SEARCHING<br/>return"]
            ETF -->|Yes| ETH{"hold?"}
            ETH -->|Yes| ETS_HOLD["stopMotors()<br/>(car stands by)"]
            ETH -->|No| ETO{"checkObstacles()<br/><30cm?"}
            ETO -->|Yes| ETOA["performObstacleAvoidance()"]
            ETOA --> E_OA_DECIDE["Back away,<br/>turn to open side"]
            ETO -->|No| ETW{"checkWarningDistance()<br/><50cm?"}
            ETW -->|Yes| ETWTURN["Turn toward open side<br/>at TRACKING_SPEED * 0.5"]
            ETW -->|No| ETDX{"|dx| < CENTER_TOLERANCE (30)?"}
            ETDX -->|Yes| ETFWD["setMotors(50, 50)<br/>Forward"]
            ETDX -->|No, dx>0| ETR["Turn right<br/>map(dx, 30→320, 20→100)%"]
            ETDX -->|No, dx<0| ETL["Turn left<br/>map(dx, 30→320, 20→100)%"]
        end

        subgraph Obstacle_Avoid["Obstacle Avoidance"]
            EO["readSensors()<br/>3× HC-SR04, averaged"] --> EO_CLOSE{"Middle <30cm?"}
            EO_CLOSE -->|Yes| EO_BACK["Back away -SEARCH_SPEED"]
            EO_BACK --> EO_TURN
            EO_CLOSE --> EO_TURN["Turn toward side<br/>with more clearance"]
            EO_TURN --> EO_DONE["delay(800ms)<br/>resume previous mode"]
        end

        subgraph Search_Beh["Search Behavior"]
            ES["Check obstacles first"] --> ESPAT["Pattern:<br/>Forward 2s → Turn 1.5s<br/>(alternate direction each turn)"]
        end

        ELOOP --> EMOTORS["stepperLeft.runSpeed()<br/>stepperRight.runSpeed()"]
        EMOTORS --> ELED["updateStatusLed()<br/>(blink rate per mode)"]
        ELED --> EDELAY["delay(10)"] --> ELOOP
    end

    RPi -.->|"WebSocket JSON<br/>ws://192.168.4.1:80/ws"| ESP32
    EWS_NF -.->|"{\"type\":\"track\",\"found\":false}"| E_PARSE
    EWS_MOVE -.->|"{\"type\":\"track\",\"dx\":int,\"dy\":int,<br/>\"found\":true,\"hold\":false}"| E_PARSE
    EWS_HOLD -.->|"{\"type\":\"track\",\"dx\":0,\"dy\":0,<br/>\"found\":true,\"hold\":true}"| E_PARSE
```
