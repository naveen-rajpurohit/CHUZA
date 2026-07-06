#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>

// Wraps a VL53L0X time-of-flight distance sensor sharing the I2C bus
// with the other sensors (see PIN_SDA/PIN_SCL in CHUZAPins.h). Caches
// the last reading so getters are cheap, same pattern as EnvSensor.
class DistanceSensor {
public:
    // i2cAddress: 0x29 is the VL53L0X's fixed default - change only if
    // you've re-addressed it (e.g. running more than one on the bus).
    DistanceSensor(uint8_t sdaPin, uint8_t sclPin, uint8_t i2cAddress = 0x29);

    // Call once in setup(). Returns false if no VL53L0X responded on
    // the bus (check wiring/power if so).
    bool begin();

    // Call periodically (e.g. every 100ms via the scheduler). Runs one
    // ranging measurement (blocks for ~30ms - the sensor's own
    // measurement time) and caches the result.
    void update();

    // Distance to whatever's in front of the sensor, in millimeters.
    // Reads back the VL53L0X's own out-of-range sentinel (8190) when
    // nothing reflective is within range.
    uint16_t getDistanceMm() const;

    // True once begin() has found the sensor AND update() has run once.
    bool isReady() const;

private:
    Adafruit_VL53L0X _vl53;
    uint8_t _sdaPin, _sclPin, _i2cAddress;

    uint16_t _distanceMm = 0;
    bool _sensorFound = false;
    bool _hasReading = false;
};
