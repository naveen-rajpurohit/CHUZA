#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "CHUZAWheels.h"
#include "EnvSensor.h"
#include "CHUZACamera.h"

// Connects to WiFi + a TLS MQTT broker (HiveMQ Cloud) using TWO
// independent MQTT connections (own WiFiClientSecure + PubSubClient +
// FreeRTOS task each, both on core 0):
//
//   - the "cmd" connection: subscribes to robot/commands, does nothing
//     else. Its task loop is a tight, never-blocked-by-anything-else
//     _mqtt.loop() call, so wheel commands get processed within
//     milliseconds no matter what the other connection is doing.
//   - the "media" connection: publishes robot/telemetry and
//     robot/camera/frame. Free to block for hundreds of ms on a slow
//     publish() (see below) without that ever delaying command
//     processing, because it shares no state with the cmd connection.
//
// Why two connections instead of one task/one connection: a single
// PubSubClient's internal buffer is used for both sending and
// receiving, so two tasks touching one instance need a lock around
// every access. An earlier version tried exactly that (mutex-protected
// single connection) and it failed in practice - real camera-frame
// publishes over a slow uplink can take 100-600ms+ each, and while
// commands ARE drained before each publish, that's still only one
// checkpoint per (slow) iteration, so a steady-state backlog of ~1s
// built up under sustained WASD input. Two fully independent
// connections remove the shared resource entirely, so there's nothing
// left to contend over.
//
// This is the cloud fallback path. When a client is on the same LAN as
// the robot, CHUZALocalLink handles commands/video instead (lower
// latency, much higher achievable fps, no cloud bandwidth) - the media
// task here checks CHUZACamera::isLocalStreamActive() and skips its own
// camera publish while that's true, so the cloud path isn't wastefully
// duplicating the same video.
class MqttLink {
public:
    MqttLink(CHUZAWheels &wheels, EnvSensor &env, CHUZACamera &cam);

    // Call once in setup(). Blocks for a few seconds while WiFi and the
    // cmd connection come up (the media connection connects lazily from
    // its own task).
    void begin(const char* wifiSsid, const char* wifiPass,
               const char* mqttHost, uint16_t mqttPort,
               const char* mqttUser, const char* mqttPass,
               const char* clientId);

    // Call once from setup(), after begin(). Spins up both tasks - see
    // class comment.
    void startNetworkTask();

    // Reflects the cmd connection - the one that matters for "can I
    // currently drive the robot." Best-effort read from another core.
    bool isConnected();

private:
    static void cmdCallback(char* topic, byte* payload, unsigned int length);
    void handleCommand(char* topic, byte* payload, unsigned int length);
    void reconnectCmd();
    void reconnectMedia();
    void publishTelemetry();

    static void cmdTaskTrampoline(void* param);
    void cmdTaskLoop();
    static void mediaTaskTrampoline(void* param);
    void mediaTaskLoop();

    CHUZAWheels &_wheels;
    EnvSensor &_env;
    CHUZACamera &_cam;

    WiFiClientSecure _cmdWifiClient;
    PubSubClient _cmdMqtt;
    WiFiClientSecure _mediaWifiClient;
    PubSubClient _mediaMqtt;

    const char* _wifiSsid  = nullptr;
    const char* _wifiPass  = nullptr;
    const char* _mqttHost  = nullptr;
    uint16_t    _mqttPort  = 8883;
    const char* _mqttUser  = nullptr;
    const char* _mqttPass  = nullptr;
    const char* _clientId  = nullptr;

    unsigned long _lastCmdReconnectAttempt = 0;
    unsigned long _lastMediaReconnectAttempt = 0;
    TaskHandle_t _cmdTaskHandle = nullptr;
    TaskHandle_t _mediaTaskHandle = nullptr;

    // PubSubClient's callback is a plain function pointer, not a member
    // function, so we bounce through a static instance pointer.
    static MqttLink* _instance;
};
