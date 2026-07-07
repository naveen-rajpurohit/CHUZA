#include "MqttLink.h"
#include "CHUZACommand.h"
#include <ArduinoJson.h>
#include <string.h>

// Instrumentation added while tracking down the WASD-lag / low-fps
// reports on real hardware (see the class comment in MqttLink.h for
// what was actually wrong and how it was fixed). Flip to 1 any time you
// need to see per-frame capture/publish timing again, or want the ack
// round-trip mechanism for latency testing - it's all behind this flag
// and adds no overhead when disabled. The 1/sec STATS line is left on
// by default since heap/RSSI/fps/chip-temp is cheap and useful to have
// in the logs long-term.
#define CHUZA_DEBUG 0

MqttLink* MqttLink::_instance = nullptr;

static const char* TOPIC_COMMANDS      = "robot/commands";
static const char* TOPIC_COMMANDS_ACK  = "robot/commands/ack";
static const char* TOPIC_TELEMETRY     = "robot/telemetry";
static const char* TOPIC_STATUS        = "robot/status";
static const char* TOPIC_CAMERA_FRAME  = "robot/camera/frame";
static const char* TOPIC_SETTINGS      = "robot/settings";

// QVGA JPEGs land around 4-5KB. PubSubClient's default 256-byte buffer
// is only big enough for telemetry/commands, so bump it for the media
// connection. The cmd connection now also carries full settings
// snapshots/updates (~16 fields), bigger than the old small
// move/stop/brake commands, so it needs headroom past those too.
static const uint16_t MEDIA_BUFFER_SIZE = 20480;
static const uint16_t CMD_BUFFER_SIZE = 1536;

static const unsigned long TELEMETRY_INTERVAL_MS = 1500;
static const unsigned long THERMAL_CHECK_INTERVAL_MS = 1000;

MqttLink::MqttLink(CHUZAWheels &wheels, EnvSensor &env, CHUZACamera &cam, BatterySensor &batt, DistanceSensor &dist,
                    CHUZAFace &face, RobotSettings &settings)
    : _wheels(wheels), _env(env), _cam(cam), _batt(batt), _dist(dist), _face(face), _settings(settings),
      _cmdMqtt(_cmdWifiClient), _mediaMqtt(_mediaWifiClient) {
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
        // Modem sleep saves power by napping the radio between beacons,
        // but it's a well-known cause of multi-second delays on INCOMING
        // packets specifically. Disabling it trades a bit of power for
        // consistent latency, worth it for a robot you're driving live.
        WiFi.setSleep(false);
    } else {
        Serial.println("WiFi connect failed - will keep retrying from the network tasks");
    }

    // Hobby-project trade-off: skip TLS certificate verification so we
    // don't have to embed/maintain HiveMQ's root CA on the device. The
    // link is still encrypted, just not identity-verified against a
    // trusted CA. Fine for a personal robot; ask me if you want proper
    // cert pinning later.
    _cmdWifiClient.setInsecure();
    _mediaWifiClient.setInsecure();

    _cmdMqtt.setBufferSize(CMD_BUFFER_SIZE);
    _cmdMqtt.setServer(_mqttHost, _mqttPort);
    _cmdMqtt.setCallback(cmdCallback);

    _mediaMqtt.setBufferSize(MEDIA_BUFFER_SIZE);
    _mediaMqtt.setServer(_mqttHost, _mqttPort);

    reconnectCmd();
}

void MqttLink::reconnectCmd() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (_cmdMqtt.connected()) return;

    String cmdClientId = String(_clientId) + "-cmd";
    Serial.print("Connecting cmd MQTT connection...");
    // Last Will: if we drop off ungracefully, the broker publishes
    // "offline" (retained) on our behalf.
    bool ok = _cmdMqtt.connect(cmdClientId.c_str(), _mqttUser, _mqttPass,
                                TOPIC_STATUS, 1, true, "offline");
    if (ok) {
        Serial.println(" connected.");
        _cmdMqtt.publish(TOPIC_STATUS, "online", true); // retained
        _cmdMqtt.subscribe(TOPIC_COMMANDS);
        publishSettings(); // so a fresh connect/reconnect always shows the app current values
    } else {
        Serial.print(" failed, rc=");
        Serial.println(_cmdMqtt.state());
    }
}

void MqttLink::reconnectMedia() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (_mediaMqtt.connected()) return;

    String mediaClientId = String(_clientId) + "-media";
    Serial.print("Connecting media MQTT connection...");
    bool ok = _mediaMqtt.connect(mediaClientId.c_str(), _mqttUser, _mqttPass);
    if (ok) {
        Serial.println(" connected.");
    } else {
        Serial.print(" failed, rc=");
        Serial.println(_mediaMqtt.state());
    }
}

bool MqttLink::isConnected() {
    return _cmdMqtt.connected();
}

void MqttLink::publishTelemetry() {
    if (!_mediaMqtt.connected()) return;

    StaticJsonDocument<384> doc;
    doc["tempC"]       = _env.getTemperatureC();
    doc["humidity"]    = _env.getHumidityPct();
    doc["pressureHpa"] = _env.getPressureHpa();
    doc["altitudeM"]   = _env.getAltitudeM();
    doc["battV"]       = _batt.getVoltage();
    doc["battPct"]     = _batt.getPercent();
    doc["distanceMm"]  = _dist.getDistanceMm();
    doc["camOn"]       = _cam.isEnabled();
    doc["camFps"]      = _cam.getFps();
    doc["chipTempC"]   = _cam.getChipTempC();
    doc["camOverheat"] = _cam.isOverheatShutoff();
    // Lets the app probe this IP directly and switch to the LAN-direct
    // path (CHUZALocalLink) when it's on the same network as the robot.
    doc["ip"]          = WiFi.localIP().toString();

    char buf[384];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    _mediaMqtt.publish(TOPIC_TELEMETRY, (const uint8_t*)buf, n);
}

void MqttLink::publishSettings() {
    if (!_cmdMqtt.connected()) return;

    StaticJsonDocument<512> doc;
    _settings.toJson(doc);

    char buf[512];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    _cmdMqtt.publish(TOPIC_SETTINGS, (const uint8_t*)buf, n, true); // retained
}

void MqttLink::startNetworkTask() {
    // 4096 was enough back when the biggest payload here was a tiny
    // move/stop/brake JSON command - it panic'ed with a stack canary
    // overflow (Guru Meditation, "Stack canary watchpoint triggered
    // (chuzaCmd)") once settings commands landed: dispatchRobotCommand's
    // now-1KB JSON doc + char buf, on top of this task's own TLS
    // (WiFiClientSecure) record processing and the NVS Preferences
    // calls saveAsDefault() makes, all stack up in the same call chain.
    xTaskCreatePinnedToCore(cmdTaskTrampoline, "chuzaCmd", 16384, this, 2, &_cmdTaskHandle, 0);
    xTaskCreatePinnedToCore(mediaTaskTrampoline, "chuzaMedia", 6144, this, 1, &_mediaTaskHandle, 0);
}

void MqttLink::cmdTaskTrampoline(void* param) {
    static_cast<MqttLink*>(param)->cmdTaskLoop();
}

void MqttLink::cmdTaskLoop() {
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (!_cmdMqtt.connected()) {
            unsigned long now = millis();
            if (now - _lastCmdReconnectAttempt > 5000) {
                _lastCmdReconnectAttempt = now;
                reconnectCmd();
            }
        } else {
            // This task NEVER does anything else - no camera, no
            // telemetry - so this call is never stuck behind a slow
            // publish. Commands get processed within one short delay.
            _cmdMqtt.loop();
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void MqttLink::mediaTaskTrampoline(void* param) {
    static_cast<MqttLink*>(param)->mediaTaskLoop();
}

void MqttLink::mediaTaskLoop() {
    unsigned long lastTelemetryMs = 0;
    unsigned long lastThermalCheckMs = 0;
    unsigned long lastStatsMs = 0;
    unsigned long loopIterations = 0;

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (!_mediaMqtt.connected()) {
            unsigned long now = millis();
            if (now - _lastMediaReconnectAttempt > 5000) {
                _lastMediaReconnectAttempt = now;
                reconnectMedia();
            }
        } else {
            _mediaMqtt.loop(); // keepalive only - this connection doesn't subscribe to anything
        }

        unsigned long now = millis();

        if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
            lastTelemetryMs = now;
            publishTelemetry();
        }

        if (now - lastThermalCheckMs >= THERMAL_CHECK_INTERVAL_MS) {
            lastThermalCheckMs = now;
            _cam.checkThermalSafety();
        }

        // Skip cloud publishing entirely while a LAN client is already
        // getting frames via CHUZALocalLink - no point paying cloud
        // bandwidth (and the capture+publish time budget) for the same
        // video twice.
        if (_cam.isEnabled() && !_cam.isLocalStreamActive() && _mediaMqtt.connected()) {
#if CHUZA_DEBUG
            unsigned long capT0 = micros();
            const uint8_t* buf;
            size_t len;
            void* handle;
            bool gotFrame = _cam.captureJpeg(&buf, &len, &handle);
            unsigned long capDt = micros() - capT0;
            if (gotFrame) {
                unsigned long pubT0 = micros();
                bool ok = _mediaMqtt.publish(TOPIC_CAMERA_FRAME, buf, len);
                unsigned long pubDt = micros() - pubT0;
                _cam.releaseJpeg(handle);
                Serial.printf("[%lu] CAM cap=%lums pub=%lums len=%u ok=%d\n",
                              millis(), capDt / 1000, pubDt / 1000, (unsigned)len, ok);
            }
#else
            const uint8_t* buf;
            size_t len;
            void* handle;
            if (_cam.captureJpeg(&buf, &len, &handle)) {
                _mediaMqtt.publish(TOPIC_CAMERA_FRAME, buf, len);
                _cam.releaseJpeg(handle);
            }
#endif
        }

        loopIterations++;
        if (now - lastStatsMs >= 1000) {
            Serial.printf("[%lu] STATS iter/s=%lu heap=%u rssi=%d camFps=%.1f chipTemp=%.1f\n",
                          now, loopIterations, ESP.getFreeHeap(), WiFi.RSSI(),
                          _cam.getFps(), _cam.getChipTempC());
            loopIterations = 0;
            lastStatsMs = now;
        }

        vTaskDelay(pdMS_TO_TICKS(1)); // yield; actual cadence is set by the work above
    }
}

void MqttLink::cmdCallback(char* topic, byte* payload, unsigned int length) {
    if (_instance) _instance->handleCommand(topic, payload, length);
}

void MqttLink::handleCommand(char* topic, byte* payload, unsigned int length) {
    if (strcmp(topic, TOPIC_COMMANDS) != 0) return;

#if CHUZA_DEBUG
    if (length > 0 && length <= 200) {
        char buf[201];
        memcpy(buf, payload, length);
        buf[length] = '\0';
        Serial.printf("[%lu] CMD recv: %s\n", millis(), buf);

        // If the sender tagged this command with a "seq" number, echo it
        // straight back on ack (same cmd connection - fast, tiny message).
        // Comparing send-time vs receive-time on the PC's single clock
        // (no device/PC clock sync needed) gives an exact round-trip
        // latency.
        StaticJsonDocument<200> doc;
        if (!deserializeJson(doc, buf) && doc.containsKey("seq")) {
            StaticJsonDocument<64> ack;
            ack["seq"] = doc["seq"];
            ack["ms"]  = millis();
            char ackBuf[64];
            size_t ackLen = serializeJson(ack, ackBuf, sizeof(ackBuf));
            _cmdMqtt.publish(TOPIC_COMMANDS_ACK, (const uint8_t*)ackBuf, ackLen);
        }
    }
#endif

    if (dispatchRobotCommand(_wheels, _cam, payload, length, &_face, &_settings)) {
        publishSettings();
    }
}
