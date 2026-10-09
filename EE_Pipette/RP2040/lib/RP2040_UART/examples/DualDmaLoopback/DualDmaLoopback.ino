#include <Arduino.h>
#include <RP2040_UART.h>

// 교차 연결:
// GPIO12 (UART0 TX) -> GPIO5  (UART1 RX)
// GPIO4  (UART1 TX) -> GPIO13 (UART0 RX)

static constexpr uint32_t BAUD = 1500000;
static constexpr size_t FRAME_SIZE = 64;

RP2040_UART port_a;
RP2040_UART port_b;

struct Receiver {
    uint8_t frame[FRAME_SIZE];
    size_t index = 0;
    uint32_t expected_sequence = 0;
    uint32_t good = 0;
    uint32_t bad = 0;
};

Receiver rx_a;
Receiver rx_b;
uint32_t tx_sequence = 0;
uint32_t previous_report_ms = 0;

static uint8_t checksum(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum = static_cast<uint8_t>(sum + data[i]);
    }
    return sum;
}

static void makeFrame(uint8_t* frame, uint8_t source, uint32_t sequence) {
    frame[0] = 0xa5;
    frame[1] = source;
    frame[2] = static_cast<uint8_t>(sequence);
    frame[3] = static_cast<uint8_t>(sequence >> 8);
    frame[4] = static_cast<uint8_t>(sequence >> 16);
    frame[5] = static_cast<uint8_t>(sequence >> 24);

    for (size_t i = 6; i < FRAME_SIZE - 1; ++i) {
        frame[i] = static_cast<uint8_t>(sequence + source * 37u + i * 13u);
    }
    frame[FRAME_SIZE - 1] = checksum(frame, FRAME_SIZE - 1);
}

static void validateFrame(Receiver& rx, uint8_t expected_source) {
    const uint8_t* const frame = rx.frame;
    const uint32_t sequence =
        static_cast<uint32_t>(frame[2]) |
        (static_cast<uint32_t>(frame[3]) << 8) |
        (static_cast<uint32_t>(frame[4]) << 16) |
        (static_cast<uint32_t>(frame[5]) << 24);

    bool valid = frame[0] == 0xa5 &&
                 frame[1] == expected_source &&
                 frame[FRAME_SIZE - 1] == checksum(frame, FRAME_SIZE - 1);

    for (size_t i = 6; valid && i < FRAME_SIZE - 1; ++i) {
        const uint8_t expected =
            static_cast<uint8_t>(sequence + expected_source * 37u + i * 13u);
        valid = frame[i] == expected;
    }

    if (valid && sequence == rx.expected_sequence) {
        ++rx.good;
    } else {
        ++rx.bad;
    }
    rx.expected_sequence = sequence + 1u;
}

static void drain(RP2040_UART& port, Receiver& rx, uint8_t expected_source) {
    while (port.available() > 0) {
        const int value = port.read();
        if (value < 0) {
            break;
        }
        rx.frame[rx.index++] = static_cast<uint8_t>(value);
        if (rx.index == FRAME_SIZE) {
            validateFrame(rx, expected_source);
            rx.index = 0;
        }
    }
}

void setup() {
    Serial.begin(115200);

    const bool a_ok = port_a.begin(uart0, 12, 13, BAUD);
    const bool b_ok = port_b.begin(uart1, 4, 5, BAUD);
    if (!a_ok || !b_ok) {
        Serial.print("begin failed: A=");
        Serial.print(port_a.lastBeginErrorString());
        Serial.print(" B=");
        Serial.println(port_b.lastBeginErrorString());
        while (true) delay(1000);
    }
}

void loop() {
    drain(port_a, rx_a, 1); // UART1 TX에서 온 프레임
    drain(port_b, rx_b, 0); // UART0 TX에서 온 프레임

    if (port_a.txSpace() >= FRAME_SIZE && port_b.txSpace() >= FRAME_SIZE) {
        uint8_t frame_a[FRAME_SIZE];
        uint8_t frame_b[FRAME_SIZE];
        makeFrame(frame_a, 0, tx_sequence);
        makeFrame(frame_b, 1, tx_sequence);
        port_a.write(frame_a, sizeof(frame_a));
        port_b.write(frame_b, sizeof(frame_b));
        ++tx_sequence;
    }

    const uint32_t now = millis();
    if (now - previous_report_ms >= 1000u) {
        previous_report_ms = now;
        const RP2040_UART::Stats a = port_a.getStats();
        const RP2040_UART::Stats b = port_b.getStats();

        Serial.print("frames A good/bad=");
        Serial.print(rx_a.good);
        Serial.print('/');
        Serial.print(rx_a.bad);
        Serial.print(" B good/bad=");
        Serial.print(rx_b.good);
        Serial.print('/');
        Serial.print(rx_b.bad);
        Serial.print(" dropped A/B=");
        Serial.print(a.rx_dropped);
        Serial.print('/');
        Serial.print(b.rx_dropped);
        Serial.print(" uart_error A/B=");
        Serial.print(a.uart_errors);
        Serial.print('/');
        Serial.println(b.uart_errors);
    }
}
