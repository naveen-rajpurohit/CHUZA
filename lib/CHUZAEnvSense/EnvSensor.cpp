#include "EnvSensor.h"

EnvSensor::EnvSensor(uint8_t sdaPin, uint8_t sclPin, uint8_t i2cAddress)
    : _sdaPin(sdaPin), _sclPin(sclPin), _i2cAddress(i2cAddress) {}

bool EnvSensor::begin() {
    Wire.begin(_sdaPin, _sclPin);

    _sensorFound = _bme.begin(_i2cAddress, &Wire);

    if (!_sensorFound) {
        // Try the other common address before giving up.
        uint8_t altAddress = (_i2cAddress == 0x76) ? 0x77 : 0x76;
        _sensorFound = _bme.begin(altAddress, &Wire);
    }

    return _sensorFound;
}

void EnvSensor::setSeaLevelPressureHpa(float hPa) {
    _seaLevelHpa = hPa;
}

void EnvSensor::update() {
    if (!_sensorFound) return;

    _temperatureC = _bme.readTemperature();
    _humidityPct  = _bme.readHumidity();
    _pressureHpa  = _bme.readPressure() / 100.0f; // Pa -> hPa
    _altitudeM    = _bme.readAltitude(_seaLevelHpa);

    _hasReading = true;
}

float EnvSensor::getTemperatureC() const { return _temperatureC; }
float EnvSensor::getHumidityPct() const { return _humidityPct; }
float EnvSensor::getPressureHpa() const { return _pressureHpa; }
float EnvSensor::getAltitudeM() const { return _altitudeM; }

bool EnvSensor::isReady() const { return _sensorFound && _hasReading; }