#include "CHUZAWheels.h"

CHUZAWheels::CHUZAWheels(uint8_t lfPin, uint8_t lbPin, uint8_t rfPin, uint8_t rbPin) {
    _lfPin = lfPin;
    _lbPin = lbPin;
    _rfPin = rfPin;
    _rbPin = rbPin;
}

void CHUZAWheels::begin() {
    ledcSetup(0, _pwmFreq, _pwmRes);
    ledcSetup(1, _pwmFreq, _pwmRes);
    ledcSetup(2, _pwmFreq, _pwmRes);
    ledcSetup(3, _pwmFreq, _pwmRes);

    ledcAttachPin(_lfPin, 0); 
    ledcAttachPin(_lbPin, 1); 
    ledcAttachPin(_rfPin, 2); 
    ledcAttachPin(_rbPin, 3); 
    
    stop();
}

void CHUZAWheels::setLeftMotor(int speedPct) {
    // Clamp the percentage between -100% and 100%
    speedPct = constrain(speedPct, -100, 100);
    
    if (speedPct == 0) {
        // 0% means BRAKE
        ledcWrite(0, 255);
        ledcWrite(1, 255);
    } else if (speedPct > 0) {
        // Map 1%->100% to _minPwm->255
        int pwm = map(speedPct, 1, 100, _minPwm, _maxPwm);
        ledcWrite(0, pwm);
        ledcWrite(1, 0);
    } else {
        // Map reverse percentages safely
        int pwm = map(-speedPct, 1, 100, _minPwm, _maxPwm);
        ledcWrite(0, 0);
        ledcWrite(1, pwm);
    }
}

void CHUZAWheels::setRightMotor(int speedPct) {
    speedPct = constrain(speedPct, -100, 100);
    
    if (speedPct == 0) {
        // 0% means BRAKE
        ledcWrite(2, 255);
        ledcWrite(3, 255);
    } else if (speedPct > 0) {
        int pwm = map(speedPct, 1, 100, _minPwm, _maxPwm);
        ledcWrite(2, pwm);
        ledcWrite(3, 0);
    } else {
        int pwm = map(-speedPct, 1, 100, _minPwm, _maxPwm);
        ledcWrite(2, 0);
        ledcWrite(3, pwm);
    }
}

void CHUZAWheels::stop() {
    setLeftMotor(0);
    setRightMotor(0);
}

void CHUZAWheels::moveBurst(int leftSpeedPct, int rightSpeedPct, int durationMs) {
    setLeftMotor(leftSpeedPct);
    setRightMotor(rightSpeedPct);
    delay(durationMs);
    stop();
}