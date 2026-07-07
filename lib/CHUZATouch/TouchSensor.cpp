#include "TouchSensor.h"

// How many consecutive baseline samples to average at startup.
static const int BASELINE_SAMPLES = 8;

// Taps within this window of each other (release to next release)
// count as one group - what turns 2-3 quick touches into a "double" or
// "triple" tap instead of several separate singles.
static const unsigned long TAP_GROUP_WINDOW_MS = 400;

TouchSensor::TouchSensor(uint8_t pin) : _pin(pin) {}

void TouchSensor::begin() {
    uint32_t sum = 0;
    for (int i = 0; i < BASELINE_SAMPLES; i++) {
        sum += touchRead(_pin);
        delay(10);
    }
    _baseline = sum / BASELINE_SAMPLES;
    _lastReleaseMs = millis();
}

void TouchSensor::update() {
    _lastRaw = touchRead(_pin);
    bool rawTouched = _lastRaw > (uint32_t)((float)_baseline * _sensitivityRatio);
    unsigned long now = millis();

    if (rawTouched && !_touched) {
        _touched = true;
        _touchStartMs = now;
    } else if (!rawTouched && _touched) {
        _touched = false;
        _lastReleaseMs = now;
        _pendingTaps++;
        _tapWindowDeadlineMs = now + TAP_GROUP_WINDOW_MS; // extend the window on each new tap
    }

    // Finalize the pending tap group once it's been quiet for a whole
    // window - can't be mid-touch when this fires, since a touch in
    // progress keeps re-arming the deadline above.
    if (_pendingTaps > 0 && !_touched && now >= _tapWindowDeadlineMs) {
        _finalizedTaps = (_pendingTaps > 3) ? 3 : _pendingTaps;
        _pendingTaps = 0;
    }
}

bool TouchSensor::isTouched() const { return _touched; }

unsigned long TouchSensor::getHeldDurationMs() const {
    return _touched ? (millis() - _touchStartMs) : 0;
}

unsigned long TouchSensor::getTimeSinceLastTouchMs() const {
    return millis() - _lastReleaseMs;
}

uint8_t TouchSensor::consumeTapCount() {
    uint8_t taps = _finalizedTaps;
    _finalizedTaps = 0;
    return taps;
}

uint32_t TouchSensor::getRawValue() const { return _lastRaw; }
uint32_t TouchSensor::getBaseline() const { return _baseline; }

void TouchSensor::setSensitivityRatio(float ratio) {
    _sensitivityRatio = constrain(ratio, 1.02f, 3.0f);
}
