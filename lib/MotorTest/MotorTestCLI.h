#pragma once
#include "CHUZAWheels.h"

// Bench-test console: type commands into the Serial Monitor to drive
// the wheels directly. Not part of normal robot operation — handy for
// calibration and sanity checks.
//
//   l,10   -> left motor target 10%
//   r,-50  -> right motor target -50%
//   b,100  -> both motors target 100%
//   s      -> stop()  (gradual, ramped)
//   x      -> brake() (instant emergency stop)

void beginMotorTestCLI();                               // prints instructions to Serial Monitor, add this to setup() once
void pollMotorTestCLI(CHUZAWheels &wheels);             // call this on every loop() iteration to read Serial input and drive the wheels