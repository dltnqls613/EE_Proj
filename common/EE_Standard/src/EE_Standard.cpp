#include "EE_Standard.h"
#include <EE_Config.h>
#include <pin.h>
#include <EEPROM.h>
#include <Servo.h>
#include <math.h>
#include <string.h>
#include <hardware/sync.h>

namespace EE {
static_assert(EE_DEFAULT_ID > 0 && EE_DEFAULT_ID < DISCOVERY_ID, "EE ID must be 1..254");
static_assert(sizeof(float) == 4, "Protocol requires float32");
static SendPacket send_packet;
static SpecialCommand special_command;
static uint8_t device_id = EE_DEFAULT_ID;
static char device_name[MAX_NAME + 1] = EE_DEFAULT_NAME;
static Servo servos[2];
static const uint8_t servo_pins[] = {Servo0_PIN, Servo1_PIN};
static const uint8_t enable_pins[] = {Servo0_EN_PIN, Servo1_EN_PIN};
static const uint8_t motor_pins[][2] = {{MOTOR_IN1_PIN, MOTOR_IN2_PIN}, {MOTOR_IN3_PIN, MOTOR_IN4_PIN}};
static float servo_pulse[] = {EE_SERVO0_INITIAL_US, EE_SERVO1_INITIAL_US};
static float motor_power[2] = {};
static float heater_power = 0;
struct Timer { uint32_t start = 0, duration = 0; };
static Timer servo_timer[2], motor_timer[2], heater_timer;
static uint8_t rx[MAX_PACKET];
static size_t rx_size = 0;
static uint32_t rx_time = 0;
static int32_t load_raw = 0;
static bool load_valid = false;
static uint32_t load_time = 0, pid_time = 0;
static float pid_target = 0, pid_kp = 0, pid_ki = 0, pid_kd = 0;
static float pid_integral = 0, pid_previous = NAN;
static bool pid_enabled = false;
struct Settings { uint32_t magic; char name[MAX_NAME + 1]; };
static constexpr uint32_t SETTINGS_MAGIC = 0x45453101;

static bool expired(const Timer &timer, uint32_t now) {
    return timer.duration && uint32_t(now - timer.start) >= timer.duration;
}
static bool valid_time(float seconds) {
    return isfinite(seconds) && seconds >= 0 && seconds <= 3600;
}
static void start_timer(Timer &timer, float seconds) {
    timer.start = millis();
    timer.duration = seconds > 0 ? uint32_t(ceilf(seconds * 1000.0f)) : 0;
}
static void servo_off(unsigned int num) {
    digitalWrite(enable_pins[num], !EE_SERVO_ENABLE_LEVEL);
    if (servos[num].attached()) servos[num].detach();
    digitalWrite(servo_pins[num], LOW);
    servo_timer[num].duration = 0;
}
static void motor_output(unsigned int num, float power) {
    // Drop both inputs before changing direction.
    analogWrite(motor_pins[num][0], 0);
    analogWrite(motor_pins[num][1], 0);
    if (power != 0) analogWrite(motor_pins[num][power < 0 ? 1 : 0], lroundf(fabsf(power) * 255));
    motor_power[num] = power;
}
static void heater_output(float power) {
    heater_power = power;
    analogWrite(HEATER_PWM_PIN, lroundf(power * 255));
}
uint8_t id() { return device_id; }
bool set_id(unsigned int value) {
    if (value == MASTER_ID || value >= DISCOVERY_ID) return false;
    device_id = value;
    return true;
}
bool set_name(const uint8_t *name, size_t size) {
    if (!size || size > MAX_NAME) return false;
    for (size_t i = 0; i < size; ++i) if (name[i] < 32 || name[i] == 127) return false;
    if (strlen(device_name) == size && memcmp(device_name, name, size) == 0) return true;
    memcpy(device_name, name, size);
    device_name[size] = 0;
    Settings settings = {};
    settings.magic = SETTINGS_MAGIC;
    memcpy(settings.name, device_name, sizeof(device_name));
    EEPROM.put(0, settings);
    return EEPROM.commit();
}
void reply(uint8_t code, const uint8_t *payload, size_t size) {
    if (size > MAX_PACKET - 8 || !send_packet) return;
    uint8_t packet[MAX_PACKET] = {254, 254, uint8_t(size + 8), 0, device_id, MASTER_ID, code};
    if (size) memcpy(packet + 7, payload, size);
    uint8_t checksum = 0;
    for (size_t i = 0; i < size + 7; ++i) checksum += packet[i];
    packet[size + 7] = checksum;
    send_packet(packet, size + 8);
}
void reply_floats(uint8_t code, const float *values, size_t count) {
    reply(code, reinterpret_cast<const uint8_t *>(values), count * sizeof(float));
}
bool read_floats(const uint8_t *payload, size_t size, float *values, size_t count) {
    if (size != count * sizeof(float)) return false;
    if (size) memcpy(values, payload, size);
    for (size_t i = 0; i < count; ++i) if (!isfinite(values[i])) return false;
    return true;
}
bool set_servo(unsigned int num, float pulse, float seconds) {
    if (num > 1 || !isfinite(pulse) || pulse < EE_SERVO_MIN_US ||
        pulse > EE_SERVO_MAX_US || !valid_time(seconds)) return false;
    if (seconds == 0) { servo_off(num); return true; }
    servo_pulse[num] = lroundf(pulse);
    // Keep the power gate off while attaching/configuring the PWM signal.
    digitalWrite(enable_pins[num], !EE_SERVO_ENABLE_LEVEL);
    if (!servos[num].attached()) servos[num].attach(servo_pins[num], EE_SERVO_MIN_US, EE_SERVO_MAX_US);
    servos[num].writeMicroseconds(lroundf(pulse));
    digitalWrite(enable_pins[num], EE_SERVO_ENABLE_LEVEL);
    start_timer(servo_timer[num], seconds);
    return true;
}
bool set_motor(unsigned int num, float power, float seconds) {
    if (num > 1 || !isfinite(power) || fabsf(power) > 1 || !valid_time(seconds)) return false;
    motor_output(num, seconds > 0 ? power : 0);
    start_timer(motor_timer[num], seconds);
    return true;
}
bool set_heater(float power, float seconds) {
    if (!isfinite(power) || power < 0 || power > 1 || !valid_time(seconds)) return false;
    pid_enabled = false;
    heater_output(seconds > 0 ? power : 0);
    start_timer(heater_timer, seconds);
    return true;
}
float adc() { return analogRead(ADC_PIN); }
float temperature() {
    int raw = analogRead(TEMP_ADC_PIN);
    if (EE_TEMP_C_PER_COUNT == 0 || raw <= 0 || raw >= 4095) return NAN;
    return raw * EE_TEMP_C_PER_COUNT + EE_TEMP_OFFSET_C;
}
float weight() {
    if (!load_valid || uint32_t(millis() - load_time) > 1000 || EE_LOADCELL_COUNTS_PER_KG == 0) return NAN;
    return (float(load_raw) - EE_LOADCELL_OFFSET) / EE_LOADCELL_COUNTS_PER_KG;
}
static void dispatch(uint8_t code, const uint8_t *payload, size_t size) {
    float values[4] = {};
    switch (code) {
    case 1:
        if (set_name(payload, size)) reply(3, reinterpret_cast<const uint8_t *>(device_name), strlen(device_name));
        break;
    case 2:
        if (!size) reply(3, reinterpret_cast<const uint8_t *>(device_name), strlen(device_name));
        break;
    case 4:
        if (read_floats(payload, size, values, 3) && (values[0] == 0 || values[0] == 1)) {
            if (set_servo(values[0], values[1], values[2]) && special_command)
                special_command(code, payload, size); // Cancel this model's deferred servo action.
        }
        break;
    case 5:
        if (!size) reply_floats(6, servo_pulse, 2);
        break;
    case 7:
        if (read_floats(payload, size, values, 3) && (values[0] == 0 || values[0] == 1))
            set_motor(values[0], values[1], values[2]);
        break;
    case 8:
        if (!size) reply_floats(9, motor_power, 2);
        break;
    case 10:
        if (!size) { values[0] = weight(); reply_floats(11, values, 1); }
        break;
    case 12:
        if (!size) { values[0] = temperature(); reply_floats(13, values, 1); }
        break;
    case 14:
        if (!size) { values[0] = adc(); reply_floats(15, values, 1); }
        break;
    case 16:
        if (read_floats(payload, size, values, 2)) set_heater(values[0], values[1]);
        break;
    case 17:
        if (!size) reply_floats(18, &heater_power, 1);
        break;
    case 19:
        if (read_floats(payload, size, values, 1) && values[0] <= EE_TEMP_MAX_C) {
            pid_target = values[0];
            pid_enabled = pid_target > 0;
            pid_integral = 0;
            pid_previous = NAN;
            pid_time = millis();
            heater_timer.duration = 0;
            heater_output(0);
        }
        break;
    case 20: case 21: case 22:
        if (read_floats(payload, size, values, 1) && values[0] >= 0) {
            if (code == 20) pid_kp = values[0];
            if (code == 21) pid_ki = values[0];
            if (code == 22) pid_kd = values[0];
            pid_integral = 0;
        }
        break;
    case 23:
        if (!size) {
            float info[] = {pid_target, pid_kp, pid_ki, pid_kd};
            reply_floats(24, info, 4);
        }
        break;
    default:
        // Return codes 3,6,9,11,13,15,18,24 are generated above, not actuations.
        if (code >= 100 && special_command) special_command(code, payload, size);
        break;
    }
}
void local_command(uint8_t code, const uint8_t *payload, size_t size) {
    if (size <= MAX_PACKET - 8) dispatch(code, payload, size);
}
void receive(uint8_t byte) {
    uint32_t now = millis();
    if (rx_size && uint32_t(now - rx_time) > 100) rx_size = 0;
    rx_time = now;
    if (rx_size == MAX_PACKET) { memmove(rx, rx + 1, --rx_size); }
    rx[rx_size++] = byte;
    while (rx_size >= 2) {
        if (rx[0] != 254 || rx[1] != 254) { memmove(rx, rx + 1, --rx_size); continue; }
        if (rx_size < 4) return;
        size_t length = rx[2] | (rx[3] << 8);
        if (length < 8 || length > MAX_PACKET) { memmove(rx, rx + 1, --rx_size); continue; }
        if (rx_size < length) return;
        uint8_t checksum = 0;
        for (size_t i = 0; i < length - 1; ++i) checksum += rx[i];
        if (checksum != rx[length - 1]) { memmove(rx, rx + 1, --rx_size); continue; }
        if (rx[4] == MASTER_ID && (rx[5] == device_id ||
            (rx[5] == DISCOVERY_ID && rx[6] == 2 && length == 8)))
            dispatch(rx[6], rx + 7, length - 8);
        rx_size -= length;
        memmove(rx, rx + length, rx_size);
    }
}
void tick() {
    uint32_t now = millis();
    for (unsigned int i = 0; i < 2; ++i) {
        if (expired(servo_timer[i], now)) servo_off(i);
        if (expired(motor_timer[i], now)) { motor_output(i, 0); motor_timer[i].duration = 0; }
    }
    if (expired(heater_timer, now)) { heater_output(0); heater_timer.duration = 0; }
    // HX711 A/128: read only when ready; never wait for an absent sensor.
    if (digitalRead(HX711_DT_PIN) == LOW) {
        uint32_t raw = 0;
        for (int i = 0; i < 25; ++i) {
            // IRQs must not stretch SCK high past the HX711 power-down threshold.
            uint32_t irq = save_and_disable_interrupts();
            digitalWrite(HX711_SCK_PIN, HIGH);
            delayMicroseconds(1);
            if (i < 24) raw = (raw << 1) | digitalRead(HX711_DT_PIN);
            digitalWrite(HX711_SCK_PIN, LOW);
            restore_interrupts(irq);
            delayMicroseconds(1);
        }
        load_raw = int32_t(raw & 0x800000 ? raw | 0xFF000000 : raw);
        load_valid = true;
        load_time = now;
    }
    if (uint32_t(now - pid_time) < 100) return;
    float dt = uint32_t(now - pid_time) / 1000.0f;
    pid_time = now;
    float temp = temperature();
    if (isfinite(temp) && temp >= EE_TEMP_MAX_C) {
        pid_enabled = false;
        heater_timer.duration = 0;
        heater_output(0);
    }
    if (!pid_enabled) return;
    if (!isfinite(temp)) { heater_output(0); pid_integral = 0; pid_previous = NAN; return; }
    float error = pid_target - temp;
    float derivative = isfinite(pid_previous) ? -(temp - pid_previous) / dt : 0;
    float integral = constrain(pid_integral + pid_ki * error * dt, 0.0f, 1.0f);
    float output = pid_kp * error + integral + pid_kd * derivative;
    if (isfinite(output)) {
        if ((output >= 0 && output <= 1) || (output > 1 && error < 0) || (output < 0 && error > 0))
            pid_integral = integral;
        heater_output(constrain(output, 0.0f, 1.0f));
    } else heater_output(0);
    pid_previous = temp;
}
void begin(SendPacket send, SpecialCommand special) {
    send_packet = send;
    special_command = special;
    rx_size = 0;
    EEPROM.begin(sizeof(Settings));
    Settings settings;
    EEPROM.get(0, settings);
    if (settings.magic == SETTINGS_MAGIC && settings.name[0] &&
        memchr(settings.name, 0, sizeof(settings.name))) {
        memcpy(device_name, settings.name, sizeof(device_name));
    }
    analogReadResolution(12);
    analogWriteResolution(8);
    for (unsigned int i = 0; i < 2; ++i) {
        pinMode(enable_pins[i], OUTPUT);
        pinMode(servo_pins[i], OUTPUT);
        servo_off(i);
        pinMode(motor_pins[i][0], OUTPUT);
        pinMode(motor_pins[i][1], OUTPUT);
        motor_output(i, 0);
    }
    pinMode(HEATER_PWM_PIN, OUTPUT);
    heater_output(0);
    pinMode(HX711_DT_PIN, INPUT_PULLUP);
    pinMode(HX711_SCK_PIN, OUTPUT);
    digitalWrite(HX711_SCK_PIN, LOW);
    pid_time = millis();
}
}
