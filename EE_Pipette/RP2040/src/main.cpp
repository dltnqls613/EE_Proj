#include <Arduino.h>
#include <pin.h>
#include <EE_Config.h>
#include <EE_Standard.h>
#include <RP2040_UART.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

RP2040_UART Uart;
static bool pipette_return[2] = {};
static uint32_t pipette_started[2] = {};
static char serial_line[128];
static size_t serial_size = 0;
static bool serial_overflow = false;

static void uart_send(const uint8_t *packet, size_t size) { Uart.write(packet, size); }

static void pipette_command(uint8_t code, const uint8_t *payload, size_t size) {
    float amount;
    switch (code) {
    case 4: // Successful standard servo command supersedes only that servo's sequence.
        { float values[3];
          if (EE::read_floats(payload, size, values, 3) && (values[0] == 0 || values[0] == 1))
              pipette_return[int(values[0])] = false;
        }
        break;
    case 100: // Absolute inhale fraction: 0 = push position, 1 = full pull.
        if (!EE::read_floats(payload, size, &amount, 1) || amount < 0 || amount > 1) return;
        if (PIPETTE_PISTON_PULL_US < EE_SERVO_MIN_US || PIPETTE_PISTON_PULL_US > EE_SERVO_MAX_US) {
            Serial.println("[EE] Set PIPETTE_PISTON_PULL_US to the measured pull pulse first.");
            return;
        }
        pipette_return[0] = pipette_return[1] = false;
        EE::set_servo(0, PIPETTE_TIP_INSERT_US, PIPETTE_MOVE_SECONDS);
        EE::set_servo(1, PIPETTE_PISTON_PUSH_US +
                      amount * (PIPETTE_PISTON_PULL_US - PIPETTE_PISTON_PUSH_US), PIPETTE_MOVE_SECONDS);
        break;
    case 101: // Blow out, then return to the push position.
        if (size) return;
        EE::set_servo(1, PIPETTE_LIQUID_EXIT_US, PIPETTE_MOVE_SECONDS);
        pipette_return[1] = true;
        pipette_started[1] = millis();
        break;
    case 102: // Eject tip, then restore the tip holder.
        if (size) return;
        EE::set_servo(0, PIPETTE_TIP_EXTRACT_US, PIPETTE_MOVE_SECONDS);
        pipette_return[0] = true;
        pipette_started[0] = millis();
        break;
    case 103:
        if (size) return;
        amount = (EE::adc() >= EE_HOLDER_THRESHOLD) == EE_HOLDER_PRESENT_ABOVE ? 1.0f : 0.0f;
        EE::reply_floats(104, &amount, 1);
        break;
    }
}

static void serial_command(char *line) {
    char *end;
    long code = strtol(line, &end, 10);
    if (end == line || code < 1 || code > 255) return;
    uint8_t packet[EE::MAX_PACKET] = {254, 254, 8, 0, 0, EE::id(), uint8_t(code)};
    size_t payload_size = 0;
    if (code == 1) {
        while (*end == ' ' || *end == '\t') ++end;
        payload_size = strlen(end);
        if (!payload_size || payload_size > EE::MAX_NAME) return;
        memcpy(packet + 7, end, payload_size);
    } else {
        while (*end) {
            while (*end == ' ' || *end == '\t' || *end == '\r') ++end;
            if (!*end) break;
            char *next;
            float value = strtof(end, &next);
            if (next == end || !isfinite(value) || payload_size + 4 > sizeof(packet) - 8) return;
            memcpy(packet + 7 + payload_size, &value, 4);
            payload_size += 4;
            end = next;
        }
    }
    // Dispatch validated USB text without disturbing a partial UART frame.
    EE::local_command(code, packet + 7, payload_size);
}

void setup() {
    Serial.begin(1000000);
    EE::begin(uart_send, pipette_command);
    if (!Uart.begin(uart1, TX_PIN, RX_PIN, EE_UART_BAUD))
        Serial.println(Uart.lastBeginErrorString());
}

void loop() {
    EE::tick();
    // RP2040_UART owns its DMA RX queue; no second uninitialized UartQ is needed.
    uint8_t buffer[64];
    size_t size = Uart.read(buffer, sizeof(buffer));
    for (size_t i = 0; i < size; ++i) EE::receive(buffer[i]);
    for (int num = 0; num < 2; ++num) {
        if (pipette_return[num] && uint32_t(millis() - pipette_started[num]) >=
                uint32_t(PIPETTE_MOVE_SECONDS * 1000)) {
            EE::set_servo(num, num == 0 ? PIPETTE_TIP_INSERT_US : PIPETTE_PISTON_PUSH_US,
                          PIPETTE_MOVE_SECONDS);
            pipette_return[num] = false;
        }
    }
    // Bound USB work so binary UART commands and power deadlines keep running.
    for (int count = 0; count < 64 && Serial.available(); ++count) {
        char value = Serial.read();
        if (value == '\n') {
            serial_line[serial_size] = 0;
            if (!serial_overflow) serial_command(serial_line);
            serial_size = 0;
            serial_overflow = false;
        } else if (value != '\r') {
            if (serial_size < sizeof(serial_line) - 1) serial_line[serial_size++] = value;
            else serial_overflow = true;
        }
    }
}
