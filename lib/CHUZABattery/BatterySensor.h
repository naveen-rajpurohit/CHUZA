#pragma once
#include <Arduino.h>

// Reads battery voltage through a resistor divider tapped at its
// midpoint into an ADC-capable pin, and turns that into a 0-100%
// estimate over a fixed voltage range. Caches the last reading so
// getters are cheap, same pattern as EnvSensor.
class BatterySensor {
public:
    // adcPin: ADC-capable pin wired to the divider midpoint (see
    // PIN_BATT_SENSE in CHUZAPins.h). minVoltage/maxVoltage: the
    // empty/full voltage of the cell(s) powering the robot. Defaults
    // are for a single-cell Li-ion/LiPo (3.6V ~ "dead", 4.2V ~ full).
    explicit BatterySensor(uint8_t adcPin, float minVoltage = 3.6f, float maxVoltage = 4.2f);

    // Call once in setup().
    void begin();

    // Call periodically (e.g. every 500ms via the scheduler — battery
    // voltage drifts slowly, no need for a fast tick). Reads the ADC
    // and updates the cached voltage/percentage. Raw ADC samples are
    // smoothed with a simple EMA so the percentage doesn't jitter
    // between reads.
    void update();

    float getVoltage() const;   // actual battery voltage, in volts
    uint8_t getPercent() const; // 0-100, clamped to [minVoltage, maxVoltage]

private:
    uint8_t _adcPin;
    float _minVoltage, _maxVoltage;

    float _voltage = 0.0f;
    bool _hasReading = false;
};
