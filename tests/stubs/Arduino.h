#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLUP 2
inline uint32_t test_ms = 0;
inline int test_pins[32] = {};
inline int test_adc[32] = {};
inline uint32_t millis() { return test_ms; }
inline void pinMode(int pin, int mode) { if (mode == INPUT_PULLUP) test_pins[pin] = HIGH; }
inline void digitalWrite(int pin, int value) { test_pins[pin] = value; }
inline int digitalRead(int pin) { return test_pins[pin]; }
inline void analogWrite(int pin, int value) { test_pins[pin] = value; }
inline int analogRead(int pin) { return test_adc[pin]; }
inline void analogReadResolution(int) {}
inline void analogWriteResolution(int) {}
inline void delayMicroseconds(int) {}
template<class T> inline T constrain(T v, T lo, T hi) { return std::min(hi, std::max(lo, v)); }
struct SerialStub {
    std::string input;
    void begin(int) {}
    int available() { return input.size(); }
    int read() { int c = input[0]; input.erase(0,1); return c; }
    void println(const char *) {}
};
inline SerialStub Serial;
