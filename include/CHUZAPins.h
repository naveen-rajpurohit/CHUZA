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

// I2C bus (BME280 environmental sensor, and anything else on the bus)
#define PIN_SDA  D4
#define PIN_SCL  D5

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
