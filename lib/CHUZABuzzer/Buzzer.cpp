#include "Buzzer.h"

// The core's tone()/noTone() drive an LEDC channel behind the scenes,
// hardcoded to channel 0 unless told otherwise - and CHUZAWheels claims
// channels 0-3 for the four motor PWM outputs (see its ledcSetup calls).
// Without this, every beep silently reattached channel 0 from the
// left-forward motor pin to the buzzer pin (and never gave it back),
// which is what was making the left wheel twitch on every beep.
static const uint8_t TONE_LEDC_CHANNEL = 4;

Buzzer::Buzzer(uint8_t pin) : _pin(pin) {}

void Buzzer::begin() {
    pinMode(_pin, OUTPUT);
    setToneChannel(TONE_LEDC_CHANNEL);
    noTone(_pin);
}

void Buzzer::beep(unsigned int freqHz, unsigned int durationMs) {
    if (_queueCount >= QUEUE_CAPACITY) return; // dropped - queue backed up, shouldn't happen in practice

    uint8_t idx = (_queueHead + _queueCount) % QUEUE_CAPACITY;
    _queue[idx] = { (uint16_t)freqHz, (uint16_t)durationMs };
    _queueCount++;

    if (!_noteActive && !_inGap) startNextNote();
}

void Buzzer::playSequence(const uint16_t *freqsHz, const uint16_t *durationsMs, uint8_t count, uint16_t gapMs) {
    stop();
    _gapMs = gapMs;
    for (uint8_t i = 0; i < count; i++) {
        beep(freqsHz[i], durationsMs[i]);
    }
}

void Buzzer::stop() {
    noTone(_pin);
    _noteActive = false;
    _inGap = false;
    _queueHead = 0;
    _queueCount = 0;
}

bool Buzzer::isPlaying() const {
    return _noteActive || _inGap || _queueCount > 0;
}

void Buzzer::startNextNote() {
    if (_queueCount == 0) {
        _noteActive = false;
        _inGap = false;
        return;
    }

    Note n = _queue[_queueHead];
    _queueHead = (_queueHead + 1) % QUEUE_CAPACITY;
    _queueCount--;

    tone(_pin, n.freqHz, 0); // duration 0: update() manages the timing itself
    _noteActive = true;
    _inGap = false;
    _stepEndMs = millis() + n.durationMs;
}

void Buzzer::update() {
    unsigned long now = millis();

    if (_noteActive && now >= _stepEndMs) {
        noTone(_pin);
        _noteActive = false;
        if (_queueCount > 0 && _gapMs > 0) {
            _inGap = true;
            _stepEndMs = now + _gapMs;
        } else {
            startNextNote();
        }
    } else if (_inGap && now >= _stepEndMs) {
        _inGap = false;
        startNextNote();
    }
}
