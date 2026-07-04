#include "CHUZAWheels.h"

namespace {
    // Moves `current` toward `target` by at most `maxStep`.
    float approach(float current, float target, float maxStep) {
        if (current < target) {
            current += maxStep;
            if (current > target) current = target;
        } else if (current > target) {
            current -= maxStep;
            if (current < target) current = target;
        }
        return current;
    }
}

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

    _lastUpdateMs = millis();
    brake(); // deterministic, fully-stopped state right after boot
}

void CHUZAWheels::setRampRate(float percentPerSecond) {
    _rampRatePctPerSec = percentPerSecond;
}

void CHUZAWheels::setLeftMotor(int speedPct) {
    _leftTarget = constrain(speedPct, -100, 100);
}

void CHUZAWheels::setRightMotor(int speedPct) {
    _rightTarget = constrain(speedPct, -100, 100);
}

void CHUZAWheels::applyMotorOutput(uint8_t fwdChannel, uint8_t revChannel, int speedPct) {
    speedPct = constrain(speedPct, -100, 100);

    if (speedPct == 0) {
        // Coast: a target of 0 means "not driven", not "actively braked".
        // Call brake() if you want the instant hard-stop behavior.
        ledcWrite(fwdChannel, 0);
        ledcWrite(revChannel, 0);
    } else if (speedPct > 0) {
        int pwm = map(speedPct, 1, 100, _minPwm, _maxPwm);
        ledcWrite(fwdChannel, pwm);
        ledcWrite(revChannel, 0);
    } else {
        int pwm = map(-speedPct, 1, 100, _minPwm, _maxPwm);
        ledcWrite(fwdChannel, 0);
        ledcWrite(revChannel, pwm);
    }
}

void CHUZAWheels::update() {
    unsigned long now = millis();
    float dtSec = (now - _lastUpdateMs) / 1000.0f;
    _lastUpdateMs = now;

    // Auto-finish a timed burst
    if (_burstActive && (long)(now - _burstEndMs) >= 0) {
        _burstActive = false;
        stop();
    }

    float maxStep = _rampRatePctPerSec * dtSec;

    _leftCurrent  = approach(_leftCurrent,  (float)_leftTarget,  maxStep);
    _rightCurrent = approach(_rightCurrent, (float)_rightTarget, maxStep);

    applyMotorOutput(0, 1, (int)round(_leftCurrent));
    applyMotorOutput(2, 3, (int)round(_rightCurrent));
}

void CHUZAWheels::stop() {
    _leftTarget = 0;
    _rightTarget = 0;
    _burstActive = false;
    // No hardware writes here on purpose — update() eases the current
    // speed down to 0 at the configured ramp rate.
}

void CHUZAWheels::brake() {
    _leftTarget = 0;
    _rightTarget = 0;
    _leftCurrent = 0;
    _rightCurrent = 0;
    _burstActive = false;

    // Hard stop, right now, no ramp.
    ledcWrite(0, 255);
    ledcWrite(1, 255);
    ledcWrite(2, 255);
    ledcWrite(3, 255);
}

void CHUZAWheels::moveBurst(int leftSpeedPct, int rightSpeedPct, int durationMs) {
    setLeftMotor(leftSpeedPct);
    setRightMotor(rightSpeedPct);
    _burstActive = true;
    _burstEndMs = millis() + (unsigned long)durationMs;
    // Non-blocking — no delay() here. Keep calling update() from your
    // scheduler and the wheels will auto-stop when the timer elapses.
}