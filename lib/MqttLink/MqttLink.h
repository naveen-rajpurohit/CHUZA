#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "CHUZAWheels.h"
#include "EnvSensor.h"

// Connects to WiFi + a TLS MQTT broker (HiveMQ Cloud), subscribes to
// robot/commands to drive the wheels, and publishes robot/telemetry +
// a retained robot/status (online/offline via Last Will) presence flag.
class MqttLink {
public:
    MqttLink(CHUZAWheels &wheels, EnvSensor &env);

    // Call once in setup(). Blocks for a few seconds while WiFi and
    // the broker connection come up.
    void begin(const char* wifiSsid, const char* wifiPass,
               const char* mqttHost, uint16_t mqttPort,
               const char* mqttUser, const char* mqttPass,
               const char* clientId);

    // Call on every scheduler tick (e.g. every 20ms). Handles incoming
    // messages and automatic reconnects. Reconnect attempts (only when
    // disconnected) can briefly block for a second or two — this does
    // not happen while a connection is healthy.
    void update();

    // Call periodically (e.g. every 2000ms) to publish the latest
    // EnvSensor reading to robot/telemetry.
    void publishTelemetry();

    bool isConnected();

private:
    static void staticCallback(char* topic, byte* payload, unsigned int length);
    void handleCommand(char* topic, byte* payload, unsigned int length);
    void reconnect();

    CHUZAWheels &_wheels;
    EnvSensor &_env;

    WiFiClientSecure _wifiClient;
    PubSubClient _mqtt;

    const char* _wifiSsid  = nullptr;
    const char* _wifiPass  = nullptr;
    const char* _mqttHost  = nullptr;
    uint16_t    _mqttPort  = 8883;
    const char* _mqttUser  = nullptr;
    const char* _mqttPass  = nullptr;
    const char* _clientId  = nullptr;

    unsigned long _lastReconnectAttempt = 0;

    // PubSubClient's callback is a plain function pointer, not a member
    // function, so we bounce through a static instance pointer.
    static MqttLink* _instance;
};