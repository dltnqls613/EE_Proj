#pragma once
#include <Arduino.h>
inline void *uart1 = nullptr;
struct RP2040_UART {
    std::vector<uint8_t> input;
    std::vector<std::vector<uint8_t>> output;
    bool begin(void *, int, int, int) { return true; }
    const char *lastBeginErrorString() { return "test"; }
    size_t write(const uint8_t *data, size_t size) { output.emplace_back(data, data+size); return size; }
    size_t read(uint8_t *data, size_t max) {
        size_t count = std::min(max, input.size());
        std::copy_n(input.begin(), count, data); input.erase(input.begin(), input.begin()+count); return count;
    }
};
