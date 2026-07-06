#include "DistanceSensor.h"

// The VL53L0X reports this when nothing reflective is within its
// ranging window - not a real distance, just its "out of range" flag.
static const uint16_t OUT_OF_RANGE_MM = 8190;

DistanceSensor::DistanceSensor(uint8_t sdaPin, uint8_t sclPin, uint8_t i2cAddress)
    : _sdaPin(sdaPin), _sclPin(sclPin), _i2cAddress(i2cAddress) {}

bool DistanceSensor::begin() {
    Wire.begin(_sdaPin, _sclPin);
    _sensorFound = _vl53.begin(_i2cAddress, false, &Wire);
    return _sensorFound;
}

void DistanceSensor::update() {
    if (!_sensorFound) return;

    VL53L0X_RangingMeasurementData_t measure;
    _vl53.rangingTest(&measure, false);

    // RangeStatus 4 is the Adafruit driver's "out of range" status
    // code (see their own rangingTest examples) - anything else is a
    // valid reading.
    _distanceMm = (measure.RangeStatus != 4) ? measure.RangeMilliMeter : OUT_OF_RANGE_MM;
    _hasReading = true;
}

uint16_t DistanceSensor::getDistanceMm() const { return _distanceMm; }

bool DistanceSensor::isReady() const { return _sensorFound && _hasReading; }
