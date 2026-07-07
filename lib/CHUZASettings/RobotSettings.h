#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "CHUZAWheels.h"
#include "CHUZAFace.h"
#include "TouchSensor.h"
#include "Buzzer.h"
#include "WanderMode.h"

// Every user-configurable value on the robot, in one place - the
// firmware side of the app's Settings screen. Boots by loading whatever
// was last saved to NVS (falling back to the hardcoded defaults below
// the very first time), and pushes values out to the hardware modules
// referenced in the constructor (this class stays the single source of
// truth; the modules themselves don't know settings exist).
//
// The app's "Save for current session" button is just applyFromJson()
// with persist=false: it changes live behavior but is forgotten on
// next boot. "Save to Default" is persist=true: same live effect, plus
// written to NVS as the new boot default. Every field behaves the same
// way here - there's no separate "boot-only" vs "live" value.
//
// envSensorEnabled/distSensorEnabled aren't pushed anywhere by this
// class - EnvSensor/DistanceSensor have no enable/disable concept of
// their own, so main.cpp's scheduler wrappers read those two flags
// directly instead. cliffThresholdMm is similarly read straight from
// here by main.cpp's updateDistance().
class RobotSettings {
public:
    RobotSettings(CHUZAWheels &wheels, CHUZAFace &face, TouchSensor &touch,
                  Buzzer &buzzer, WanderMode &wander);

    // --- Timing (seconds) ---
    uint16_t angryTimeoutSec = 120;
    uint16_t tiredHoldSec = 10;
    uint16_t menuTimeoutSec = 10;
    uint16_t timerAlarmSec = 10;

    // --- Threshold ---
    uint8_t lowBattPct = 20;
    uint16_t cliffThresholdMm = 50;
    float touchSensitivity = 1.15f;
    uint8_t wheelMinPwm = 180;
    uint8_t wheelMaxPwm = 255;
    uint16_t wheelRampRate = 250; // percent/sec
    int8_t wheelTrimPct = 0; // -50..50; +right slower, -left slower - corrects L/R speed mismatch

    // --- Hardware on/off ---
    bool motorsEnabled = true;
    bool oledEnabled = true;
    bool buzzerEnabled = true;
    bool envSensorEnabled = true;
    bool distSensorEnabled = true;

    // --- Behavior ---
    uint8_t wanderMode = 0; // WanderModeSetting value

    // Call once in setup(), after every module passed to the
    // constructor has already had its own begin() called. Loads the
    // last-saved-as-default values from NVS (or keeps the hardcoded
    // defaults above) and immediately pushes them out to hardware.
    void begin();

    // Merges whichever of the fields above are present in obj
    // (clamping anything safety- or hardware-relevant), pushes the
    // result out to hardware, and - if persist is true - writes it to
    // NVS as the new boot default. Returns true if anything actually
    // changed or was persisted, so the caller knows whether to
    // re-publish a fresh snapshot.
    bool applyFromJson(JsonObjectConst obj, bool persist);

    // Serializes every field above into doc, for publishing a full
    // settings snapshot back to the app.
    void toJson(JsonDocument &doc) const;

private:
    bool mergeFields(JsonObjectConst obj);
    void applyToHardware();
    void loadFromNvs();
    void saveAsDefault();

    CHUZAWheels &_wheels;
    CHUZAFace &_face;
    TouchSensor &_touch;
    Buzzer &_buzzer;
    WanderMode &_wander;
    Preferences _prefs;
};
