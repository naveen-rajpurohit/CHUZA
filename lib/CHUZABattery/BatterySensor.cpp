#include "BatterySensor.h"

// The divider halves the battery voltage before it reaches the ADC
// pin, so the raw reading is doubled back out to get true battery
// voltage.
static const float DIVIDER_RATIO = 2.0f;

// Smoothing factor for the voltage EMA — lower = smoother but slower
// to react. Tuned for a percentage readout that doesn't flicker on
// every ADC sample rather than for tracking fast transients.
static const float EMA_ALPHA = 0.2f;

BatterySensor::BatterySensor(uint8_t adcPin, float minVoltage, float maxVoltage)
    : _adcPin(adcPin), _minVoltage(minVoltage), _maxVoltage(maxVoltage) {}

void BatterySensor::begin() {
    analogReadResolution(12); // explicit for clarity; matches the arduino-esp32 default
}

void BatterySensor::update() {
    float pinVoltage = analogReadMilliVolts(_adcPin) / 1000.0f;
    float sample = pinVoltage * DIVIDER_RATIO;

    _voltage = _hasReading ? (EMA_ALPHA * sample + (1.0f - EMA_ALPHA) * _voltage) : sample;
    _hasReading = true;
}

float BatterySensor::getVoltage() const { return _voltage; }

uint8_t BatterySensor::getPercent() const {
    if (!_hasReading) return 0;

    float pct = (_voltage - _minVoltage) / (_maxVoltage - _minVoltage) * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return (uint8_t)(pct + 0.5f);
}
