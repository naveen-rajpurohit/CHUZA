#include <Arduino.h>
#include "CHUZAPins.h"
#include "Secrets.h"
#include "CHUZAWheels.h"
#include "EnvSensor.h"
#include "BatterySensor.h"
#include "DistanceSensor.h"
#include "TouchSensor.h"
#include "Buzzer.h"
#include "CHUZAFace.h"
#include "CHUZACamera.h"
#include "MqttLink.h"
#include "CHUZALocalLink.h"
#include "Scheduler.h"
#include "WanderMode.h"
#include "RobotSettings.h"

CHUZAWheels wheels(PIN_LF, PIN_LB, PIN_RF, PIN_RB);
EnvSensor envSensor(PIN_SDA, PIN_SCL);
BatterySensor battery(PIN_BATT_SENSE);
DistanceSensor distanceSensor(PIN_SDA, PIN_SCL);
TouchSensor touchSensor(PIN_TOUCH);
Buzzer buzzer(PIN_BUZZER);
WanderMode wander;
CHUZAFace face(PIN_SDA, PIN_SCL, touchSensor, buzzer, envSensor, battery, distanceSensor, wander);
RobotSettings settings(wheels, face, touchSensor, buzzer, wander);

CameraPins camPins = {
    CAM_PIN_PWDN, CAM_PIN_RESET, CAM_PIN_XCLK, CAM_PIN_SIOD, CAM_PIN_SIOC,
    CAM_PIN_Y9, CAM_PIN_Y8, CAM_PIN_Y7, CAM_PIN_Y6, CAM_PIN_Y5, CAM_PIN_Y4, CAM_PIN_Y3, CAM_PIN_Y2,
    CAM_PIN_VSYNC, CAM_PIN_HREF, CAM_PIN_PCLK
};
CHUZACamera camera(camPins);

MqttLink mqttLink(wheels, envSensor, camera, battery, distanceSensor, face, settings);
CHUZALocalLink localLink(wheels, camera);
Scheduler scheduler;

void updateMotors() {
    wheels.update();
}

void updateEnvSensor() {
    if (settings.envSensorEnabled) envSensor.update();
}

void updateBattery() {
    battery.update();
}

void updateDistance() {
    if (!settings.distSensorEnabled) {
        wheels.setCliffBlocked(false); // no sensor feeding it - don't leave a stale block in place
        return;
    }
    distanceSensor.update();
    wheels.setCliffBlocked(distanceSensor.getDistanceMm() > settings.cliffThresholdMm);
}

void updateFace() {
    face.update();
}

void updateBuzzer() {
    buzzer.update();
}

void updateWander() {
    wander.update(wheels);
}

void setup() {
    Serial.begin(115200);

    randomSeed(esp_random()); // so WanderMode's bursts aren't the same sequence every boot

    wheels.begin();

    if (!envSensor.begin()) {
        Serial.println("BME280 not found - check wiring/I2C address");
    }

    if (!distanceSensor.begin()) {
        Serial.println("VL53L0X not found - check wiring/I2C address");
    }

    battery.begin();
    touchSensor.begin();
    buzzer.begin();

    if (!face.begin()) {
        Serial.println("OLED not found - check wiring/I2C address");
    }

    if (!camera.begin()) {
        Serial.println("Camera init failed - check the Sense board/ribbon cable");
    }

    // Loads last-saved-as-default values from NVS (or the hardcoded
    // defaults on first boot) and pushes them into wheels/face/touch/
    // buzzer/wander - must run after all of those begin() calls above.
    settings.begin();

    delay(2000);

    mqttLink.begin(WIFI_SSID, WIFI_PASSWORD,
                   MQTT_HOST, MQTT_PORT,
                   MQTT_USERNAME, MQTT_PASSWORD,
                   MQTT_CLIENT_ID);

    // WiFi should be up by now (mqttLink.begin() blocks for it above) -
    // configTime() queues an NTP sync in the background; CHUZAFace's
    // clock menu page reads the result via getLocalTime().
    configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER);

    // Runs on its own core (0): commands, telemetry, and camera frames
    // all happen there, fully independent of the scheduler below - see
    // the class comment on MqttLink for why this is a single task.
    mqttLink.startNetworkTask();

    // LAN-direct fallback: the app auto-switches to this (UDP commands +
    // local MJPEG stream) whenever it's on the same network as the
    // robot, for far lower latency and higher fps than the cloud path.
    localLink.begin();

    scheduler.addTask(updateMotors, 10);     // motor ramp tick, every 10ms
    scheduler.addTask(updateEnvSensor, 50);  // BME280 sample tick, every 50ms
    scheduler.addTask(updateBattery, 500);   // battery voltage tick, every 500ms
    scheduler.addTask(updateDistance, 100);  // VL53L0X ranging tick, every 100ms
    scheduler.addTask(updateFace, 33);       // touch + eyes/menu tick, ~30fps
    scheduler.addTask(updateBuzzer, 5);      // buzzer note/gap timing tick, tight for clean beeps
    scheduler.addTask(updateWander, 10);     // wander dice-roll + burst timing tick, same cadence as the motor ramp
}

void loop() {
    scheduler.run();
}