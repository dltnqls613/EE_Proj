#include "SB_ByteTool.h"

unsigned short SB_ByteTool::Substring(byte *component, unsigned short length, byte *result, unsigned short resultlength, unsigned short start, unsigned short count)
{
    // Serial.print(_length);
    // Serial.print(" ");
    // Serial.print(start);
    // Serial.print(" ");
    // Serial.print(end);
    // Serial.print("\n");
    if (count == 0)
        return 0;
    else if (start + count > length)
        return 0;

    for (int i = 0; i < count; i++)
    {
        if(i < resultlength)
        {
            result[i] = component[start + i];
        }
        else
            break;
    }
    return count;
}

unsigned short SB_ByteTool::Substring(byte *component, unsigned short length, unsigned short start, unsigned short count)
{
    // Serial.print(_length);
    // Serial.print(" ");
    // Serial.print(start);
    // Serial.print(" ");
    // Serial.print(end);
    // Serial.print("\n");
    if (count == 0)
        return 0;
    else if (start + count > length)
        return 0;

    for (int i = 0; i < count; i++)
    {
        component[i] = component[start + i];
    }
    return count;
}

unsigned short SB_ByteTool::Substring(byte *component, unsigned short length, unsigned short start)
{
    if (start >= length)
        return 0;

    for (int i = start; i < length; i++)
        component[i - start] = component[i];

    return length - start;
}

int SB_ByteTool::IndexOf(byte *component, unsigned short length, byte index)
{
    for (int i = 0; i < length; i++)
    {
        if (component[i] == index)
        {
            return i;
        }
    }
    return -1;
}

unsigned short SB_ByteTool::IndexOfRemove(byte *component, unsigned short length, byte index)
{
    for (int i = 0; i < length; i++)
    {
        if (component[i] == index)
        {
            if (i < length - 1)
            {
                for (int j = i; j < length - 1; j++)
                {
                    component[j] = component[j + 1];
                }
            }
            length--;
        }
    }
    return length;
}

unsigned short SB_ByteTool::Remove(byte *component, unsigned short length, unsigned short indexPoint)
{
    if (indexPoint > length)
        return length;

    for (int i = indexPoint; i < length - 1; i++)
    {
        component[i] = component[i + 1];
    }

    return length - 1;
}

unsigned short SB_ByteTool::Copy(byte *target, unsigned short length, byte *return_)
{
    for(int i=0; i<length; i++)
        return_[i] = target[i];
    
    return length;
}

void SB_ByteTool::charsPrint(byte *component, unsigned short length)
{
    for (int i = 0; i < length; i++)
    {
        Serial.print((char)component[i]);
    }
    Serial.print("\n");
}

void SB_ByteTool::Print(byte *component, unsigned short length)
{
    for (int i = 0; i < length; i++)
    {
        Serial.print(component[i]);
        Serial.print(" ");
    }
    Serial.print("\n");
}