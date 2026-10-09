#include "Scheduler_SB_Manager.h"

const unsigned char Scheduler_SB_Garage_Num = 3;
Scheduler_SB Scheduler_SB_Garage[Scheduler_SB_Garage_Num];
unsigned char Scheduler_SB_Manager::Garage_Stack = 0;
volatile bool Scheduler_SB_Manager::overflowing;
volatile unsigned int Scheduler_SB_Manager::tcnt2;

unsigned long micro_t = 0;
void Scheduler_SB_Manager::idle()
{
    micro_t = time_us_32();
    for (unsigned int i = 0; i < Garage_Stack; i++)
    {
        (Scheduler_SB_Garage[i]).Execute(micro_t);
    }

    // for (unsigned int i = 0; i < Scheduler_SB_Stack-1; i++)
    // {
    //     for (unsigned int j = i+1; j < Scheduler_SB_Stack; j++)
    //     {
    //         if (((*Scheduler_SB_Garage[i]).get_priority() < (*Scheduler_SB_Garage[j]).get_priority()))
    //         {
    //             (*Scheduler_SB_Garage[Scheduler_SB_Stack + 1]) = (*Scheduler_SB_Garage[i]);
    //             (*Scheduler_SB_Garage[i]) = (*Scheduler_SB_Garage[j]);
    //             (*Scheduler_SB_Garage[j]) = (*Scheduler_SB_Garage[Scheduler_SB_Stack + 1]);
    //         }
    //     }
    //     // (*Scheduler_SB_Garage[i]).Execute();
    // }
    // for(unsigned int i = 0; i < Scheduler_SB_Stack; i++)
    // {
    //     if((*Scheduler_SB_Garage[i]).get_available())
    //     {
    //         (*Scheduler_SB_Garage[i]).Execute();
    //         return;

    //     }
    // }
}
void Scheduler_SB_Manager::add(Scheduler_SB SB)
{
    if(Garage_Stack >= Scheduler_SB_Garage_Num)
        return;

    Scheduler_SB_Garage[Garage_Stack++] = SB;

    return;
}

void Scheduler_SB_Manager::remove(Scheduler_SB *SB)
{

    for (unsigned int i = 0; i < Garage_Stack; i++)
    {
        if (&Scheduler_SB_Garage[i] == SB)
        {
            for (unsigned int j = i; j < Garage_Stack - 1; j++)
            {
                Scheduler_SB_Garage[j] = Scheduler_SB_Garage[j + 1];
            }
        }
    }
    Garage_Stack--;
    return;
}