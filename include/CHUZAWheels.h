#pragma once
#include <Arduino.h>

class CHUZAWheels {
public:
    // Explicit parameter names prevent any mix-ups during instantiation
    CHUZAWheels(uint8_t lfPin, uint8_t lbPin, uint8_t rfPin, uint8_t rbPin);

    // Initialization
    void begin();

    // Core movement functions
    void setLeftMotor(int speed);
    void setRightMotor(int speed);
    void stop();

    // Autonomous movements
    void moveBurst(int leftSpeed, int rightSpeed, int durationMs);

private:
    uint8_t _lfPin, _lbPin, _rfPin, _rbPin;
    const int _pwmFreq = 20000;
    const int _pwmRes = 8;
};