#pragma once
#include <Arduino.h>
#include "config.h"
#include "can_manager.h"

class CommBuffer
{
public:
    CommBuffer();
    size_t numAvailableBytes();
    uint8_t* getBufferedBytes();
    void clearBufferedBytes();
    void sendFrameToBuffer(CAN_FRAME &frame, int whichBus);
    void sendBytesToBuffer(uint8_t *bytes, size_t length);
    void sendByteToBuffer(uint8_t byt);
    void sendString(String str);
    void sendCharString(char *str);

protected:
    uint8_t transmitBuffer[SER_BUFF_SIZE];
    int transmitBufferLength;
};
