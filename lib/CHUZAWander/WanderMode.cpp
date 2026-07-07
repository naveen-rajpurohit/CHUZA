#include "WanderMode.h"

namespace {
    // How long after the last manual drive command wander stays paused.
    const unsigned long MANUAL_GUARD_MS = 1500;

    // Dice-roll cadence + odds this was tuned from: about one SOFT
    // episode every ~4.8s, one NORMAL episode every ~4s, on average.
    const unsigned long DICE_INTERVAL_MS = 40;
    const int SOFT_DICE_ODDS = 120;
    const int NORMAL_DICE_ODDS = 100;

    // Direction index -> (left%, right%) at full power: 0=stop,
    // 1=backward, 2=forward, 3/4=pivot in place, 5-8=single-wheel arcs.
    const int8_t LEFT_PCT[9]  = { 0, -100,  100, -100,  100, -100,    0,  100,    0 };
    const int8_t RIGHT_PCT[9] = { 0, -100,  100,  100, -100,    0, -100,    0,  100 };
}

void WanderMode::setMode(WanderModeSetting mode) {
    _mode = mode;
}

WanderModeSetting WanderMode::getMode() const {
    return _mode;
}

void WanderMode::startEpisode() {
    uint8_t dir = random(9);
    _leftPct = LEFT_PCT[dir];
    _rightPct = RIGHT_PCT[dir];

    if (_mode == WANDER_SOFT) {
        _driveMs = random(6, 19);  // 6-18ms
        _gapMs = random(40, 91);   // 40-90ms
        _cyclesLeft = 1;
    } else { // WANDER_NORMAL
        _driveMs = random(5, 51);  // 5-50ms
        _gapMs = random(10, 101);  // 10-100ms
        _cyclesLeft = random(20);  // 0-19 - a 0 is a quiet "episode", same odds as the original
    }

    _state = (_cyclesLeft == 0) ? State::IDLE : State::DRIVING;
    _stateEndMs = millis() + _driveMs;
}

void WanderMode::update(CHUZAWheels &wheels) {
    unsigned long now = millis();

    if (_mode == WANDER_OFF || wheels.msSinceManualCommand() < MANUAL_GUARD_MS) {
        if (_state != State::IDLE) {
            wheels.setInstant(0, 0);
            _state = State::IDLE;
        }
        return;
    }

    if (_state == State::IDLE) {
        if (now - _lastDiceMs < DICE_INTERVAL_MS) return;
        _lastDiceMs = now;

        int odds = (_mode == WANDER_SOFT) ? SOFT_DICE_ODDS : NORMAL_DICE_ODDS;
        if (random(odds) == 1) {
            startEpisode();
            if (_state == State::DRIVING) {
                wheels.setInstant(_leftPct, _rightPct);
            }
        }
        return;
    }

    if ((long)(now - _stateEndMs) < 0) return; // still mid-phase

    if (_state == State::DRIVING) {
        wheels.setInstant(0, 0); // coast, same as the original's LOW/LOW/LOW/LOW gap
        _cyclesLeft--;
        if (_cyclesLeft == 0) {
            _state = State::IDLE;
        } else {
            _state = State::GAP;
            _stateEndMs = now + _gapMs;
        }
    } else { // GAP finished -> next cycle, same direction/timing as the episode's first
        _state = State::DRIVING;
        _stateEndMs = now + _driveMs;
        wheels.setInstant(_leftPct, _rightPct);
    }
}
