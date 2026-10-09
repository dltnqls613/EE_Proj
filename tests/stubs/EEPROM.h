#pragma once
#include <Arduino.h>
struct EEPROMStub {
    std::vector<uint8_t> data;
    int commits = 0;
    void begin(size_t size) { if (data.empty()) data.resize(size, 255); }
    template<class T> void get(size_t pos, T &value) { memcpy(&value, data.data()+pos, sizeof(T)); }
    template<class T> void put(size_t pos, const T &value) { memcpy(data.data()+pos, &value, sizeof(T)); }
    bool commit() { ++commits; return true; }
};
inline EEPROMStub EEPROM;
