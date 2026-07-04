#include <Arduino.h>
#include "CHUZAPins.h"
#include "CHUZAWheels.h"
#include "Scheduler.h"
#include "MotorTestCLI.h"

CHUZAWheels wheels(PIN_LF, PIN_LB, PIN_RF, PIN_RB);
Scheduler scheduler;

void updateMotors() {
    wheels.update();
}

void setup() {
    Serial.begin(115200);

    wheels.begin();
    wheels.setRampRate(200); // percent/sec — tune this once, here

    delay(2000);
    beginMotorTestCLI();     // print instructions to Serial Monitor

    scheduler.addTask(updateMotors, 10); // motor ramp tick, every 10ms
}

void loop() {
    scheduler.run();
    pollMotorTestCLI(wheels);
}