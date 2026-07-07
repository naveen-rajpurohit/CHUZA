#include "RobotSettings.h"

static const char *NVS_NAMESPACE = "chuza";

RobotSettings::RobotSettings(CHUZAWheels &wheels, CHUZAFace &face, TouchSensor &touch,
                              Buzzer &buzzer, WanderMode &wander)
    : _wheels(wheels), _face(face), _touch(touch), _buzzer(buzzer), _wander(wander) {}

void RobotSettings::begin() {
    loadFromNvs();
    applyToHardware();
}

void RobotSettings::loadFromNvs() {
    _prefs.begin(NVS_NAMESPACE, false);

    angryTimeoutSec  = _prefs.getUShort("angryToSec", angryTimeoutSec);
    tiredHoldSec     = _prefs.getUShort("tiredHoldSec", tiredHoldSec);
    menuTimeoutSec   = _prefs.getUShort("menuToSec", menuTimeoutSec);
    timerAlarmSec    = _prefs.getUShort("timerAlmSec", timerAlarmSec);

    lowBattPct       = _prefs.getUChar("lowBattPct", lowBattPct);
    cliffThresholdMm = _prefs.getUShort("cliffMm", cliffThresholdMm);
    touchSensitivity = _prefs.getFloat("touchSens", touchSensitivity);
    wheelMinPwm      = _prefs.getUChar("wheelMinPwm", wheelMinPwm);
    wheelMaxPwm      = _prefs.getUChar("wheelMaxPwm", wheelMaxPwm);
    wheelRampRate    = _prefs.getUShort("rampRate", wheelRampRate);

    motorsEnabled     = _prefs.getBool("motorsOn", motorsEnabled);
    oledEnabled       = _prefs.getBool("oledOn", oledEnabled);
    buzzerEnabled     = _prefs.getBool("buzzerOn", buzzerEnabled);
    envSensorEnabled  = _prefs.getBool("envOn", envSensorEnabled);
    distSensorEnabled = _prefs.getBool("distOn", distSensorEnabled);

    wanderMode = _prefs.getUChar("wanderMode", wanderMode);

    _prefs.end();
}

void RobotSettings::saveAsDefault() {
    _prefs.begin(NVS_NAMESPACE, false);

    _prefs.putUShort("angryToSec", angryTimeoutSec);
    _prefs.putUShort("tiredHoldSec", tiredHoldSec);
    _prefs.putUShort("menuToSec", menuTimeoutSec);
    _prefs.putUShort("timerAlmSec", timerAlarmSec);

    _prefs.putUChar("lowBattPct", lowBattPct);
    _prefs.putUShort("cliffMm", cliffThresholdMm);
    _prefs.putFloat("touchSens", touchSensitivity);
    _prefs.putUChar("wheelMinPwm", wheelMinPwm);
    _prefs.putUChar("wheelMaxPwm", wheelMaxPwm);
    _prefs.putUShort("rampRate", wheelRampRate);

    _prefs.putBool("motorsOn", motorsEnabled);
    _prefs.putBool("oledOn", oledEnabled);
    _prefs.putBool("buzzerOn", buzzerEnabled);
    _prefs.putBool("envOn", envSensorEnabled);
    _prefs.putBool("distOn", distSensorEnabled);

    _prefs.putUChar("wanderMode", wanderMode);

    _prefs.end();
}

void RobotSettings::applyToHardware() {
    _wheels.setRampRate((float)wheelRampRate);
    _wheels.setPwmRange(wheelMinPwm, wheelMaxPwm);
    _wheels.setEnabled(motorsEnabled);

    _face.setAngryTimeoutMs((unsigned long)angryTimeoutSec * 1000UL);
    _face.setTiredHoldMs((unsigned long)tiredHoldSec * 1000UL);
    _face.setMenuTimeoutMs((unsigned long)menuTimeoutSec * 1000UL);
    _face.setTimerAlarmDurationMs((unsigned long)timerAlarmSec * 1000UL);
    _face.setLowBatteryPct(lowBattPct);
    _face.setDisplayEnabled(oledEnabled);

    _touch.setSensitivityRatio(touchSensitivity);
    _buzzer.setEnabled(buzzerEnabled);

    _wander.setMode((WanderModeSetting)wanderMode);
}

bool RobotSettings::mergeFields(JsonObjectConst obj) {
    if (obj.isNull()) return false;
    bool changed = false;

    auto u16 = [&](const char *key, uint16_t &field) {
        if (obj.containsKey(key)) { field = obj[key].as<uint16_t>(); changed = true; }
    };
    auto u8 = [&](const char *key, uint8_t &field) {
        if (obj.containsKey(key)) { field = obj[key].as<uint8_t>(); changed = true; }
    };
    auto f32 = [&](const char *key, float &field) {
        if (obj.containsKey(key)) { field = obj[key].as<float>(); changed = true; }
    };
    auto b = [&](const char *key, bool &field) {
        if (obj.containsKey(key)) { field = obj[key].as<bool>(); changed = true; }
    };

    u16("angryTimeoutSec", angryTimeoutSec);
    u16("tiredHoldSec", tiredHoldSec);
    u16("menuTimeoutSec", menuTimeoutSec);
    u16("timerAlarmSec", timerAlarmSec);

    u8("lowBattPct", lowBattPct);
    u16("cliffThresholdMm", cliffThresholdMm);
    f32("touchSensitivity", touchSensitivity);
    u8("wheelMinPwm", wheelMinPwm);
    u8("wheelMaxPwm", wheelMaxPwm);
    u16("wheelRampRate", wheelRampRate);

    b("motorsEnabled", motorsEnabled);
    b("oledEnabled", oledEnabled);
    b("buzzerEnabled", buzzerEnabled);
    b("envSensorEnabled", envSensorEnabled);
    b("distSensorEnabled", distSensorEnabled);

    u8("wanderMode", wanderMode);

    if (!changed) return false;

    // Clamp anything safety- or hardware-relevant - the setters called
    // from applyToHardware() clamp too, but clamping here keeps what
    // gets persisted/reported back in toJson() consistent with what's
    // actually running.
    lowBattPct = constrain(lowBattPct, (uint8_t)0, (uint8_t)100);
    cliffThresholdMm = max((uint16_t)10, cliffThresholdMm);
    touchSensitivity = constrain(touchSensitivity, 1.02f, 3.0f);
    wheelMinPwm = constrain(wheelMinPwm, (uint8_t)0, (uint8_t)255);
    wheelMaxPwm = constrain(wheelMaxPwm, wheelMinPwm, (uint8_t)255);
    wheelRampRate = constrain(wheelRampRate, (uint16_t)10, (uint16_t)2000);
    wanderMode = constrain(wanderMode, (uint8_t)0, (uint8_t)2);

    return true;
}

bool RobotSettings::applyFromJson(JsonObjectConst obj, bool persist) {
    bool changed = mergeFields(obj);
    if (changed) applyToHardware();
    if (persist) saveAsDefault();
    return changed || persist;
}

void RobotSettings::toJson(JsonDocument &doc) const {
    doc["angryTimeoutSec"] = angryTimeoutSec;
    doc["tiredHoldSec"] = tiredHoldSec;
    doc["menuTimeoutSec"] = menuTimeoutSec;
    doc["timerAlarmSec"] = timerAlarmSec;

    doc["lowBattPct"] = lowBattPct;
    doc["cliffThresholdMm"] = cliffThresholdMm;
    doc["touchSensitivity"] = touchSensitivity;
    doc["wheelMinPwm"] = wheelMinPwm;
    doc["wheelMaxPwm"] = wheelMaxPwm;
    doc["wheelRampRate"] = wheelRampRate;

    doc["motorsEnabled"] = motorsEnabled;
    doc["oledEnabled"] = oledEnabled;
    doc["buzzerEnabled"] = buzzerEnabled;
    doc["envSensorEnabled"] = envSensorEnabled;
    doc["distSensorEnabled"] = distSensorEnabled;

    doc["wanderMode"] = wanderMode;
}
