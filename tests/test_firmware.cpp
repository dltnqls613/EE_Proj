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
    command(103); check_reply(104); assert(reply_value() == 1);
    test_adc[ADC_PIN] = 100; command(103); assert(reply_value() == 0);
    command(20,{.1f}); command(21,{.01f}); command(22,{.2f}); command(19,{50});
    command(23); check_reply(24); assert(reply_value() == 50 && reply_value(1) == .1f);
    test_ms = 500; loop(); assert(test_pins[HEATER_PWM_PIN] == 0); // Uncalibrated sensor.
    command(101); assert(test_servo_us[Servo1_PIN] == 2500);
    test_ms += 600; loop(); assert(test_servo_us[Servo1_PIN] == 2200);
    command(102); assert(test_servo_us[Servo0_PIN] == 2380);
    command(4,{0,1000,1}); test_ms += 700; loop();
    assert(test_servo_us[Servo0_PIN] == 1000); // Old tip-return must not overwrite manual servo command.
    command(102); test_ms += 600; loop(); assert(test_servo_us[Servo0_PIN] == 680);
#if PIPETTE_PISTON_PULL_US >= 500
    command(100,{0}); assert(test_servo_us[Servo1_PIN] == 2200);
    command(100,{.5f}); assert(test_servo_us[Servo1_PIN] == (2200+PIPETTE_PISTON_PULL_US)/2);
    command(100,{1}); assert(test_servo_us[Servo1_PIN] == PIPETTE_PISTON_PULL_US);
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
    // uint32 timer wrap.
    test_ms = 0xFFFFFFF0; command(7,{0,1,.1f}); test_ms = 100; loop();
    command(8); assert(reply_value() == 0);
    std::cout << "Firmware protocol, timers, sensors and pipette checks passed\n";
}
