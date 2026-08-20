#include "commbuffer.h"
#include "Logger.h"

CommBuffer::CommBuffer()
{
    transmitBufferLength = 0;
}

size_t CommBuffer::numAvailableBytes()
{
    return transmitBufferLength;
}

void CommBuffer::clearBufferedBytes()
{
    transmitBufferLength = 0;
}

uint8_t* CommBuffer::getBufferedBytes()
{
    return transmitBuffer;
}

void CommBuffer::sendBytesToBuffer(uint8_t *bytes, size_t length)
{
    if (transmitBufferLength + length < SER_BUFF_SIZE) {
        memcpy(&transmitBuffer[transmitBufferLength], bytes, length);
        transmitBufferLength += length;
    }
}

void CommBuffer::sendByteToBuffer(uint8_t byt)
{
    if (transmitBufferLength < SER_BUFF_SIZE) {
        transmitBuffer[transmitBufferLength++] = byt;
    }
}

void CommBuffer::sendString(String str)
{
    sendBytesToBuffer((uint8_t*)str.c_str(), str.length());
}

void CommBuffer::sendCharString(char *str)
{
    sendBytesToBuffer((uint8_t*)str, strlen(str));
}

void CommBuffer::sendFrameToBuffer(CAN_FRAME &frame, int whichBus)
{
    if (settings.useBinarySerialComm) {
        if (transmitBufferLength + 12 + frame.length >= SER_BUFF_SIZE) return;

        int startPos = transmitBufferLength;
        uint32_t id = frame.id;
        if (frame.extended) id |= (1UL << 31);

        transmitBuffer[transmitBufferLength++] = 0xF1;
        transmitBuffer[transmitBufferLength++] = 0; // 0 = CAN frame
        uint32_t now = micros();
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now & 0xFF);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 8);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 16);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 24);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(id & 0xFF);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(id >> 8);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(id >> 16);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(id >> 24);
        transmitBuffer[transmitBufferLength++] = frame.length + (uint8_t)(whichBus << 4);

        for (int c = 0; c < frame.length; c++) {
            transmitBuffer[transmitBufferLength++] = frame.data[c];
        }

        uint8_t checksum = 0;
        for (int i = startPos; i < transmitBufferLength; i++) {
            checksum ^= transmitBuffer[i];
        }
        transmitBuffer[transmitBufferLength++] = checksum;
    } else {
        char buff[128];
        int len = sprintf(buff, "%lu - %lX %s %i %i",
                          (unsigned long)micros(),
                          (unsigned long)frame.id,
                          frame.extended ? "X" : "S",
                          whichBus,
                          frame.length);
        sendCharString(buff);
        for (int c = 0; c < frame.length; c++) {
            sprintf(buff, " %X", frame.data[c]);
            sendCharString(buff);
        }
        sendCharString("\r\n");
    }
}
