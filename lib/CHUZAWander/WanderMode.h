#pragma once
#include <Arduino.h>
#include "CHUZAWheels.h"

// Three intensities of autonomous "personality" driving - OFF, SOFT
// (occasional small wiggles), NORMAL (more frequent, longer bursts).
// Values are also used directly as CHUZAFace's menu-cursor index, so
// keep them contiguous starting at 0.
enum WanderModeSetting : uint8_t {
    WANDER_OFF = 0,
    WANDER_SOFT,
    WANDER_NORMAL
};

// Idle-time full-speed twitches/turns, layered underneath manual
// driving. Purely a driving *policy*: CHUZAWheels still owns the actual
// motor output (including the cliff-safety clamp), and CHUZAFace only
// reads/writes the mode via setMode()/getMode() for its menu page.
//
// update() is non-blocking (no delay() anywhere) - call it every
// scheduler tick, same as CHUZAWheels::update(). It automatically backs
// off whenever a manual drive command came in recently (see
// CHUZAWheels::noteManualCommand()), so it never fights a live
// joystick session.
class WanderMode {
public:
    void setMode(WanderModeSetting mode);
    WanderModeSetting getMode() const;

    void update(CHUZAWheels &wheels);

private:
    void startEpisode();

    WanderModeSetting _mode = WANDER_OFF;
    unsigned long _lastDiceMs = 0; // paces the dice-roll cadence

    enum class State { IDLE, DRIVING, GAP };
    State _state = State::IDLE;

    int8_t _leftPct = 0;
    int8_t _rightPct = 0;
    uint16_t _driveMs = 0;
    uint16_t _gapMs = 0;
    uint8_t _cyclesLeft = 0;
    unsigned long _stateEndMs = 0;
};
