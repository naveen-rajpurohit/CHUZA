#include <Arduino.h>
#include "CHUZAPins.h"
#include "Secrets.h"
#include "CHUZAWheels.h"
#include "EnvSensor.h"
#include "MqttLink.h"
#include "Scheduler.h"

CHUZAWheels wheels(PIN_LF, PIN_LB, PIN_RF, PIN_RB);
EnvSensor envSensor(PIN_SDA, PIN_SCL);
MqttLink mqttLink(wheels, envSensor);
Scheduler scheduler;

void updateMotors() {
    wheels.update();
}

void updateEnvSensor() {
    envSensor.update();
}

void updateMqtt() {
    mqttLink.update();
}

void publishTelemetryMqtt() {
    mqttLink.publishTelemetry();
}

void setup() {
    Serial.begin(115200);

    wheels.begin();
    wheels.setRampRate(250); // percent/sec — tune this once, here

    if (!envSensor.begin()) {
        Serial.println("BME280 not found - check wiring/I2C address");
    }

    delay(2000);

    mqttLink.begin(WIFI_SSID, WIFI_PASSWORD,
                   MQTT_HOST, MQTT_PORT,
                   MQTT_USERNAME, MQTT_PASSWORD,
                   MQTT_CLIENT_ID);

    scheduler.addTask(updateMotors, 10);        // motor ramp tick, every 10ms
    scheduler.addTask(updateEnvSensor, 50);     // BME280 sample tick, every 50ms
    scheduler.addTask(updateMqtt, 20);          // MQTT network loop + reconnects
    scheduler.addTask(publishTelemetryMqtt, 1500); // push telemetry to the cloud
}

void loop() {
    scheduler.run();
}