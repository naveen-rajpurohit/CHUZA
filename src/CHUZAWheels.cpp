#include "CHUZAWheels.h"

CHUZAWheels::CHUZAWheels(uint8_t lfPin, uint8_t lbPin, uint8_t rfPin, uint8_t rbPin) {
    _lfPin = lfPin;
    _lbPin = lbPin;
    _rfPin = rfPin;
    _rbPin = rbPin;
}

void CHUZAWheels::begin() {
    // 1. Setup the PWM channels (Channels 0, 1, 2, and 3)
    ledcSetup(0, _pwmFreq, _pwmRes);
    ledcSetup(1, _pwmFreq, _pwmRes);
    ledcSetup(2, _pwmFreq, _pwmRes);
    ledcSetup(3, _pwmFreq, _pwmRes);

    // 2. Attach your custom hardware pins to those channels
    ledcAttachPin(_lfPin, 0); // Left Fwd  = Channel 0
    ledcAttachPin(_lbPin, 1); // Left Rev  = Channel 1
    ledcAttachPin(_rfPin, 2); // Right Fwd = Channel 2
    ledcAttachPin(_rbPin, 3); // Right Rev = Channel 3
    
    stop();
}

void CHUZAWheels::setLeftMotor(int speed) {
    speed = constrain(speed, -255, 255);
    if (speed > 0) {
        ledcWrite(0, speed); // Channel 0 (LF)
        ledcWrite(1, 0);     // Channel 1 (LB)
    } else if (speed < 0) {
        ledcWrite(0, 0);
        ledcWrite(1, -speed);
    } else {
        // BRAKE
        ledcWrite(0, 255);
        ledcWrite(1, 255);
    }
}

void CHUZAWheels::setRightMotor(int speed) {
    speed = constrain(speed, -255, 255);
    if (speed > 0) {
        ledcWrite(2, speed); // Channel 2 (RF)
        ledcWrite(3, 0);     // Channel 3 (RB)
    } else if (speed < 0) {
        ledcWrite(2, 0);
        ledcWrite(3, -speed);
    } else {
        // BRAKE
        ledcWrite(2, 255);
        ledcWrite(3, 255);
    }
}

void CHUZAWheels::stop() {
    setLeftMotor(0);
    setRightMotor(0);
}

void CHUZAWheels::moveBurst(int leftSpeed, int rightSpeed, int durationMs) {
    setLeftMotor(leftSpeed);
    setRightMotor(rightSpeed);
    delay(durationMs);
    stop();
}