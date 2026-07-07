#pragma once
#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "TouchSensor.h"
#include "EnvSensor.h"
#include "BatterySensor.h"
#include "DistanceSensor.h"
#include "Buzzer.h"
#include "WanderMode.h"

// Forward-declared only: the concrete RoboEyes<Adafruit_SSD1306> type -
// and RoboEyes.h's very generic macros (DEFAULT/ON/OFF/N/S/E/W/etc,
// several of which collide with Arduino.h's own DEFAULT) - live entirely
// in CHUZAFace.cpp so they never leak into any file that includes this
// header. Same trick CHUZACamera.h uses to keep esp_camera.h confined to
// its own .cpp.
struct RoboEyesHandle;

// Drives a 0.96" I2C SSD1306 OLED as CHUZA's face: RoboEyes-style
// animated eyes whose mood reacts to petting (touch) and battery level,
// plus a triple-tap-activated menu mode that swaps the eyes out for a
// clock/telemetry/stopwatch/timer readout, paginated by further single
// taps, with a buzzer chiming in on touches and mood/menu/stopwatch/
// timer events.
class CHUZAFace {
public:
    CHUZAFace(uint8_t sdaPin, uint8_t sclPin, TouchSensor &touch, Buzzer &buzzer,
              EnvSensor &env, BatterySensor &batt, DistanceSensor &dist, WanderMode &wander);
    ~CHUZAFace();

    // Call once in setup(), after Wire has a chance to come up (order
    // vs. the other I2C sensors doesn't matter - this re-begins the bus
    // defensively, same as they do). Returns false if no display
    // responded (check wiring/address).
    bool begin();

    // Call every scheduler tick (e.g. every 30-50ms). Reads the touch
    // sensor, updates the mood/menu/stopwatch/timer state machines, and
    // redraws the display (RoboEyes internally throttles its own frame
    // rate, so calling this faster than that is harmless). The timer's
    // countdown and alarm are tracked here regardless of whether the
    // menu is even open - see STOPWATCH_PAGE/TIMER_PAGE in the .cpp.
    void update();

    // --- RobotSettings hooks (see lib/CHUZASettings) ---
    // All timeouts take milliseconds despite RobotSettings storing them
    // in seconds - the conversion happens once, at the call site.
    void setAngryTimeoutMs(unsigned long ms);
    void setTiredHoldMs(unsigned long ms);
    void setMenuTimeoutMs(unsigned long ms);
    void setTimerAlarmDurationMs(unsigned long ms);
    void setLowBatteryPct(uint8_t pct);

    // Physically turns the OLED panel on/off (SSD1306_DISPLAYON/OFF).
    // While off, update() returns immediately - no touch/mood/menu
    // processing either, since there's nothing to see it react.
    void setDisplayEnabled(bool enabled);

    // Remote equivalent of double-tap-to-arm + single-tap-to-start on
    // TIMER_PAGE - used by the "set_timer" command. Clamped to the same
    // 1-60 minute range as the physical menu.
    void startTimer(uint8_t minutes);

private:
    void updateMood();
    void updateTimer();
    void drawMenu();

    uint8_t _sdaPin, _sclPin;
    TouchSensor &_touch;
    Buzzer &_buzzer;
    EnvSensor &_env;
    BatterySensor &_batt;
    DistanceSensor &_dist;
    WanderMode &_wander;

    Adafruit_SSD1306 _display;
    RoboEyesHandle *_eyes = nullptr;
    bool _displayFound = false;
    bool _displayEnabled = true;

    // RobotSettings-configurable timing/threshold - defaults match what
    // used to be hardcoded static consts here.
    unsigned long _angryTimeoutMs = 2UL * 60UL * 1000UL; // 2 minutes
    unsigned long _tiredHoldMs = 10UL * 1000UL;
    unsigned long _menuTimeoutMs = 10UL * 1000UL;
    unsigned long _timerAlarmDurationMs = 10UL * 1000UL;
    uint8_t _lowBatteryPct = 20;

    bool _menuActive = false;
    uint8_t _menuPage = 0;
    unsigned long _menuLastInputMs = 0;

    bool _wasTouched = false; // edge-detects touch-down for the tap chirp
    uint8_t _lastMood = 0;    // last mood sent to RoboEyes (0 = DEFAULT), for jingle-on-change

    // Stopwatch, lives on STOPWATCH_PAGE - double tap there starts it,
    // taking over that page's single taps as a run/pause toggle instead
    // of page navigation until it's double-tapped again to reset.
    bool _stopwatchActive = false;
    bool _stopwatchRunning = false;
    unsigned long _stopwatchElapsedMs = 0; // accumulated time while not running
    unsigned long _stopwatchStartMs = 0;   // millis() this run started, if running

    // Countdown timer, lives on TIMER_PAGE. Unlike the stopwatch, this
    // runs in the background: it keeps counting (and the menu's normal
    // 10s idle auto-exit stays in effect) even after you navigate away
    // or the menu closes, and its alarm fires regardless of what's on
    // screen - only silencing it early requires being back on the page.
    bool _timerSubMenu = false;   // armed/adjusting duration, not yet started
    bool _timerRunning = false;
    bool _timerAlarming = false;
    uint8_t _timerDurationMin = 1;       // configured length, 1-60, wraps
    unsigned long _timerEndMs = 0;       // millis() deadline, while running
    unsigned long _timerAlarmEndMs = 0;  // millis() the 10s alarm auto-silences
    unsigned long _lastAlarmBeepMs = 0;  // paces the repeating alarm beep

    // Wander-mode picker, lives on MODE_PAGE - double tap there opens a
    // 3-item cursor (OFF/SOFT/NORMAL); single tap moves the cursor,
    // double tap again commits it as the active WanderMode and closes
    // the submenu.
    bool _wanderSubMenu = false;
    uint8_t _wanderCursor = 0;
};
