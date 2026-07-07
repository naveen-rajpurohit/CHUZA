#pragma once
#include <ArduinoJson.h>
#include <string.h>
#include "CHUZAWheels.h"
#include "CHUZACamera.h"
#include "CHUZAFace.h"
#include "RobotSettings.h"

// Parses and executes one JSON robot command ({"cmd":"move"/"stop"/
// "brake"/"cam_on"/"cam_off"/"set_timer"/"get_settings"/"set_settings",
// ...}). Shared by both the cloud (MQTT, MqttLink) and LAN-direct (UDP,
// CHUZALocalLink) command paths so there's exactly one place that knows
// the wire format - whichever transport a client is currently using,
// the robot reacts the same way for movement/camera commands.
//
// face/settings are optional (default nullptr) - CHUZALocalLink's UDP
// path only ever passes wheels+cam. Settings/timer commands are
// low-frequency and the app keeps its MQTT connection up regardless of
// LAN/cloud mode, so there's no need to plumb them through the
// latency-sensitive UDP path too.
//
// Returns true when the command may have changed RobotSettings (i.e.
// "get_settings" or "set_settings") - the caller should then re-publish
// a fresh settings snapshot. Always false when settings is nullptr.
inline bool dispatchRobotCommand(CHUZAWheels &wheels, CHUZACamera &cam,
                                  const uint8_t *payload, unsigned int length,
                                  CHUZAFace *face = nullptr, RobotSettings *settings = nullptr) {
    static const unsigned int MAX_CMD_LEN = 1024; // settings payloads carry ~16 fields, well past the old 200-byte cap

    if (length == 0 || length > MAX_CMD_LEN) return false;

    char buf[MAX_CMD_LEN + 1];
    memcpy(buf, payload, length);
    buf[length] = '\0';

    StaticJsonDocument<MAX_CMD_LEN> doc;
    if (deserializeJson(doc, buf)) return false;

    const char *cmd = doc["cmd"] | "";

    if (strcmp(cmd, "move") == 0) {
        wheels.noteManualCommand();
        wheels.setLeftMotor(doc["left"] | 0);
        wheels.setRightMotor(doc["right"] | 0);
    } else if (strcmp(cmd, "stop") == 0) {
        wheels.noteManualCommand();
        wheels.stop();
    } else if (strcmp(cmd, "brake") == 0) {
        wheels.noteManualCommand();
        wheels.brake();
    } else if (strcmp(cmd, "cam_on") == 0) {
        cam.enable();
    } else if (strcmp(cmd, "cam_off") == 0) {
        cam.disable();
    } else if (strcmp(cmd, "set_timer") == 0) {
        if (face) face->startTimer((uint8_t)(doc["minutes"] | 1));
    } else if (strcmp(cmd, "get_settings") == 0) {
        return settings != nullptr;
    } else if (strcmp(cmd, "set_settings") == 0) {
        if (!settings) return false;
        bool persist = doc["persist"] | false;
        JsonObjectConst fields = doc["settings"];
        return settings->applyFromJson(fields, persist);
    }

    return false;
}
