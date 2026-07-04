#pragma once
#include "EnvSensor.h"

// Bench-test helper: dumps the latest BME280 reading to Serial.
// Wire this in as its own scheduled task (see main.cpp) — every time
// it's called it just prints whatever the sensor last read; it doesn't
// trigger an I2C read itself.
void printEnvReading(EnvSensor &env);