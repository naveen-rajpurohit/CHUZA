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

    // Jumps straight to the requested speed, bypassing the ramp
    // entirely (both current AND target are set immediately). For
    // WanderMode's snappy full-power personality twitches, where a
    // move as short as 5-18ms would be mostly eaten by the ramp before
    // ever reaching speed.
    void setInstant(int leftSpeedPct, int rightSpeedPct);

    // Cliff safety: call from the distance-sensor tick with whether the
    // floor is currently missing from under the down-facing ToF sensor
    // (desk edge, stairs, etc). Rising edge triggers an immediate
    // brake(); while true, every command below silently clamps any
    // wheel's FORWARD component to 0 (reverse/pivot-away stays live) so
    // nothing can drive the robot further over the edge.
    void setCliffBlocked(bool blocked);
    bool isCliffBlocked() const;

    // Marks "a manual drive command just came in" - call this from
    // wherever manual commands are dispatched (see
    // dispatchRobotCommand() in CHUZACommand.h). WanderMode polls
    // msSinceManualCommand() so it never fights a live joystick session.
    void noteManualCommand();
    unsigned long msSinceManualCommand() const;

    // Overrides the stiction-breaking PWM floor / ceiling set below.
    // Clamped to [0,255] with min <= max.
    void setPwmRange(int minPwm, int maxPwm);

    // Corrects for one drive motor spinning faster than the other at
    // the same commanded speed. Signed percentage, clamped to
    // [-50, 50]: positive trims the RIGHT side down by that percent
    // (right was spinning faster), negative trims the LEFT side down
    // by that percent (left was spinning faster). Never boosts a side
    // past the caller's requested speed - only slows the faster one
    // down to match, since there's no PWM headroom to boost into.
    void setMotorTrim(int trimPct);

    // Hardware kill-switch (RobotSettings' "motorsEnabled"). Disabling
    // brakes immediately, then every speed-setting entry point below
    // clamps to 0 - in both directions, unlike the cliff clamp - until
    // re-enabled.
    void setEnabled(bool enabled);
    bool isEnabled() const;

private:
    void applyMotorOutput(uint8_t fwdChannel, uint8_t revChannel, int speedPct);
    int clampForSafety(int speedPct) const;
    void computeTrimmedSpeeds(int leftIn, int rightIn, int &leftOut, int &rightOut) const;

    uint8_t _lfPin, _lbPin, _rfPin, _rbPin;

    const int _pwmFreq = 20000;
    const int _pwmRes = 8;

    // --- Hardware Calibration Limits ---
    // The lowest PWM value that breaks stiction under the robot's own
    // weight - overridable via setPwmRange() (RobotSettings' threshold
    // section).
    int _minPwm = 180;
    int _maxPwm = 255;
    int _trimPct = 0;

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

    // --- Cliff safety + manual-command tracking ---
    bool _cliffBlocked = false;
    unsigned long _lastManualCmdMs = 0;

    // --- Hardware kill-switch ---
    bool _hwEnabled = true;
};