#pragma once
#include <Arduino.h>

// Pin wiring for the OV2640, supplied by the caller (see CHUZAPins.h) —
// matches this codebase's convention of centralizing pins in main.cpp
// rather than having each lib reach for the pin-map header itself.
struct CameraPins {
    int pwdn, reset, xclk, siod, sioc;
    int y9, y8, y7, y6, y5, y4, y3, y2;
    int vsync, href, pclk;
};

// Wraps the onboard OV2640 (XIAO ESP32S3 Sense). Starts disabled — call
// enable()/disable() (normally from an MQTT or LAN command) to turn
// frame capture on or off, since running the sensor continuously costs
// both CPU and, once frames are being sent somewhere, network
// bandwidth. Also owns the chip-thermal-safety check, since that's a
// camera-usage-triggered concern shared by whichever transport
// (MqttLink, CHUZALocalLink) happens to be capturing frames.
//
// Deliberately doesn't expose esp_camera.h / camera_fb_t here: that
// header's sensor_t typedef collides with Adafruit_Sensor.h's sensor_t
// if both end up in the same translation unit (e.g. main.cpp, which
// also needs EnvSensor.h). Keeping esp_camera.h confined to
// CHUZACamera.cpp avoids the clash - outHandle below is an opaque
// camera_fb_t* for the same reason.
class CHUZACamera {
public:
    explicit CHUZACamera(const CameraPins &pins);

    // Call once in setup(). Initializes the sensor but leaves streaming
    // disabled. Returns false if the camera didn't respond (check the
    // Sense board is seated / camera ribbon is connected).
    bool begin();

    void enable();
    void disable();
    bool isEnabled() const;

    // Grabs one JPEG frame into *outBuf/*outLen, handing back an opaque
    // *outHandle that identifies it. Returns false (leaving all three
    // untouched) if disabled, not initialized, or the sensor failed to
    // deliver a frame this call. Every true result MUST be followed by
    // releaseJpeg(handle) once you're done reading the buffer.
    //
    // Safe to call from more than one task concurrently (e.g. the cloud
    // media task and the LAN stream handler, mid-handoff between them)
    // - each caller gets its own handle back rather than sharing one
    // piece of internal state, so there's nothing to race on here. The
    // underlying esp32-camera driver's frame buffer pool is already
    // safe for concurrent get/return across tasks.
    bool captureJpeg(const uint8_t **outBuf, size_t *outLen, void **outHandle);
    void releaseJpeg(void *handle);

    // Frames actually captured per second, averaged over the last
    // second. Reads 0 while disabled. Updated from whichever task(s)
    // are calling captureJpeg(); cosmetic torn-read risk only if two
    // are active at once, not worth a lock.
    float getFps() const;

    // Call periodically (e.g. every 1s) from any one task. Reads the
    // ESP32-S3's onboard temperature sensor and auto-disables the
    // camera if it's running hot - capturing/encoding is the main
    // camera-related CPU load, so cutting it is the direct lever here.
    void checkThermalSafety();
    float getChipTempC() const;
    bool isOverheatShutoff() const;

    // Set by CHUZALocalLink while a client is actively reading the LAN
    // MJPEG stream, so MqttLink can skip redundant (and much more
    // expensive) cloud publishing of the same frames.
    void setLocalStreamActive(bool active);
    bool isLocalStreamActive() const;

private:
    CameraPins _pins;
    bool _initialized = false;
    bool _enabled = false;

    unsigned long _fpsWindowStartMs = 0;
    int _framesThisWindow = 0;
    float _fps = 0.0f;

    volatile float _chipTempC = 0.0f;
    volatile bool _overheatShutoff = false;
    volatile bool _localStreamActive = false;
};
