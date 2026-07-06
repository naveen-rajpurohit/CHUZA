#include "CHUZAFace.h"
// Arduino.h (pulled in via CHUZAFace.h) already defines DEFAULT=1 for
// analogReference(), which this project never calls - RoboEyes.h's own
// DEFAULT=0 (a mood constant) is what everything below actually means.
#undef DEFAULT
#include "RoboEyes.h"
#include <Wire.h>
#include <WiFi.h>
#include <time.h>

// See CHUZAFace.h for why RoboEyes.h (and its macros) are confined to
// this one file.
struct RoboEyesHandle {
    RoboEyes<Adafruit_SSD1306> eyes;
    explicit RoboEyesHandle(Adafruit_SSD1306 &display) : eyes(display) {}
};

static const uint8_t OLED_WIDTH = 128;
static const uint8_t OLED_HEIGHT = 64;
static const uint8_t OLED_I2C_ADDR = 0x3C;

// How long CHUZA can go without a pet before getting grumpy about it -
// tune to taste.
static const unsigned long ANGRY_TIMEOUT_MS = 2UL * 60UL * 1000UL; // 2 minutes
// Petting that goes on this long turns into a sleepy/tired reaction
// instead of staying happy, per spec.
static const unsigned long TIRED_HOLD_MS = 10UL * 1000UL;
static const uint8_t LOW_BATTERY_PCT = 20;

static const unsigned long MENU_TIMEOUT_MS = 10UL * 1000UL; // per spec
static const uint8_t MENU_PAGE_COUNT = 5;
static const uint8_t STOPWATCH_PAGE = 3; // "page 4" to the user (1-indexed)
static const uint8_t TIMER_PAGE = 4;     // "page 5" to the user (1-indexed)

static const uint8_t TIMER_MIN_MIN = 1;
static const uint8_t TIMER_MAX_MIN = 60;
static const unsigned long TIMER_ALARM_DURATION_MS = 10UL * 1000UL; // per spec
static const unsigned long TIMER_ALARM_BEEP_INTERVAL_MS = 400UL;

// Tiny convenience wrapper - Buzzer::playSequence() wants arrays even
// for a single note.
static void playNote(Buzzer &buzzer, uint16_t freqHz, uint16_t durationMs) {
    buzzer.playSequence(&freqHz, &durationMs, 1);
}

CHUZAFace::CHUZAFace(uint8_t sdaPin, uint8_t sclPin, TouchSensor &touch, Buzzer &buzzer,
                      EnvSensor &env, BatterySensor &batt, DistanceSensor &dist)
    : _sdaPin(sdaPin), _sclPin(sclPin), _touch(touch), _buzzer(buzzer), _env(env), _batt(batt), _dist(dist),
      _display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1) {}

CHUZAFace::~CHUZAFace() {
    delete _eyes;
}

bool CHUZAFace::begin() {
    Wire.begin(_sdaPin, _sclPin);

    _displayFound = _display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
    if (!_displayFound) {
        // Some clones answer on 0x3D instead.
        _displayFound = _display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
    }
    if (!_displayFound) return false;

    _eyes = new RoboEyesHandle(_display);
    _eyes->eyes.begin(OLED_WIDTH, OLED_HEIGHT, 30); // 30fps - about what the shared I2C bus can actually push anyway
    _eyes->eyes.setAutoblinker(true, 3, 2);
    _eyes->eyes.setIdleMode(true, 2, 3);

    _menuLastInputMs = millis();
    return true;
}

void CHUZAFace::updateMood() {
    uint8_t mood;
    if (_touch.isTouched()) {
        // Being petted - happy, unless it's gone on long enough to turn sleepy.
        mood = (_touch.getHeldDurationMs() > TIRED_HOLD_MS) ? TIRED : HAPPY;
    } else if (_batt.getPercent() < LOW_BATTERY_PCT) {
        mood = TIRED;
    } else if (_touch.getTimeSinceLastTouchMs() > ANGRY_TIMEOUT_MS) {
        mood = ANGRY;
    } else {
        mood = DEFAULT;
    }

    if (mood != _lastMood) {
        switch (mood) {
            case HAPPY: {
                static const uint16_t f[] = {1200, 1600};
                static const uint16_t d[] = {60, 80};
                _buzzer.playSequence(f, d, 2);
                break;
            }
            case TIRED: {
                static const uint16_t f[] = {900, 600};
                static const uint16_t d[] = {120, 180};
                _buzzer.playSequence(f, d, 2);
                break;
            }
            case ANGRY: {
                static const uint16_t f[] = {300, 250};
                static const uint16_t d[] = {100, 100};
                _buzzer.playSequence(f, d, 2);
                break;
            }
            default:
                playNote(_buzzer, 700, 60);
                break;
        }
        _lastMood = mood;
    }

    _eyes->eyes.setMood(mood);
}

void CHUZAFace::updateTimer() {
    unsigned long now = millis();

    if (_timerRunning && now >= _timerEndMs) {
        _timerRunning = false;
        _timerAlarming = true;
        _timerAlarmEndMs = now + TIMER_ALARM_DURATION_MS;
        _lastAlarmBeepMs = 0; // fires the first alarm beep immediately, below
    }

    if (_timerAlarming) {
        if (now - _lastAlarmBeepMs >= TIMER_ALARM_BEEP_INTERVAL_MS) {
            _lastAlarmBeepMs = now;
            playNote(_buzzer, 2500, 150);
        }
        if (now >= _timerAlarmEndMs) {
            _timerAlarming = false;
        }
    }
}

void CHUZAFace::update() {
    if (!_displayFound) return;

    _touch.update();
    uint8_t taps = _touch.consumeTapCount();

    bool touchedNow = _touch.isTouched();
    if (touchedNow && !_wasTouched) {
        _buzzer.beep(2200, 35); // cute chirp on every touch-down
    }
    _wasTouched = touchedNow;

    // Cheap and handy for tuning TouchSensor's sensitivity ratio on real
    // hardware - left on by default, same call as MqttLink's own 1/sec
    // STATS line.
    static unsigned long lastDebugMs = 0;
    unsigned long nowDebug = millis();
    if (nowDebug - lastDebugMs >= 2000) {
        lastDebugMs = nowDebug;
        Serial.printf("[Face] touchRaw=%lu baseline=%lu touched=%d heldMs=%lu sinceLastMs=%lu menu=%d page=%d taps=%u\n",
                      (unsigned long)_touch.getRawValue(), (unsigned long)_touch.getBaseline(),
                      _touch.isTouched(), _touch.getHeldDurationMs(), _touch.getTimeSinceLastTouchMs(),
                      _menuActive, _menuPage, taps);
    }

    // The countdown timer runs (and alarms) in the background regardless
    // of menu/page state - see the class comment on updateTimer().
    updateTimer();

    // Triple tap is reserved for entering/paging the menu - it never
    // feeds the mood engine below, so a quick triple-tap doesn't also
    // read as "petting" beyond the brief real-time happy flicker each
    // touch already gives while the finger's down.
    if (_timerAlarming) {
        // Any tap silences the alarm early, wherever you are - it never
        // reaches the menu/mood logic below while it's sounding.
        if (taps > 0) {
            _timerAlarming = false;
            playNote(_buzzer, 600, 50); // little "acknowledged" blip
        }
    } else if (!_menuActive) {
        if (taps >= 3) {
            _menuActive = true;
            _menuPage = 0;
            _menuLastInputMs = millis();
            static const uint16_t f[] = {1000, 1300, 1700};
            static const uint16_t d[] = {40, 40, 60};
            _buzzer.playSequence(f, d, 3);
        }
    } else if (_stopwatchActive) {
        // A stopwatch session is running/paused on STOPWATCH_PAGE - its
        // taps are captured for start/stop and reset instead of paging.
        if (taps == 1) {
            if (_stopwatchRunning) {
                _stopwatchElapsedMs += millis() - _stopwatchStartMs;
                _stopwatchRunning = false;
                playNote(_buzzer, 900, 70); // pause
            } else {
                _stopwatchStartMs = millis();
                _stopwatchRunning = true;
                playNote(_buzzer, 1500, 70); // resume
            }
        } else if (taps >= 2) {
            // Reset - hands the page back to normal single-tap navigation.
            _stopwatchActive = false;
            _stopwatchRunning = false;
            _stopwatchElapsedMs = 0;
            static const uint16_t f[] = {1300, 900};
            static const uint16_t d[] = {60, 90};
            _buzzer.playSequence(f, d, 2);
        }
        if (taps > 0) _menuLastInputMs = millis();
        // No timeout check here: the menu stays open for as long as a
        // stopwatch session is active (running or paused), per spec.
    } else if (_menuPage == TIMER_PAGE && (_timerSubMenu || _timerRunning)) {
        // A timer session is being armed or is running on TIMER_PAGE.
        // Unlike the stopwatch, the 10s menu-idle timeout below stays in
        // effect - the countdown itself keeps going in the background
        // (see updateTimer()) even after the menu closes.
        if (_timerRunning) {
            if (taps > 0) {
                _timerRunning = false;
                static const uint16_t f[] = {1000, 700};
                static const uint16_t d[] = {60, 80};
                _buzzer.playSequence(f, d, 2);
            }
        } else if (taps == 1) {
            _timerRunning = true;
            _timerSubMenu = false;
            _timerEndMs = millis() + (unsigned long)_timerDurationMin * 60000UL;
            static const uint16_t f[] = {1200, 1600, 2000};
            static const uint16_t d[] = {50, 50, 70};
            _buzzer.playSequence(f, d, 3);
        } else if (taps == 2) {
            _timerDurationMin = (_timerDurationMin % TIMER_MAX_MIN) + 1; // wraps 60 -> 1
            playNote(_buzzer, 1600, 30);
        } else if (taps >= 3) {
            _timerDurationMin = (_timerDurationMin <= TIMER_MIN_MIN) ? TIMER_MAX_MIN : _timerDurationMin - 1;
            playNote(_buzzer, 700, 30);
        }
        if (taps > 0) _menuLastInputMs = millis();
        if (millis() - _menuLastInputMs > MENU_TIMEOUT_MS) {
            _menuActive = false;
        }
    } else {
        if (taps == 2 && _menuPage == STOPWATCH_PAGE) {
            _stopwatchActive = true;
            _stopwatchRunning = true;
            _stopwatchElapsedMs = 0;
            _stopwatchStartMs = millis();
            _menuLastInputMs = millis();
            static const uint16_t f[] = {1200, 1700};
            static const uint16_t d[] = {60, 90};
            _buzzer.playSequence(f, d, 2);
        } else if (taps == 2 && _menuPage == TIMER_PAGE) {
            _timerSubMenu = true;
            _menuLastInputMs = millis();
            static const uint16_t f[] = {1100, 1400};
            static const uint16_t d[] = {50, 50};
            _buzzer.playSequence(f, d, 2);
        } else if (taps >= 1) {
            _menuPage = (_menuPage + 1) % MENU_PAGE_COUNT;
            _menuLastInputMs = millis();
            playNote(_buzzer, 1400, 35);
        }
        if (millis() - _menuLastInputMs > MENU_TIMEOUT_MS) {
            _menuActive = false;
        }
    }

    // Keep mood current even while the menu is showing, so the eyes are
    // right the instant the menu times out back to them.
    updateMood();

    if (_menuActive) {
        drawMenu();
    } else {
        _eyes->eyes.update();
    }
}

void CHUZAFace::drawMenu() {
    _display.clearDisplay();
    _display.setTextColor(SSD1306_WHITE);

    switch (_menuPage) {
        case 0: {
            struct tm timeinfo;
            bool haveTime = getLocalTime(&timeinfo, 0);

            _display.setTextSize(1);
            _display.setCursor(0, 0);
            _display.println("== CHUZA ==");

            _display.setTextSize(2);
            _display.setCursor(4, 24);
            if (haveTime) {
                char buf[9];
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                _display.println(buf);
            } else {
                _display.println("--:--:--");
            }

            _display.setTextSize(1);
            _display.setCursor(4, 48);
            if (haveTime) {
                char dateBuf[16];
                snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d/%04d",
                         timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
                _display.println(dateBuf);
            } else {
                _display.println("(no NTP sync yet)");
            }
            break;
        }
        case 1: {
            _display.setTextSize(1);
            _display.setCursor(0, 0);
            _display.println("== Environment ==");
            _display.setCursor(0, 16);
            _display.printf("Temp: %.1f C\n", _env.getTemperatureC());
            _display.setCursor(0, 28);
            _display.printf("Humidity: %.1f %%\n", _env.getHumidityPct());
            _display.setCursor(0, 40);
            _display.printf("Pressure: %.0f hPa\n", _env.getPressureHpa());
            _display.setCursor(0, 52);
            _display.printf("Altitude: %.0f m\n", _env.getAltitudeM());
            break;
        }
        case 2: {
            _display.setTextSize(1);
            _display.setCursor(0, 0);
            _display.println("== Status ==");
            _display.setCursor(0, 16);
            _display.printf("Batt: %d%% (%.2fV)\n", _batt.getPercent(), _batt.getVoltage());
            _display.setCursor(0, 28);
            _display.printf("Dist: %u mm\n", _dist.getDistanceMm());
            _display.setCursor(0, 40);
            _display.printf("WiFi: %d dBm\n", WiFi.RSSI());
            _display.setCursor(0, 52);
            _display.print(WiFi.localIP());
            break;
        }
        case STOPWATCH_PAGE: {
            if (_stopwatchActive) {
                unsigned long total = _stopwatchElapsedMs + (_stopwatchRunning ? millis() - _stopwatchStartMs : 0);
                unsigned long totalSec = total / 1000;
                unsigned int mm = totalSec / 60;
                unsigned int ss = totalSec % 60;
                unsigned int ds = (total % 1000) / 100;

                _display.setTextSize(1);
                _display.setCursor(0, 0);
                _display.println("== Stopwatch ==");

                _display.setTextSize(2);
                _display.setCursor(10, 22);
                char buf[12];
                snprintf(buf, sizeof(buf), "%02u:%02u.%u", mm, ss, ds);
                _display.println(buf);

                _display.setTextSize(1);
                _display.setCursor(0, 48);
                _display.println(_stopwatchRunning ? "RUNNING - tap to stop" : "PAUSED - tap to go");
            } else {
                _display.setTextSize(1);
                _display.setCursor(0, 0);
                _display.println("== Stopwatch ==");

                _display.setTextSize(2);
                _display.setCursor(10, 22);
                _display.println("00:00.0");

                _display.setTextSize(1);
                _display.setCursor(0, 48);
                _display.println("dbl-tap to start");
            }
            break;
        }
        default: { // TIMER_PAGE
            _display.setTextSize(1);
            _display.setCursor(0, 0);
            _display.println("== Timer ==");

            if (_timerAlarming) {
                _display.setTextSize(2);
                _display.setCursor(4, 24);
                _display.println("TIME UP!");
                _display.setTextSize(1);
                _display.setCursor(0, 48);
                _display.println("tap to silence");
            } else if (_timerRunning) {
                unsigned long remainMs = (_timerEndMs > millis()) ? (_timerEndMs - millis()) : 0;
                unsigned long remainSec = remainMs / 1000;
                unsigned int mm = remainSec / 60;
                unsigned int ss = remainSec % 60;

                _display.setTextSize(2);
                _display.setCursor(16, 22);
                char buf[8];
                snprintf(buf, sizeof(buf), "%02u:%02u", mm, ss);
                _display.println(buf);

                _display.setTextSize(1);
                _display.setCursor(0, 48);
                _display.println("tap to stop");
            } else if (_timerSubMenu) {
                _display.setTextSize(1);
                _display.setCursor(0, 16);
                _display.printf("Set: %u min\n", _timerDurationMin);
                _display.setCursor(0, 40);
                _display.println("tap:start");
                _display.setCursor(0, 52);
                _display.println("dbl:+1  tri:-1");
            } else {
                _display.setTextSize(1);
                _display.setCursor(0, 16);
                _display.printf("Length: %u min\n", _timerDurationMin);
                _display.setCursor(0, 40);
                _display.println("dbl-tap to set");
            }
            break;
        }
    }

    _display.display();
}
