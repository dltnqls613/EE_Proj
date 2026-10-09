#ifndef Scheduler_SB_H
#define Scheduler_SB_H

#include <Arduino.h>

class Scheduler_SB
{
private:
    bool next_available = false;
    bool prev_available = false;

    unsigned int func_inf = 0;
    unsigned long execute_time;

public:
    float ref_var1, ref_var2, ref_var3, ref_var4;
    unsigned long micros_period;
    unsigned long dt;
    unsigned long ex_time = 0;
    int Priority = 0;

    union func {
        void (*v)(void);
        // void (*f1)(float);
        // void (*f2)(float, float);
        // void (*f3)(float, float, float);
        // void (*f4)(float, float, float, float);
    } func;

    Scheduler_SB();
    Scheduler_SB(void (*)(void), unsigned long);
    Scheduler_SB(void (*)(void), int, unsigned long);
    // Scheduler_SB(void (*)(float),unsigned long);
    // Scheduler_SB(void (*)(float,float),unsigned long);
    // Scheduler_SB(void (*)(float,float,float),unsigned long);
    // Scheduler_SB(void (*)(float,float,float,float),unsigned long);

    unsigned long get_execute_time();
    int get_priority();
    bool get_available();
    void set_priority(int);
    void set_period_time(unsigned long);
    void Execute(unsigned long);

    bool operator==(Scheduler_SB sechduler_sb);
};

#endif
