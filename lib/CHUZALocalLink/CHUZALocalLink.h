#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "CHUZAWheels.h"
#include "CHUZACamera.h"

// LAN-direct control + video path. Always listening once begin() is
// called; whether a client actually USES it is entirely up to the
// client (CHUZAControls' app probes for reachability and switches to
// this path automatically when it's on the same network as the robot,
// falling back to MqttLink otherwise - the device side doesn't need to
// know which mode the app is in).
//
//   - UDP command listener (port 4210): same JSON schema as MQTT's
//     robot/commands, dispatched through the same dispatchRobotCommand()
//     helper - no broker round-trip, so this is the fast path.
//   - HTTP MJPEG stream (GET /stream) on port 81, and GET /ping (a cheap
//     reachability probe clients use to detect "am I on the same LAN as
//     the robot") on port 80 - deliberately TWO separate httpd server
//     instances, not two URIs on one. esp_http_server runs all of a
//     given instance's handlers on a single worker task; /stream's
//     handler never returns while a client is attached, so sharing one
//     instance meant a stuck/slow stream connection could block /ping
//     from answering for seconds (found this the hard way - matches the
//     two-server split Espressif's own camera examples use).
//
// CHUZACamera::isLocalStreamActive() reflects whether /stream currently
// has a connected client, so MqttLink can skip publishing the same
// frames to the cloud while someone's already getting them locally.
class CHUZALocalLink {
public:
    CHUZALocalLink(CHUZAWheels &wheels, CHUZACamera &cam);

    // Call once from setup(), after WiFi is connected.
    void begin();

private:
    static void udpTaskTrampoline(void *param);
    void udpTaskLoop();

    static esp_err_t streamHandler(httpd_req_t *req);
    static esp_err_t pingHandler(httpd_req_t *req);

    CHUZAWheels &_wheels;
    CHUZACamera &_cam;

    WiFiUDP _udp;
    TaskHandle_t _udpTaskHandle = nullptr;
    httpd_handle_t _pingServer = nullptr;
    httpd_handle_t _streamServer = nullptr;

    static CHUZALocalLink *_instance;
};
