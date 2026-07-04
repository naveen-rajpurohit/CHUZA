#include "MotorTestCLI.h"

void beginMotorTestCLI() {
    Serial.println("\n--- CHUZA Motor Calibration (PERCENTAGE MODE) ---");
    Serial.println("Format: <motor>,<speed_percentage>");
    Serial.println("  'l,10'   -> Left motor target 10% (~187 PWM once ramped up)");
    Serial.println("  'r,-50'  -> Right motor target -50%");
    Serial.println("  'b,100'  -> Both motors target 100%");
    Serial.println("  's'      -> stop()  - gradual, ramped stop");
    Serial.println("  'x'      -> brake() - instant emergency stop");
    Serial.println("Ready for input...\n");
}

void pollMotorTestCLI(CHUZAWheels &wheels) {
    if (Serial.available() <= 0) return;

    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;

    char motorTarget = input.charAt(0);
    int commaIndex = input.indexOf(',');
    int speedPct = 0;

    if (commaIndex != -1) {
        speedPct = input.substring(commaIndex + 1).toInt();
    }

    switch (motorTarget) {
        case 'l':
        case 'L':
            wheels.setLeftMotor(speedPct);
            Serial.print("Executing: Left Motor target ");
            Serial.print(speedPct);
            Serial.println("%");
            break;

        case 'r':
        case 'R':
            wheels.setRightMotor(speedPct);
            Serial.print("Executing: Right Motor target ");
            Serial.print(speedPct);
            Serial.println("%");
            break;

        case 'b':
        case 'B':
            wheels.setLeftMotor(speedPct);
            wheels.setRightMotor(speedPct);
            Serial.print("Executing: Both Motors target ");
            Serial.print(speedPct);
            Serial.println("%");
            break;

        case 's':
        case 'S':
            wheels.stop();
            Serial.println("Executing: stop() - gradual ramp-down");
            break;

        case 'x':
        case 'X':
            wheels.brake();
            Serial.println("Executing: brake() - instant stop");
            break;

        default:
            Serial.println("Error: Unknown command. Use l, r, b, s, or x.");
            break;
    }
}