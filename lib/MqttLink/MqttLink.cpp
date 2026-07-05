#include "MqttLink.h"
#include <ArduinoJson.h>
#include <string.h>

MqttLink* MqttLink::_instance = nullptr;

static const char* TOPIC_COMMANDS  = "robot/commands";
static const char* TOPIC_TELEMETRY = "robot/telemetry";
static const char* TOPIC_STATUS    = "robot/status";

MqttLink::MqttLink(CHUZAWheels &wheels, EnvSensor &env)
    : _wheels(wheels), _env(env), _mqtt(_wifiClient) {
    _instance = this;
}

void MqttLink::begin(const char* wifiSsid, const char* wifiPass,
                      const char* mqttHost, uint16_t mqttPort,
                      const char* mqttUser, const char* mqttPass,
                      const char* clientId) {
    _wifiSsid = wifiSsid;
    _wifiPass = wifiPass;
    _mqttHost = mqttHost;
    _mqttPort = mqttPort;
    _mqttUser = mqttUser;
    _mqttPass = mqttPass;
    _clientId = clientId;

    WiFi.mode(WIFI_STA);
    WiFi.begin(_wifiSsid, _wifiPass);

    Serial.print("Connecting to WiFi");
    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 15000) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("WiFi connected, IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("WiFi connect failed - will keep retrying from update()");
    }

    // Hobby-project trade-off: skip TLS certificate verification so we
    // don't have to embed/maintain HiveMQ's root CA on the device.
    // The link is still encrypted, just not identity-verified against
    // a trusted CA. Fine for a personal robot; ask me if you want
    // proper cert pinning later.
    _wifiClient.setInsecure();

    _mqtt.setServer(_mqttHost, _mqttPort);
    _mqtt.setCallback(staticCallback);

    reconnect();
}

void MqttLink::reconnect() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (_mqtt.connected()) return;

    Serial.print("Connecting to MQTT broker...");
    // Last Will: if we drop off ungracefully, the broker publishes
    // "offline" (retained) on our behalf.
    bool ok = _mqtt.connect(_clientId, _mqttUser, _mqttPass,
                             TOPIC_STATUS, 1, true, "offline");
    if (ok) {
        Serial.println(" connected.");
        _mqtt.publish(TOPIC_STATUS, "online", true); // retained
        _mqtt.subscribe(TOPIC_COMMANDS);
    } else {
        Serial.print(" failed, rc=");
        Serial.println(_mqtt.state());
    }
}

void MqttLink::update() {
    if (WiFi.status() != WL_CONNECTED) return;

    if (!_mqtt.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > 5000) {
            _lastReconnectAttempt = now;
            reconnect();
        }
        return;
    }

    _mqtt.loop();
}

bool MqttLink::isConnected() {
    return _mqtt.connected();
}

void MqttLink::publishTelemetry() {
    if (!_mqtt.connected()) return;

    StaticJsonDocument<128> doc;
    doc["tempC"]       = _env.getTemperatureC();
    doc["humidity"]    = _env.getHumidityPct();
    doc["pressureHpa"] = _env.getPressureHpa();
    doc["altitudeM"]   = _env.getAltitudeM();

    char buf[128];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    _mqtt.publish(TOPIC_TELEMETRY, (const uint8_t*)buf, n);
}

void MqttLink::staticCallback(char* topic, byte* payload, unsigned int length) {
    if (_instance) _instance->handleCommand(topic, payload, length);
}

void MqttLink::handleCommand(char* topic, byte* payload, unsigned int length) {
    if (strcmp(topic, TOPIC_COMMANDS) != 0) return;
    if (length == 0 || length > 200) return;

    char buf[201];
    memcpy(buf, payload, length);
    buf[length] = '\0';

    StaticJsonDocument<200> doc;
    DeserializationError err = deserializeJson(doc, buf);
    if (err) {
        Serial.print("Bad command JSON: ");
        Serial.println(err.c_str());
        return;
    }

    const char* cmd = doc["cmd"] | "";

    if (strcmp(cmd, "move") == 0) {
        int left  = doc["left"]  | 0;
        int right = doc["right"] | 0;
        _wheels.setLeftMotor(left);
        _wheels.setRightMotor(right);
    } else if (strcmp(cmd, "stop") == 0) {
        _wheels.stop();
    } else if (strcmp(cmd, "brake") == 0) {
        _wheels.brake();
    }
}