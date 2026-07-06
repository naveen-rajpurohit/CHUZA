#pragma once

// --- CHUZA V1 Pin Map ---
// Every pin on the robot lives here. As sensors / display / camera /
// wifi peripherals come online, add their pins below instead of
// scattering #define's through main.cpp or other modules.

// CHUZAWheels (Motor_Driver)
#define PIN_LF   D9
#define PIN_LB   D10
#define PIN_RF   D0
#define PIN_RB   D6

// I2C bus (BME280 environmental sensor, VL53L0X time-of-flight distance
// sensor, and anything else on the bus)
#define PIN_SDA  D4
#define PIN_SCL  D5

// Battery voltage sense: midpoint of a resistor divider across the
// battery, read on an ADC-capable pin. The divider halves the battery
// voltage before it reaches this pin, so BatterySensor doubles the
// reading back out.
#define PIN_BATT_SENSE D2

// Capacitive touch pad (petting sensor) for CHUZAFace's mood engine and
// menu navigation. Any GPIO1-14 works for touch on the ESP32-S3; D1 was
// free.
#define PIN_TOUCH D1

// Piezo buzzer for CHUZAFace's beeps/jingles (touch chirps, mood/menu/
// stopwatch/timer events).
#define PIN_BUZZER D8

// Onboard OV2640 camera (XIAO ESP32S3 Sense). Fixed silicon routing on
// the Sense sensor board — these are GPIO numbers, not D-pin aliases,
// and don't overlap with the D-pins used above.
#define CAM_PIN_PWDN     -1
#define CAM_PIN_RESET    -1
#define CAM_PIN_XCLK     10
#define CAM_PIN_SIOD     40
#define CAM_PIN_SIOC     39
#define CAM_PIN_Y9       48
#define CAM_PIN_Y8       11
#define CAM_PIN_Y7       12
#define CAM_PIN_Y6       14
#define CAM_PIN_Y5       16
#define CAM_PIN_Y4       18
#define CAM_PIN_Y3       17
#define CAM_PIN_Y2       15
#define CAM_PIN_VSYNC    38
#define CAM_PIN_HREF     47
#define CAM_PIN_PCLK     13
