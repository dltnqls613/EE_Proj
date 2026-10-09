#ifndef SB_BYTETOOL_H_
#define SB_BYTETOOL_H_

#include<Arduino.h>

namespace SB_ByteTool
{
    unsigned short Substring(byte *component, unsigned short length, byte *result, unsigned short resultlength, unsigned short start, unsigned short count);

    unsigned short Substring(byte *component, unsigned short length, unsigned short start, unsigned short count);

    unsigned short Substring(byte *component, unsigned short length, unsigned short start);

    int IndexOf(byte *component, unsigned short length, byte index);

    unsigned short IndexOfRemove(byte *component, unsigned short length, byte index);

    unsigned short Remove(byte *component, unsigned short length, unsigned short indexPoint);

    unsigned short Copy(byte *target, unsigned short length, byte *return_);

    void charsPrint(byte *component, unsigned short length);

    void Print(byte *component, unsigned short length);
}

#endif