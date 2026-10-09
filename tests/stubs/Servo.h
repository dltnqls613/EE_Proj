#pragma once
#include <Arduino.h>
inline int test_servo_us[32] = {};
struct Servo {
    int pin = -1;
    bool attached() { return pin >= 0; }
    void attach(int p, int, int) { pin = p; }
    void detach() { pin = -1; }
    void writeMicroseconds(int value) { test_servo_us[pin] = value; }
};
