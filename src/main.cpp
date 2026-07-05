#include <Arduino.h>
#include "CHUZAPins.h"
#include "Secrets.h"
#include "CHUZAWheels.h"
#include "EnvSensor.h"
#include "CHUZACamera.h"
#include "MqttLink.h"
#include "CHUZALocalLink.h"
#include "Scheduler.h"

CHUZAWheels wheels(PIN_LF, PIN_LB, PIN_RF, PIN_RB);
EnvSensor envSensor(PIN_SDA, PIN_SCL);

CameraPins camPins = {
    CAM_PIN_PWDN, CAM_PIN_RESET, CAM_PIN_XCLK, CAM_PIN_SIOD, CAM_PIN_SIOC,
    CAM_PIN_Y9, CAM_PIN_Y8, CAM_PIN_Y7, CAM_PIN_Y6, CAM_PIN_Y5, CAM_PIN_Y4, CAM_PIN_Y3, CAM_PIN_Y2,
    CAM_PIN_VSYNC, CAM_PIN_HREF, CAM_PIN_PCLK
};
CHUZACamera camera(camPins);

MqttLink mqttLink(wheels, envSensor, camera);
CHUZALocalLink localLink(wheels, camera);
Scheduler scheduler;

void updateMotors() {
    wheels.update();
}

void updateEnvSensor() {
    envSensor.update();
}

void setup() {
    Serial.begin(115200);

    wheels.begin();
    wheels.setRampRate(250); // percent/sec — tune this once, here

    if (!envSensor.begin()) {
        Serial.println("BME280 not found - check wiring/I2C address");
    }

    if (!camera.begin()) {
        Serial.println("Camera init failed - check the Sense board/ribbon cable");
    }

    delay(2000);

    mqttLink.begin(WIFI_SSID, WIFI_PASSWORD,
                   MQTT_HOST, MQTT_PORT,
                   MQTT_USERNAME, MQTT_PASSWORD,
                   MQTT_CLIENT_ID);

    // Runs on its own core (0): commands, telemetry, and camera frames
    // all happen there, fully independent of the scheduler below - see
    // the class comment on MqttLink for why this is a single task.
    mqttLink.startNetworkTask();

    // LAN-direct fallback: the app auto-switches to this (UDP commands +
    // local MJPEG stream) whenever it's on the same network as the
    // robot, for far lower latency and higher fps than the cloud path.
    localLink.begin();

    scheduler.addTask(updateMotors, 10);    // motor ramp tick, every 10ms
    scheduler.addTask(updateEnvSensor, 50); // BME280 sample tick, every 50ms
}

void loop() {
    scheduler.run();
}