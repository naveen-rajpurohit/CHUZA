#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>

// Wraps a BME280 on I2C. Caches the last reading so getters are cheap
// and always return the most recent sample, whatever update() cadence
// you choose from the scheduler.
class EnvSensor {
public:
    // i2cAddress: most breakout boards are 0x76 or 0x77. begin() will
    // try the other one automatically if the given address doesn't
    // respond, so you usually don't need to change this.
    EnvSensor(uint8_t sdaPin, uint8_t sclPin, uint8_t i2cAddress = 0x76);

    // Call once in setup(). Returns false if no BME280 responded on
    // the bus (check wiring / power / address if so).
    bool begin();

    // Call periodically (e.g. every 50ms via the scheduler). Reads the
    // sensor over I2C and caches the latest values.
    void update();

    // Optional: call once if you know your local sea-level pressure
    // reference, for a more accurate altitude. Defaults to the
    // standard 1013.25 hPa.
    void setSeaLevelPressureHpa(float hPa);

    float getTemperatureC() const;
    float getHumidityPct() const;
    float getPressureHpa() const;
    float getAltitudeM() const;

    // True once begin() has found the sensor AND update() has run once.
    bool isReady() const;

private:
    Adafruit_BME280 _bme;
    uint8_t _sdaPin, _sclPin, _i2cAddress;
    float _seaLevelHpa = 1013.25f;

    float _temperatureC = 0.0f;
    float _humidityPct = 0.0f;
    float _pressureHpa = 0.0f;
    float _altitudeM = 0.0f;

    bool _sensorFound = false;
    bool _hasReading = false;
};