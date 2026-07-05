#include "CHUZACamera.h"
#include <esp_camera.h>

// QVGA keeps JPEG frames small (roughly 5-15KB) so they stay cheap to
// publish over MQTT to a cloud broker. Bump this later if bandwidth
// allows, but re-check MqttLink's buffer size (setBufferSize) if you do.
static const framesize_t CAM_FRAME_SIZE = FRAMESIZE_QVGA;
static const int CAM_JPEG_QUALITY = 12; // 0-63, lower = higher quality/bigger frame

// The Sense board's OV2640 is mounted so the raw feed reads as a mirror
// image of what's actually in front of the lens. Flip these two if your
// unit turns out mirrored/upside-down the other way.
static const int CAM_HMIRROR = 1;
static const int CAM_VFLIP   = 0;

// ESP32-S3 is commercial-grade rated to ~85C ambient; the die runs
// hotter than ambient under load. Cutting camera work at 80C leaves
// some margin. Sending cam_on again always retries (and will just
// re-latch if still hot).
static const float CAM_OVERHEAT_C = 80.0f;

CHUZACamera::CHUZACamera(const CameraPins &pins) : _pins(pins) {}

bool CHUZACamera::begin() {
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;

    config.pin_d0 = _pins.y2;
    config.pin_d1 = _pins.y3;
    config.pin_d2 = _pins.y4;
    config.pin_d3 = _pins.y5;
    config.pin_d4 = _pins.y6;
    config.pin_d5 = _pins.y7;
    config.pin_d6 = _pins.y8;
    config.pin_d7 = _pins.y9;
    config.pin_xclk = _pins.xclk;
    config.pin_pclk = _pins.pclk;
    config.pin_vsync = _pins.vsync;
    config.pin_href = _pins.href;
    config.pin_sccb_sda = _pins.siod;
    config.pin_sccb_scl = _pins.sioc;
    config.pin_pwdn = _pins.pwdn;
    config.pin_reset = _pins.reset;

    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = CAM_FRAME_SIZE;
    config.jpeg_quality = CAM_JPEG_QUALITY;
    config.fb_count = psramFound() ? 2 : 1;
    config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    // Always hand back the newest frame rather than queuing stale ones —
    // matters once capture cadence and publish cadence can drift.
    config.grab_mode = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&config);
    _initialized = (err == ESP_OK);

    if (_initialized) {
        sensor_t *sensor = esp_camera_sensor_get();
        if (sensor) {
            sensor->set_hmirror(sensor, CAM_HMIRROR);
            sensor->set_vflip(sensor, CAM_VFLIP);
        }
    }

    return _initialized;
}

void CHUZACamera::enable() {
    if (!_initialized) return;
    _enabled = true;
    _overheatShutoff = false; // give it another shot; checkThermalSafety() re-latches this if still too hot
    _fpsWindowStartMs = millis();
    _framesThisWindow = 0;
    _fps = 0.0f;
}

void CHUZACamera::disable() {
    _enabled = false;
    _fps = 0.0f;
}

bool CHUZACamera::isEnabled() const {
    return _enabled;
}

bool CHUZACamera::captureJpeg(const uint8_t **outBuf, size_t *outLen, void **outHandle) {
    if (!_initialized || !_enabled) return false;

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return false;

    *outBuf = fb->buf;
    *outLen = fb->len;
    *outHandle = fb;

    _framesThisWindow++;
    unsigned long now = millis();
    unsigned long elapsed = now - _fpsWindowStartMs;
    if (elapsed >= 1000) {
        _fps = _framesThisWindow * 1000.0f / elapsed;
        _framesThisWindow = 0;
        _fpsWindowStartMs = now;
    }

    return true;
}

void CHUZACamera::releaseJpeg(void *handle) {
    if (!handle) return;
    esp_camera_fb_return(static_cast<camera_fb_t *>(handle));
}

float CHUZACamera::getFps() const {
    return _enabled ? _fps : 0.0f;
}

void CHUZACamera::checkThermalSafety() {
    _chipTempC = temperatureRead();
    if (_enabled && _chipTempC >= CAM_OVERHEAT_C) {
        _enabled = false;
        _overheatShutoff = true;
        Serial.println("Camera auto-disabled: chip over temperature threshold");
    }
}

float CHUZACamera::getChipTempC() const {
    return _chipTempC;
}

bool CHUZACamera::isOverheatShutoff() const {
    return _overheatShutoff;
}

void CHUZACamera::setLocalStreamActive(bool active) {
    _localStreamActive = active;
}

bool CHUZACamera::isLocalStreamActive() const {
    return _localStreamActive;
}
