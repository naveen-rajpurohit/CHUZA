#pragma once
#include <Arduino.h>

class CHUZAWheels {
public:
    CHUZAWheels(uint8_t lfPin, uint8_t lbPin, uint8_t rfPin, uint8_t rbPin);

    void begin();

    // Call once (e.g. in setup()) to configure how fast the real output
    // speed is allowed to change, in percentage-points per second.
    // e.g. 300 means going 0% -> 100% takes about 1/3 of a second.
    void setRampRate(float percentPerSecond);

    // Sets the TARGET speed as a percentage (-100 to 100).
    // This does NOT move the motor instantly anymore — call update() on
    // every scheduler tick and the real output speed will ramp toward
    // this target at the configured rate.
    void setLeftMotor(int speedPct);
    void setRightMotor(int speedPct);

    // Call this on every scheduler tick (non-blocking). Nudges the
    // current output speed toward the target speed, respecting the
    // configured ramp rate, and finishes off any moveBurst() whose
    // timer has elapsed.
    void update();

    // Gradual stop: sets the target speed to 0. The wheels ease down to
    // a stop over the next several update() calls, at the ramp rate.
    void stop();

    // Emergency stop: instantly zeroes target + current speed and
    // hard-brakes both motors right now. Bypasses the ramp completely.
    void brake();

    // Non-blocking timed burst: sets target speeds now, and auto-calls
    // stop() once durationMs has elapsed (checked from inside update()).
    // Requires update() to keep being called — it does NOT delay().
    void moveBurst(int leftSpeedPct, int rightSpeedPct, int durationMs);

private:
    void applyMotorOutput(uint8_t fwdChannel, uint8_t revChannel, int speedPct);

    uint8_t _lfPin, _lbPin, _rfPin, _rbPin;

    const int _pwmFreq = 20000;
    const int _pwmRes = 8;

    // --- Hardware Calibration Limits ---
    // The lowest PWM value that breaks stiction under the robot's own weight
    const int _minPwm = 180;
    const int _maxPwm = 255;

    // --- Ramping state ---
    float _rampRatePctPerSec = 300.0f; // default; override with setRampRate()
    float _leftCurrent = 0.0f;
    float _rightCurrent = 0.0f;
    int   _leftTarget = 0;
    int   _rightTarget = 0;
    unsigned long _lastUpdateMs = 0;

    // --- Non-blocking moveBurst state ---
    bool _burstActive = false;
    unsigned long _burstEndMs = 0;
};