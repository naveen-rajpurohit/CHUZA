#pragma once
#include <Arduino.h>

class CHUZAWheels {
public:
    CHUZAWheels(uint8_t lfPin, uint8_t lbPin, uint8_t rfPin, uint8_t rbPin);

    void begin();
    
    // Core movement functions now accept percentages (-100 to 100)
    void setLeftMotor(int speedPct);
    void setRightMotor(int speedPct);
    void stop();
    
    // duration in ms, speeds in percentage
    void moveBurst(int leftSpeedPct, int rightSpeedPct, int durationMs);

private:
    uint8_t _lfPin, _lbPin, _rfPin, _rbPin;
    
    const int _pwmFreq = 20000;
    const int _pwmRes = 8;
    
    // --- Hardware Calibration Limits ---
    // The lowest PWM value that breaks stiction under the robot's own weight
    const int _minPwm = 180; 
    const int _maxPwm = 255;
};