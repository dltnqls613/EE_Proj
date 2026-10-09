#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace EE {
constexpr uint8_t MASTER_ID = 0;
constexpr uint8_t DISCOVERY_ID = 255;
constexpr size_t MAX_NAME = 63;
constexpr size_t MAX_PACKET = 96;

using SpecialCommand = void (*)(uint8_t code, const uint8_t *payload, size_t size);
using SendPacket = void (*)(const uint8_t *packet, size_t size);

void begin(SendPacket send, SpecialCommand special);
void receive(uint8_t byte);
void local_command(uint8_t code, const uint8_t *payload, size_t size);
void tick();
bool set_id(unsigned int id); // 1..254 only; ID 0 is always SBARMV10.
uint8_t id();
bool set_name(const uint8_t *name, size_t size);
void reply(uint8_t code, const uint8_t *payload = nullptr, size_t size = 0);
void reply_floats(uint8_t code, const float *values, size_t count);
bool read_floats(const uint8_t *payload, size_t size, float *values, size_t count);
bool set_servo(unsigned int num, float pulse_us, float seconds);
bool set_motor(unsigned int num, float power, float seconds);
bool set_heater(float power, float seconds);
float adc();
float temperature();
float weight();
}
