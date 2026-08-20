#pragma once
#include "config.h"

// Compatibility structures matching CAN_FRAME layout
typedef struct
{
    uint32_t id;
    uint8_t length;
    uint8_t rtr;
    uint8_t extended;
    uint8_t bus;
    uint8_t data[8];
} CAN_FRAME;

typedef struct
{
    uint32_t id;
    uint8_t length;
    uint8_t bus;
    uint8_t data[64];
    uint8_t extended;
} CAN_FRAME_FD;

typedef struct {
    uint32_t bitsPerQuarter;
    uint32_t bitsSoFar;
    uint8_t busloadPercentage;
} BUSLOAD;

class CANManager
{
public:
    CANManager();
    void addBits(int offset, CAN_FRAME &frame);
    void sendFrame(CAN_FRAME &frame);
    void displayFrame(CAN_FRAME &frame, int whichBus);
    void loop();
    void setup();
    void setSendToConsole(bool state) { sendToConsole = state; }

private:
    BUSLOAD busLoad[NUM_BUSES];
    uint32_t busLoadTimer;
    bool sendToConsole;
};
