#pragma once
#include <Arduino.h>

// Small non-blocking melody/beep sequencer for a piezo buzzer on one
// pin. Queue notes with beep()/playSequence(); update() (called every
// scheduler tick) advances playback off millis(), so nothing here ever
// delays the caller. Uses the core's tone()/noTone(), but manages note
// duration and gaps itself rather than tone()'s own duration argument,
// so back-to-back notes in a sequence get a clean, predictable gap
// between them instead of running together.
class Buzzer {
public:
    explicit Buzzer(uint8_t pin);

    void begin();

    // Call every few ms (short notes are ~30-150ms, so coarse scheduler
    // ticks like the 30fps face tick would make timing visibly uneven).
    void update();

    // Queues one note. Starts immediately if nothing else is playing.
    void beep(unsigned int freqHz, unsigned int durationMs);

    // Replaces whatever's queued/playing with a new sequence of notes -
    // used for the mood/menu/stopwatch/timer jingles.
    void playSequence(const uint16_t *freqsHz, const uint16_t *durationsMs, uint8_t count, uint16_t gapMs = 25);

    // Silences immediately and drops anything queued - e.g. tapping to
    // stop the timer alarm mid-beep.
    void stop();

    bool isPlaying() const;

private:
    static const uint8_t QUEUE_CAPACITY = 16;
    struct Note { uint16_t freqHz; uint16_t durationMs; };

    void startNextNote();

    uint8_t _pin;
    Note _queue[QUEUE_CAPACITY];
    uint8_t _queueHead = 0;
    uint8_t _queueCount = 0;
    uint16_t _gapMs = 25;

    bool _noteActive = false;
    bool _inGap = false;
    unsigned long _stepEndMs = 0;
};
