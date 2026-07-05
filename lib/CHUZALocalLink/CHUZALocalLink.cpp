#include "CHUZALocalLink.h"
#include "CHUZACommand.h"

CHUZALocalLink *CHUZALocalLink::_instance = nullptr;

static const uint16_t UDP_PORT = 4210;
static const char *STREAM_BOUNDARY = "\r\n--frame\r\n";
static const char *STREAM_PART_HEADER =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

CHUZALocalLink::CHUZALocalLink(CHUZAWheels &wheels, CHUZACamera &cam)
    : _wheels(wheels), _cam(cam) {
    _instance = this;
}

void CHUZALocalLink::begin() {
    _udp.begin(UDP_PORT);
    xTaskCreatePinnedToCore(udpTaskTrampoline, "chuzaUdp", 4096, this, 2, &_udpTaskHandle, 0);

    // Two separate httpd instances (see the class comment in the header
    // for why): /ping on 80 must always answer instantly regardless of
    // what /stream is doing, since it's the reachability probe the app
    // uses to decide whether to trust the LAN path at all.
    httpd_config_t pingConfig = HTTPD_DEFAULT_CONFIG();
    pingConfig.server_port = 80;
    pingConfig.ctrl_port = 32768;
    if (httpd_start(&_pingServer, &pingConfig) == ESP_OK) {
        httpd_uri_t pingUri = {"/ping", HTTP_GET, pingHandler, nullptr};
        httpd_register_uri_handler(_pingServer, &pingUri);
    } else {
        Serial.println("CHUZALocalLink: failed to start ping server");
    }

    httpd_config_t streamConfig = HTTPD_DEFAULT_CONFIG();
    streamConfig.server_port = 81;
    streamConfig.ctrl_port = 32769;
    // Defense in depth alongside the streamHandler heartbeat below: if a
    // connection ever does get stuck anyway, let a new request evict the
    // least-recently-used one rather than this instance wedging shut for
    // every future viewer too.
    streamConfig.lru_purge_enable = true;
    if (httpd_start(&_streamServer, &streamConfig) == ESP_OK) {
        httpd_uri_t streamUri = {"/stream", HTTP_GET, streamHandler, nullptr};
        httpd_register_uri_handler(_streamServer, &streamUri);
    } else {
        Serial.println("CHUZALocalLink: failed to start stream server");
    }
}

void CHUZALocalLink::udpTaskTrampoline(void *param) {
    static_cast<CHUZALocalLink *>(param)->udpTaskLoop();
}

void CHUZALocalLink::udpTaskLoop() {
    uint8_t buf[256];
    for (;;) {
        int packetSize = _udp.parsePacket();
        if (packetSize > 0) {
            int len = _udp.read(buf, sizeof(buf) - 1);
            if (len > 0) {
                dispatchRobotCommand(_wheels, _cam, buf, (unsigned int)len);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

esp_err_t CHUZALocalLink::pingHandler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "ok", 2);
}

esp_err_t CHUZALocalLink::streamHandler(httpd_req_t *req) {
    CHUZALocalLink *self = _instance;
    esp_err_t res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    if (res != ESP_OK) return res;

    self->_cam.setLocalStreamActive(true);
    char partBuf[64];
    unsigned long lastSendMs = millis();

    while (true) {
        if (!self->_cam.isEnabled()) {
            // Connection stays open, idle, until cam_on - matches the
            // same manual on/off semantics as the cloud path instead of
            // auto-enabling just because someone opened the stream URL.
            // Still send a periodic heartbeat boundary marker though:
            // without ever writing anything, a client that disconnects
            // while the camera happens to be off would never be
            // detected, permanently pinning one of the httpd server's
            // few concurrent connection slots (learned this the hard
            // way - an idle disconnected client silently exhausted the
            // slot pool and made /ping itself stop responding).
            if (millis() - lastSendMs >= 1000) {
                res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
                if (res != ESP_OK) break; // client gone while we were idle
                lastSendMs = millis();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        const uint8_t *buf;
        size_t len;
        void *handle;
        if (!self->_cam.captureJpeg(&buf, &len, &handle)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res == ESP_OK) {
            int hlen = snprintf(partBuf, sizeof(partBuf), STREAM_PART_HEADER, (unsigned)len);
            res = httpd_resp_send_chunk(req, partBuf, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)buf, len);
        }
        self->_cam.releaseJpeg(handle);
        lastSendMs = millis();

        if (res != ESP_OK) break; // client disconnected
    }

    self->_cam.setLocalStreamActive(false);
    return res;
}
