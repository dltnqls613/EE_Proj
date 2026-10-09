#include "Scheduler_SB.h"

Scheduler_SB::Scheduler_SB()
{
    micros_period = 0;
}
Scheduler_SB::Scheduler_SB(void (*copy_func)(void), unsigned long _micros_period)
{
    func.v = copy_func;
    set_period_time(_micros_period);
    func_inf = 0;
}

Scheduler_SB::Scheduler_SB(void (*copy_func)(void), int _priority, unsigned long _micros_period)
{
    func.v = copy_func;
    set_period_time(_micros_period);
    set_priority(_priority);
    func_inf = 0;
}

unsigned long Scheduler_SB::get_execute_time()
{
    return execute_time;

}

bool Scheduler_SB::get_available()
{
    unsigned long t = time_us_32();
    unsigned long dt = t > ex_time ? t - ex_time : (4294967295 - ex_time + t);

    return dt >= micros_period ? true : false;
}

int Scheduler_SB::get_priority()
{
    return Priority;
}

void Scheduler_SB::set_period_time(unsigned long _micros_period)
{
    micros_period = _micros_period;
}

void Scheduler_SB::set_priority(int _p)
{
    Priority = _p;
}

void Scheduler_SB::Execute(unsigned long t)
{
    if(micros_period == 0) { return; }
    dt = t - ex_time;//t > ex_time ? t - ex_time : (4294967295 - ex_time + t);

    if (dt >= micros_period)
    {
        ex_time = t;
        func.v();
        // execute_time = time_us_32() - t;
    }
    return;
}

bool Scheduler_SB::operator==(Scheduler_SB scheduler_sb)
{
    return (scheduler_sb.func.v == this->func.v)
    && (scheduler_sb.micros_period == this->micros_period);

}
