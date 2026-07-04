#include <Arduino.h>
#include "CHUZAPins.h"
#include "CHUZAWheels.h"
#include "EnvSensor.h"
#include "EnvSensorPrinter.h"
#include "Scheduler.h"
#include "MotorTestCLI.h"

CHUZAWheels wheels(PIN_LF, PIN_LB, PIN_RF, PIN_RB);
EnvSensor envSensor(PIN_SDA, PIN_SCL);
Scheduler scheduler;

void updateMotors() {
    wheels.update();
}

void updateEnvSensor() {
    envSensor.update();
}

void printEnv() {
    printEnvReading(envSensor);
}

void setup() {
    Serial.begin(115200);

    wheels.begin();
    wheels.setRampRate(200); // percent/sec — tune this once, here

    if (!envSensor.begin()) {
        Serial.println("BME280 not found - check wiring/I2C address");
    }

    delay(2000);
    beginMotorTestCLI();

    scheduler.addTask(updateMotors, 10);    // motor ramp tick, every 10ms
    scheduler.addTask(updateEnvSensor, 50); // BME280 sample tick, every 50ms
    scheduler.addTask(printEnv, 500);       // human-readable serial dump
}

void loop() {
    scheduler.run();
    pollMotorTestCLI(wheels);
}