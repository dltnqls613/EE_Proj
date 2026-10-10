// Executes the production parser, hardware commands and pipette state machine.
#include "../common/EE_Standard/src/EE_Standard.cpp"
#include "../EE_Pipette/RP2040/src/main.cpp"
#include <cassert>
#include <iostream>
#include <limits>

std::vector<uint8_t> packet(int code, std::vector<float> values = {}, int from = 0, int to = 1) {
    std::vector<uint8_t> out = {254,254,uint8_t(8+4*values.size()),0,uint8_t(from),uint8_t(to),uint8_t(code)};
    for (float value : values) {
        uint8_t raw[4]; memcpy(raw,&value,4); out.insert(out.end(),raw,raw+4);
    }
    uint8_t crc = 0; for (auto value : out) crc += value; out.push_back(crc); return out;
}
void feed(const std::vector<uint8_t> &bytes) { for (auto value : bytes) EE::receive(value); }
void command(int code, std::vector<float> args = {}) { feed(packet(code,args)); }
void advance(uint32_t milliseconds) { test_ms += milliseconds; loop(); }
void check_kept_power(int pin) {
    for (const auto &write : test_pin_writes) assert(write.pin != pin || write.value != LOW);
}
float reply_value(int index = 0) { float value; memcpy(&value,Uart.output.back().data()+7+index*4,4); return value; }
void check_reply(int code) {
    const auto &p = Uart.output.back();
    assert(p[4] == 1 && p[5] == 0 && p[6] == code && p[2] == p.size());
    uint8_t crc = 0; for (size_t i=0;i<p.size()-1;++i) crc += p[i]; assert(crc == p.back());
}
int main() {
    setup();
    assert(EE::id() == 1 && !EE::set_id(0) && !EE::set_id(255));
    assert(test_pins[Servo0_EN_PIN] == 0 && test_pins[Servo1_EN_PIN] == 0);
    feed(packet(2,{},0,255)); check_reply(3);
    assert(std::string(Uart.output.back().begin()+7,Uart.output.back().end()-1) == "EE_Pipette");
    size_t replies = Uart.output.size();
    feed(packet(2,{},1,1)); feed(packet(2,{},0,0)); feed(packet(2,{},0,2));
    assert(Uart.output.size() == replies);
    auto bad = packet(4,{0,2380,1}); bad.back() ^= 1; feed(bad);
    assert(test_pins[Servo0_EN_PIN] == 0);
    feed({0,0,254,254,255,255});
    command(4,{0,680,.1f});
    assert(test_servo_us[Servo0_PIN] == 680 && test_pins[Servo0_EN_PIN] == 1);
    command(4,{0,2600,.1f}); command(4,{.5f,2380,.1f});
    command(4,{0,std::numeric_limits<float>::quiet_NaN(),.1f});
    assert(test_servo_us[Servo0_PIN] == 680);
    test_ms = 101; loop(); assert(test_pins[Servo0_EN_PIN] == 0);
    command(5); check_reply(6); assert(reply_value() == 680 && reply_value(1) == 2200);
    command(7,{1,-.5f,.1f}); assert(test_pins[MOTOR_IN3_PIN] == 0 && test_pins[MOTOR_IN4_PIN] == 128);
    command(8); check_reply(9); assert(reply_value(1) == -.5f);
    test_ms = 202; loop(); command(8); assert(reply_value(1) == 0);
    command(16,{.5f,.1f}); command(17); check_reply(18); assert(reply_value() == .5f);
    test_ms = 303; loop(); command(17); assert(reply_value() == 0);
    command(10); check_reply(11); assert(isnan(reply_value()));
    command(12); check_reply(13); assert(isnan(reply_value()));
    test_adc[ADC_PIN] = 3000; command(14); check_reply(15); assert(reply_value() == 3000);
    for (int raw : {0, 2047, 2048, 4095}) {
        test_adc[ADC_PIN] = raw;
        Serial.output.clear();
        command(105); check_reply(106);
        int present = raw >= 2048;
        assert(reply_value() == present);
        assert(Serial.output == "[EE] holder ADC=" + std::to_string(raw) +
                                " present=" + std::to_string(present) + "\n");
    }
    replies = Uart.output.size(); Serial.output.clear();
    command(105,{1}); assert(Uart.output.size() == replies && Serial.output.empty());
    command(20,{.1f}); command(21,{.01f}); command(22,{.2f}); command(19,{50});
    command(23); check_reply(24);
    assert(reply_value() == 50 && reply_value(1) == .1f && reply_value(2) == .01f && reply_value(3) == .2f);
    test_ms = 500; loop(); assert(test_pins[HEATER_PWM_PIN] == 0); // Uncalibrated sensor.
    // 101: two seconds to travel, then three seconds held at blow-out, no return.
    command(101); assert(test_servo_us[Servo1_PIN] == 2500);
    advance(2000); assert(test_pins[Servo1_EN_PIN] == HIGH);
    advance(2999); assert(test_pins[Servo1_EN_PIN] == HIGH && test_servo_us[Servo1_PIN] == 2500);
    advance(1); assert(test_pins[Servo1_EN_PIN] == LOW);
    advance(3000); assert(test_servo_us[Servo1_PIN] == 2500);
    // 104: two seconds at each target; no power interruption at the return.
    command(104); assert(test_servo_us[Servo0_PIN] == 2380);
    advance(1999); assert(test_pins[Servo0_EN_PIN] == HIGH && test_servo_us[Servo0_PIN] == 2380);
    test_pin_writes.clear(); advance(1);
    assert(test_servo_us[Servo0_PIN] == 680 && test_pins[Servo0_EN_PIN] == HIGH);
    check_kept_power(Servo0_EN_PIN);
    advance(1999); assert(test_pins[Servo0_EN_PIN] == HIGH);
    advance(1); assert(test_pins[Servo0_EN_PIN] == LOW);
    command(104); command(4,{0,1000,3}); advance(2001);
    assert(test_servo_us[Servo0_PIN] == 1000 && test_pins[Servo0_EN_PIN] == HIGH);
    advance(999); assert(test_pins[Servo0_EN_PIN] == LOW);
    // 102: replace a pending removal return with a real indefinite hold.
    command(104); advance(1000); command(102);
    assert(test_servo_us[Servo0_PIN] == 2380 && test_pins[Servo0_EN_PIN] == HIGH);
    test_pin_writes.clear(); advance(3601000); // Beyond the maximum standard timed output.
    assert(test_servo_us[Servo0_PIN] == 2380 && test_pins[Servo0_EN_PIN] == HIGH);
    check_kept_power(Servo0_EN_PIN);
    command(105); check_reply(106); command(101); advance(5000);
    assert(test_pins[Servo0_EN_PIN] == HIGH && test_pins[Servo1_EN_PIN] == LOW);
    command(4,{1,1500,.1f}); advance(100);
    command(4,{0,2600,1}); command(102,{1}); command(103,{1}); advance(1000);
    assert(test_servo_us[Servo0_PIN] == 2380 && test_pins[Servo0_EN_PIN] == HIGH);
    command(4,{0,2380,0}); assert(test_pins[Servo0_EN_PIN] == LOW);
    test_ms = 0xFFFFFFF0; command(102); advance(1000);
    assert(test_pins[Servo0_EN_PIN] == HIGH); // Hold is not affected by millis() wrap.
    command(4,{0,1500,.25f}); advance(249);
    assert(test_servo_us[Servo0_PIN] == 1500 && test_pins[Servo0_EN_PIN] == HIGH);
    advance(1); assert(test_pins[Servo0_EN_PIN] == LOW);
#if PIPETTE_PISTON_PULL_US >= 500
    // 100: amount 0/0.5/1 presses to 500/1350/2200, then returns to 500.
    for (float amount : {0.0f, .5f, 1.0f}) {
        command(100,{amount});
        int target = lroundf(PIPETTE_PISTON_PULL_US + amount * (2200-PIPETTE_PISTON_PULL_US));
        assert(test_servo_us[Servo1_PIN] == target && test_servo_us[Servo0_PIN] == 680);
        command(5); check_reply(6); assert(reply_value(1) == target); // Standard query during motion.
        advance(2999); assert(test_servo_us[Servo1_PIN] == target && test_pins[Servo1_EN_PIN] == HIGH);
        test_pin_writes.clear(); advance(1);
        assert(test_servo_us[Servo1_PIN] == PIPETTE_PISTON_PULL_US && test_pins[Servo1_EN_PIN] == HIGH);
        check_kept_power(Servo1_EN_PIN);
        advance(2999); assert(test_pins[Servo1_EN_PIN] == HIGH);
        advance(1); assert(test_pins[Servo1_EN_PIN] == LOW && test_pins[Servo0_EN_PIN] == LOW);
    }
    command(100,{1}); command(100,{-1}); command(100,{1.1f});
    command(100,{std::numeric_limits<float>::quiet_NaN()});
    advance(3000); assert(test_servo_us[Servo1_PIN] == PIPETTE_PISTON_PULL_US);
    command(100,{1}); command(4,{1,1500,4}); advance(3000);
    assert(test_servo_us[Servo1_PIN] == 1500); // Manual standard output cancels aspiration return.
    command(100,{1}); advance(1000); command(101); advance(2000);
    assert(test_servo_us[Servo1_PIN] == 2500 && test_pins[Servo1_EN_PIN] == HIGH);
    advance(3000); assert(test_servo_us[Servo1_PIN] == 2500 && test_pins[Servo1_EN_PIN] == LOW);
    // Independent tip and piston sequences, including a millis() wrap.
    test_ms = 0xFFFFFFF0;
    command(100,{1}); advance(1000); command(104); advance(2000);
    assert(test_servo_us[Servo0_PIN] == 680 && test_servo_us[Servo1_PIN] == PIPETTE_PISTON_PULL_US);
    advance(2000); assert(test_pins[Servo0_EN_PIN] == LOW && test_pins[Servo1_EN_PIN] == HIGH);
    advance(1000); assert(test_pins[Servo1_EN_PIN] == LOW);
    // 103: stand supersedes both deferred strokes and switches both channels off at 1 s.
    command(100,{1}); command(104); advance(500); command(103);
    assert(test_servo_us[Servo0_PIN] == 680 && test_servo_us[Servo1_PIN] == 500);
    advance(999); assert(test_pins[Servo0_EN_PIN] == HIGH && test_pins[Servo1_EN_PIN] == HIGH);
    advance(1); assert(test_pins[Servo0_EN_PIN] == LOW && test_pins[Servo1_EN_PIN] == LOW);
    advance(6000);
    assert(test_servo_us[Servo0_PIN] == 680 && test_servo_us[Servo1_PIN] == 500);
    assert(test_pins[Servo0_EN_PIN] == LOW && test_pins[Servo1_EN_PIN] == LOW);
    command(102); Serial.input = "103\n"; loop(); advance(1000);
    assert(test_pins[Servo0_EN_PIN] == LOW && test_pins[Servo1_EN_PIN] == LOW);
    assert(test_servo_us[Servo0_PIN] == 680 && test_servo_us[Servo1_PIN] == 500);
#else
    int before = test_servo_us[Servo1_PIN]; command(100,{1}); assert(test_servo_us[Servo1_PIN] == before);
#endif
    command(4,{0,680,0}); assert(test_pins[Servo0_EN_PIN] == 0);
    const uint8_t name[] = "Long EE pipette name";
    EE::local_command(1,name,sizeof(name)-1); check_reply(3); assert(EEPROM.commits == 1);
    EE::local_command(1,name,sizeof(name)-1); assert(EEPROM.commits == 1);
    // USB command must not corrupt a fragmented UART frame.
    auto query = packet(5); feed({query.begin(),query.begin()+4});
    Serial.input = "14\n"; loop(); feed({query.begin()+4,query.end()}); check_reply(6);
    // Every standard request is also reachable through the model's USB text parser.
    struct StandardRequest { const char *text; int reply; };
    const StandardRequest standard[] = {
        {"1 USB Pipette\n",3}, {"2\n",3}, {"4 1 1500 0.1\n",0}, {"5\n",6},
        {"7 0 0.25 0.1\n",0}, {"8\n",9}, {"10\n",11}, {"12\n",13}, {"14\n",15},
        {"16 0.5 0.1\n",0}, {"17\n",18}, {"19 40\n",0}, {"20 1\n",0},
        {"21 2\n",0}, {"22 3\n",0}, {"23\n",24}
    };
    for (const auto &request : standard) {
        replies = Uart.output.size();
        Serial.input = request.text; loop();
        assert(Uart.output.size() == replies + (request.reply != 0));
        if (request.reply) check_reply(request.reply);
        if (request.reply == 6) assert(reply_value(1) == 1500);
        if (request.reply == 9) assert(reply_value() == .25f);
        if (request.reply == 18) assert(reply_value() == .5f);
        if (request.reply == 24)
            assert(reply_value() == 40 && reply_value(1) == 1 && reply_value(2) == 2 && reply_value(3) == 3);
    }
    // uint32 timer wrap.
    test_ms = 0xFFFFFFF0; command(7,{0,1,.1f}); test_ms = 100; loop();
    command(8); assert(reply_value() == 0);
    std::cout << "Firmware protocol, timers, sensors and pipette checks passed\n";
}
