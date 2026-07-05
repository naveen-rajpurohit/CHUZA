#pragma once
#include <ArduinoJson.h>
#include <string.h>
#include "CHUZAWheels.h"
#include "CHUZACamera.h"

// Parses and executes one JSON robot command
// ({"cmd":"move"/"stop"/"brake"/"cam_on"/"cam_off", ...}). Shared by
// both the cloud (MQTT, MqttLink) and LAN-direct (UDP, CHUZALocalLink)
// command paths so there's exactly one place that knows the wire
// format - whichever transport a client is currently using, the robot
// reacts the same way.
inline void dispatchRobotCommand(CHUZAWheels &wheels, CHUZACamera &cam,
                                  const uint8_t *payload, unsigned int length) {
    if (length == 0 || length > 200) return;

    char buf[201];
    memcpy(buf, payload, length);
    buf[length] = '\0';

    StaticJsonDocument<200> doc;
    if (deserializeJson(doc, buf)) return;

    const char *cmd = doc["cmd"] | "";

    if (strcmp(cmd, "move") == 0) {
        wheels.setLeftMotor(doc["left"] | 0);
        wheels.setRightMotor(doc["right"] | 0);
    } else if (strcmp(cmd, "stop") == 0) {
        wheels.stop();
    } else if (strcmp(cmd, "brake") == 0) {
        wheels.brake();
    } else if (strcmp(cmd, "cam_on") == 0) {
        cam.enable();
    } else if (strcmp(cmd, "cam_off") == 0) {
        cam.disable();
    }
}
