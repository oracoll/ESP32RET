#include <Arduino.h>
#include "can_manager.h"
#include "config.h"
#include "sys_io.h"
#include "gvret_comm.h"
#include "lawicel.h"
#include "sd_logger.h"
#include "display_manager.h"

extern uint32_t lastTxTraffic;
extern uint32_t lastRxTraffic;

CANManager::CANManager()
{
    sendToConsole = true;
}

void CANManager::setup()
{
    if (settings.canSettings[0].enabled)
    {
        CAN.begin((CanBitRate)settings.canSettings[0].nomSpeed);
    }

    busLoad[0].bitsPerQuarter = settings.canSettings[0].nomSpeed / 4;
    busLoad[0].bitsSoFar = 0;
    busLoad[0].busloadPercentage = 0;
    if (busLoad[0].bitsPerQuarter == 0) busLoad[0].bitsPerQuarter = 125000;

    busLoadTimer = millis();
}

void CANManager::addBits(int offset, CAN_FRAME &frame)
{
    if (offset < 0 || offset >= NUM_BUSES) return;
    busLoad[offset].bitsSoFar += 41 + (frame.length * 9);
    if (frame.extended) busLoad[offset].bitsSoFar += 18;
}

void CANManager::sendFrame(CAN_FRAME &frame)
{
    if (!settings.canSettings[0].enabled) return;

    CanMsg msg;
    if (frame.extended) {
        msg = CanMsg(CanExtendedId(frame.id), frame.length, frame.data);
    } else {
        msg = CanMsg(CanStandardId(frame.id), frame.length, frame.data);
    }

    if (CAN.write(msg) > 0) {
        addBits(0, frame);
        lastTxTraffic = millis();
        sdLogger.logFrame(frame, 0, 1); // 1 = TX
        toggleTXLED();
    }
}

void CANManager::displayFrame(CAN_FRAME &frame, int whichBus)
{
    if (settings.enableLawicel && SysSettings.lawicelMode)
    {
        lawicel.sendFrameToBuffer(frame, whichBus);
    }
    else
    {
        if (sendToConsole) serialGVRET.sendFrameToBuffer(frame, whichBus);
    }
}

void CANManager::loop()
{
    CAN_FRAME incoming;

    if (millis() > (busLoadTimer + 250)) {
        busLoadTimer = millis();
        busLoad[0].busloadPercentage = ((busLoad[0].busloadPercentage * 3) + (((busLoad[0].bitsSoFar * 1000) / busLoad[0].bitsPerQuarter) / 10)) / 4;
        if (busLoad[0].busloadPercentage == 0 && busLoad[0].bitsSoFar > 0) busLoad[0].busloadPercentage = 1;
        busLoad[0].bitsPerQuarter = settings.canSettings[0].nomSpeed / 4;
        busLoad[0].bitsSoFar = 0;
    }

    if (settings.canSettings[0].enabled)
    {
        while (CAN.available())
        {
            CanMsg msg = CAN.read();
            incoming.id = msg.id;
            incoming.length = msg.data_length;
            incoming.extended = msg.is_extended ? 1 : 0;
            incoming.rtr = msg.is_rtr ? 1 : 0;
            incoming.bus = 0;
            memcpy(incoming.data, msg.data, msg.data_length);

            addBits(0, incoming);
            displayFrame(incoming, 0);
            displayManager.updateFrame(incoming);
            lastRxTraffic = millis();
            sdLogger.logFrame(incoming, 0, 0); // 0 = RX

            toggleRXLED();
        }
    }
}
