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
    
    Serial.println("\n--- CHUZA Motor Calibration (PERCENTAGE MODE) ---");
    Serial.println("Format: <motor>,<speed_percentage>");
    Serial.println("  'l,10'   -> Left motor forward at 10% (translates to ~187 PWM)");
    Serial.println("  'r,-50'  -> Right motor reverse at 50% power");
    Serial.println("  'b,100'  -> Both motors forward at 100% (255 PWM)");
    Serial.println("  's'      -> Stop all motors");
    Serial.println("Ready for input...\n");
}

void loop() {
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim(); 

        if (input.length() > 0) {
            char motorTarget = input.charAt(0);     
            int commaIndex = input.indexOf(',');    
            int speedPct = 0;

            if (commaIndex != -1) {
                speedPct = input.substring(commaIndex + 1).toInt();
            }

            switch(motorTarget) {
                case 'l':
                case 'L':
                    wheels.setLeftMotor(speedPct);
                    Serial.print("Executing: Left Motor @ "); 
                    Serial.print(speedPct);
                    Serial.println("%");
                    break;
                    
                case 'r':
                case 'R':
                    wheels.setRightMotor(speedPct);
                    Serial.print("Executing: Right Motor @ "); 
                    Serial.print(speedPct);
                    Serial.println("%");
                    break;
                    
                case 'b':
                case 'B':
                    wheels.setLeftMotor(speedPct);
                    wheels.setRightMotor(speedPct);
                    Serial.print("Executing: Both Motors @ "); 
                    Serial.print(speedPct);
                    Serial.println("%");
                    break;
                    
                case 's':
                case 'S':
                    wheels.stop();
                    Serial.println("Executing: BRAKE");
                    break;
                    
                default:
                    Serial.println("Error: Unknown command. Use l, r, b, or s.");
                    break;
            }
        }
    }
}