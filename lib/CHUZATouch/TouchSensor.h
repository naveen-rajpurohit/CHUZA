#pragma once
#include <Arduino.h>

// Wraps the ESP32-S3's built-in capacitive touch peripheral on one pin,
// turning noisy raw readings into: a debounced press/release state, how
// long the current touch has been held, how long it's been since the
// last touch ended, and single/double/triple(+) tap counting.
//
// ESP32-S3 touch polarity is the OPPOSITE of the original ESP32: raw
// readings go UP when touched, not down (see touchRead()'s doc comment
// in esp32-hal-touch.h). Threshold is calibrated once in begin() off the
// untouched baseline, so absolute readings - which vary a lot by board,
// wiring, and finger - don't matter.
class TouchSensor {
public:
    explicit TouchSensor(uint8_t pin);

    // Call once in setup(). Samples the untouched baseline reading -
    // keep your finger off the pad while this runs.
    void begin();

    // Call frequently (e.g. every 20-50ms via the scheduler). Samples
    // the pad and runs the press/release + tap-counting state machine.
    void update();

    // Live, continuous state.
    bool isTouched() const;
    unsigned long getHeldDurationMs() const;       // 0 if not currently touched
    unsigned long getTimeSinceLastTouchMs() const; // ms since the last touch ended

    // Returns the size of a finalized tap group (1 = single, 2 =
    // double, 3 = triple-or-more) exactly once, the instant the tap
    // grouping window closes - 0 every other call. Reading it clears
    // it, so call this at most once per loop iteration.
    uint8_t consumeTapCount();

    // RobotSettings hook (threshold section). Clamped to [1.02, 3.0] -
    // outside that range the pad reads as either permanently touched or
    // never touched.
    void setSensitivityRatio(float ratio);

    // Diagnostics, for picking/checking _sensitivityRatio on real
    // hardware via Serial - not needed for normal operation.
    uint32_t getRawValue() const;
    uint32_t getBaseline() const;

private:
    uint8_t _pin;
    uint32_t _baseline = 0;
    uint32_t _lastRaw = 0;
    float _sensitivityRatio = 1.15f; // touched once raw exceeds baseline * this

    bool _touched = false;
    unsigned long _touchStartMs = 0;
    unsigned long _lastReleaseMs = 0;

    uint8_t _pendingTaps = 0;
    unsigned long _tapWindowDeadlineMs = 0;
    uint8_t _finalizedTaps = 0;
};
