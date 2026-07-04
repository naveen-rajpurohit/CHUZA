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
