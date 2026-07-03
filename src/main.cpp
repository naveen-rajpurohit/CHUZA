#include <Arduino.h>
#include "CHUZAWheels.h"

// --- CHUZA V1 Direct Pin Mappings ---
#define RB   D6
#define RF   D0
#define LB   D10
#define LF   D9

// Instantiate the wheels subsystem
CHUZAWheels wheels(LF, LB, RF, RB);

void setup() {
    Serial.begin(115200);
    
    wheels.begin();
    
    delay(2000); 
}

void loop() {
    wheels.moveBurst(150, 240, 200);   // left, right, duration in ms
    delay(1000);
    
    wheels.moveBurst(240, 150, 150);
    delay(1000);

    wheels.moveBurst(-150, -240, 200); 
    delay(1000);
    
    wheels.moveBurst(-240, -150, 150);
    delay(1000);
    
    wheels.moveBurst(80, 80, 300);
    delay(2000);
}