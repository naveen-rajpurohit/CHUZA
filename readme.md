# CHUZA V1 🤖

Welcome to the development repository for **CHUZA V1**, my autonomous, internet-connected desktop companion robot.

This project bridges embedded systems, real-time control, and cloud connectivity. CHUZA is designed to be more than just an RC car; it's an autonomous desktop "pet" featuring a dynamic OLED personality, capacitive touch reflexes, environmental sensing, and a globally accessible FPV video feed.

This README serves as my engineering roadmap, documenting the phased architecture I am using to bring CHUZA to life.

## 🛠️ Hardware & Tech Stack

* **Microcontroller:** ESP32-S3 (Dual-core, WiFi, hardware capacitive touch)
* **Actuation:** DRV8833 Motor Driver
* **Sensors:** VL53L0X (Time-of-Flight distance/edge detection), BME280 (Temp/Humidity/Pressure)
* **Interface:** I2C OLED Display (U8g2/Adafruit_SSD1306), PWM Buzzer
* **Networking:** MQTT, WebSockets, NTP
* **Client App:** Progressive Web App (HTML/JS/React/Vue)

---

## 🗺️ Development Roadmap

### Phase 1: The Nervous System (Core Hardware & Actuation)

*Before giving CHUZA a brain or internet access, I need to ensure its physical muscles and senses are reliable.*

* **Motor Control:** Implement basic locomotion functions (`moveForward`, `turnLeft`, `turnRight`, `stop`) via the DRV8833. The goal is precise, controlled bursts rather than continuous speed.
* **Distance & Edge Sensing:** Configure the VL53L0X over I2C. I'll need to calibrate two critical thresholds: one for forward obstacle avoidance, and a downward-facing "cliff" threshold to prevent CHUZA from driving off my desk.
* **Environmental Data:** Establish I2C communication with the BME280 to monitor ambient temperature, humidity, and pressure.
* **Audio Feedback:** Utilize the ESP32's hardware PWM to create non-blocking chirps and beeps through the buzzer for status alerts.

### Phase 2: The Face & Touch (UI and Input)

*Building CHUZA's visual personality and tactile input processing.*

* **OLED Expressions:** Using an OLED library, I will draw dynamic "eyes" and expressions (`drawNeutral`, `drawBlink`, `drawHappy`, `lookLeft`).
* **Capacitive Touch Timing:** Utilizing the ESP32-S3's built-in capacitive touch pins, I'm building a timing algorithm to differentiate user taps. A ~400ms window will distinguish between commands:
* **Single Tap:** Trigger an expression change.
* **Triple Tap:** Open the system menu.


* **UI State Machine:** The main loop will handle display states efficiently:

| State | Mode | Function |
| --- | --- | --- |
| **0** | Pet Mode | Default state displaying animated eyes and idle behaviors. |
| **1** | Menu | Configuration menu accessed via triple-tap. |
| **2** | Sensor Display | Live readout of BME280 environmental data. |
| **3** | Clock / Notes | Digital clock (NTP synced) and incoming cloud messages. |

### Phase 3: The Brain (Autonomous Pet Logic)

*Fusing movement and UI into an autonomous, responsive system—running entirely locally.*

* **Strictly Non-Blocking Logic:** The `delay()` function is strictly forbidden in this architecture. I am using `millis()` timers to track when to move, blink, or poll sensors.
* **The Wander Algorithm:** In "Pet Mode," CHUZA will use `random()` to pick directions and short movement durations (0.5 – 1.5 seconds) to simulate organic exploration before returning to idle.
* **Reflexes:** The main loop will continuously poll the VL53L0X. If an edge or obstacle is detected, an immediate "reflex" will trigger: stop, back up, turn, and beep, overriding any current wander command.

### Phase 4: Global Communication (WAN Connectivity)

*Taking CHUZA off the local network so I can interact with it from anywhere in the world.*

* **NTP Sync:** Fetch local time via Network Time Protocol to power the OLED digital clock.
* **MQTT Architecture:** I am routing data through a lightweight MQTT broker (e.g., HiveMQ, Adafruit IO, or AWS IoT) for bidirectional, low-latency control.
* **Subscribe:** `robot/commands` (manual driving) and `robot/notes` (text to display on the OLED).
* **Publish:** `robot/telemetry` (BME280 data and battery status back to the cloud).



### Phase 5: FPV Camera Streaming (The WAN Bridge)

*The most technically demanding phase: streaming high-bandwidth video over a global connection from a microcontroller.*

* **Local Baseline:** First, initialize the `esp_camera` library to establish a basic HTTP MJPEG stream on my local network to verify hardware focus and wiring.
* **Cloud Relay Server:** Since standard MQTT can't handle live video, I am building a lightweight relay server (Node.js/Python hosted on Render/Heroku/AWS).
* **WebSocket Pipeline:** The ESP32 will open a WebSocket connection to the cloud server and push JPEG frames as binary data. My client app will connect to that same server to pull the frames in real-time.

### Phase 6: The Control Hub (Client App)

*A unified dashboard to control CHUZA from both my laptop and phone.*

* **Progressive Web App (PWA):** Instead of building native apps twice, I am developing a responsive web dashboard.
* **Direct Integration:** The PWA connects directly to the MQTT broker via WebSockets to send joystick commands and notes.
* **Video Canvas:** A dedicated canvas element will render the incoming JPEG frames pushed from the cloud relay server for a seamless FPV driving experience.

---

## ⚙️ System Optimization: FreeRTOS

To prevent bottlenecks between heavy networking (camera streaming) and precise hardware timing (I2C, touch, motors), I am utilizing **FreeRTOS tasks**.

* **Core 0:** Pinned to handle the `esp_camera` streaming and WiFi tasks.
* **Core 1:** Dedicated to motor control, sensor polling, and OLED updates.

*This separation ensures that a delayed video frame over the WAN won't cause CHUZA to miss a cliff edge and fall off the desk.*