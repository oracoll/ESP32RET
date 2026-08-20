#include "gvret_comm.h"
#include "SerialConsole.h"
#include "config.h"
#include "can_manager.h"
#include "sys_io.h"

GVRET_Comm_Handler::GVRET_Comm_Handler()
{
    step = 0;
    state = IDLE;
}

extern uint32_t lastHostActivity;

void GVRET_Comm_Handler::processIncomingByte(uint8_t in_byte)
{
    lastHostActivity = millis();
    uint32_t busSpeed = 0;
    uint32_t now = micros();

    uint8_t temp8;
    int startIdx;

    switch (state) {
    case IDLE:
        if(in_byte == 0xF1)
        {
            state = GET_COMMAND;
        }
        else if(in_byte == 0xE7)
        {
            settings.useBinarySerialComm = true;
            SysSettings.lawicelMode = false;
        }
        else
        {
            console.rcvCharacter((uint8_t) in_byte);
        }
        break;
    case GET_COMMAND:
        switch(in_byte)
        {
        case PROTO_BUILD_CAN_FRAME:
            state = BUILD_CAN_FRAME;
            buff[0] = 0xF1;
            step = 0;
            break;
        case PROTO_TIME_SYNC:
            state = TIME_SYNC;
            step = 0;
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 1; // time sync
            transmitBuffer[transmitBufferLength++] = (uint8_t) (now & 0xFF);
            transmitBuffer[transmitBufferLength++] = (uint8_t) (now >> 8);
            transmitBuffer[transmitBufferLength++] = (uint8_t) (now >> 16);
            transmitBuffer[transmitBufferLength++] = (uint8_t) (now >> 24);
            break;
        case PROTO_DIG_INPUTS:
            startIdx = transmitBufferLength;
            temp8 = (digitalRead(BTN_LOG_PIN) == LOW ? 1 : 0) | ((digitalRead(BTN_REPLAY_PIN) == LOW ? 1 : 0) << 1);
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 2;
            transmitBuffer[transmitBufferLength++] = temp8;
            temp8 = checksumCalc(&transmitBuffer[startIdx], transmitBufferLength - startIdx);
            transmitBuffer[transmitBufferLength++] = temp8;
            state = IDLE;
            break;
        case PROTO_ANA_INPUTS:
            startIdx = transmitBufferLength;
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 3;
            for (int a = 0; a < 7; a++) {
                transmitBuffer[transmitBufferLength++] = 0;
                transmitBuffer[transmitBufferLength++] = 0;
            }
            temp8 = checksumCalc(&transmitBuffer[startIdx], transmitBufferLength - startIdx);
            transmitBuffer[transmitBufferLength++] = temp8;
            state = IDLE;
            break;
        case PROTO_SET_DIG_OUT:
            state = SET_DIG_OUTPUTS;
            buff[0] = 0xF1;
            break;
        case PROTO_SETUP_CANBUS:
            state = SETUP_CANBUS;
            step = 0;
            buff[0] = 0xF1;
            break;
        case PROTO_GET_CANBUS_PARAMS:
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 6;
            transmitBuffer[transmitBufferLength++] = settings.canSettings[0].enabled + ((unsigned char) settings.canSettings[0].listenOnly << 4);
            transmitBuffer[transmitBufferLength++] = settings.canSettings[0].nomSpeed;
            transmitBuffer[transmitBufferLength++] = settings.canSettings[0].nomSpeed >> 8;
            transmitBuffer[transmitBufferLength++] = settings.canSettings[0].nomSpeed >> 16;
            transmitBuffer[transmitBufferLength++] = settings.canSettings[0].nomSpeed >> 24;
            transmitBuffer[transmitBufferLength++] = 0; // Bus 2 disabled
            transmitBuffer[transmitBufferLength++] = 0;
            transmitBuffer[transmitBufferLength++] = 0;
            transmitBuffer[transmitBufferLength++] = 0;
            transmitBuffer[transmitBufferLength++] = 0;
            state = IDLE;
            break;
        case PROTO_GET_DEV_INFO:
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 7;
            transmitBuffer[transmitBufferLength++] = CFG_BUILD_NUM & 0xFF;
            transmitBuffer[transmitBufferLength++] = (CFG_BUILD_NUM >> 8);
            transmitBuffer[transmitBufferLength++] = 0x20;
            transmitBuffer[transmitBufferLength++] = 0;
            transmitBuffer[transmitBufferLength++] = 0;
            transmitBuffer[transmitBufferLength++] = 0;
            state = IDLE;
            break;
        case PROTO_SET_SW_MODE:
            buff[0] = 0xF1;
            state = SET_SINGLEWIRE_MODE;
            step = 0;
            break;
        case PROTO_KEEPALIVE:
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 0x09;
            transmitBuffer[transmitBufferLength++] = 0xDE;
            transmitBuffer[transmitBufferLength++] = 0xAD;
            state = IDLE;
            break;
        case PROTO_SET_SYSTYPE:
            buff[0] = 0xF1;
            state = SET_SYSTYPE;
            step = 0;
            break;
        case PROTO_ECHO_CAN_FRAME:
            state = ECHO_CAN_FRAME;
            buff[0] = 0xF1;
            step = 0;
            break;
        case PROTO_GET_NUMBUSES:
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 12;
            transmitBuffer[transmitBufferLength++] = 1;
            state = IDLE;
            break;
        case PROTO_GET_EXT_BUSES:
            transmitBuffer[transmitBufferLength++] = 0xF1;
            transmitBuffer[transmitBufferLength++] = 13;
            for (int u = 2; u < 17; u++) transmitBuffer[transmitBufferLength++] = 0;
            step = 0;
            state = IDLE;
            break;
        case PROTO_SET_EXT_BUSES:
            state = SETUP_EXT_BUSES;
            step = 0;
            buff[0] = 0xF1;
            break;
        }
        break;
    case BUILD_CAN_FRAME:
        buff[1 + step] = in_byte;
        switch(step)
        {
        case 0: build_out_frame.id = in_byte; break;
        case 1: build_out_frame.id |= in_byte << 8; break;
        case 2: build_out_frame.id |= in_byte << 16; break;
        case 3:
            build_out_frame.id |= in_byte << 24;
            if(build_out_frame.id & (1UL << 31)) {
                build_out_frame.id &= 0x7FFFFFFF;
                build_out_frame.extended = true;
            } else build_out_frame.extended = false;
            break;
        case 4: out_bus = in_byte & 3; break;
        case 5:
            build_out_frame.length = in_byte & 0xF;
            if(build_out_frame.length > 8) build_out_frame.length = 8;
            break;
        default:
            if(step < build_out_frame.length + 6) {
                build_out_frame.data[step - 6] = in_byte;
            } else {
                state = IDLE;
                build_out_frame.rtr = 0;
                canManager.sendFrame(build_out_frame);
            }
            break;
        }
        step++;
        break;
    case TIME_SYNC: state = IDLE; break;
    case GET_DIG_INPUTS: break;
    case GET_ANALOG_INPUTS: break;
    case SET_DIG_OUTPUTS: state = IDLE; break;
    case SETUP_CANBUS:
        switch(step)
        {
        case 0: build_int = in_byte; break;
        case 1: build_int |= in_byte << 8; break;
        case 2: build_int |= in_byte << 16; break;
        case 3:
            build_int |= in_byte << 24;
            busSpeed = build_int & 0xFFFFF;
            if(busSpeed > 1000000) busSpeed = 1000000;
            if(build_int > 0) {
                settings.canSettings[0].enabled = true;
                settings.canSettings[0].nomSpeed = busSpeed;
            } else {
                settings.canSettings[0].enabled = false;
            }
            break;
        case 4: case 5: case 6: case 7:
            if (step == 7) {
                state = IDLE;
                saveSettings();
            }
            break;
        }
        step++;
        break;
    case GET_CANBUS_PARAMS: break;
    case GET_DEVICE_INFO: break;
    case SET_SINGLEWIRE_MODE: state = IDLE; break;
    case SET_SYSTYPE:
        settings.systemType = in_byte;
        saveSettings();
        state = IDLE;
        break;
    case ECHO_CAN_FRAME:
        buff[1 + step] = in_byte;
        switch(step)
        {
        case 0: build_out_frame.id = in_byte; break;
        case 1: build_out_frame.id |= in_byte << 8; break;
        case 2: build_out_frame.id |= in_byte << 16; break;
        case 3:
            build_out_frame.id |= in_byte << 24;
            if(build_out_frame.id & (1UL << 31)) {
                build_out_frame.id &= 0x7FFFFFFF;
                build_out_frame.extended = true;
            } else build_out_frame.extended = false;
            break;
        case 4: out_bus = in_byte & 1; break;
        case 5:
            build_out_frame.length = in_byte & 0xF;
            if(build_out_frame.length > 8) build_out_frame.length = 8;
            break;
        default:
            if(step < build_out_frame.length + 6) {
                build_out_frame.data[step - 6] = in_byte;
            } else {
                state = IDLE;
                toggleRXLED();
                canManager.displayFrame(build_out_frame, 0);
            }
            break;
        }
        step++;
        break;
    case SETUP_EXT_BUSES:
        if (step >= 11) state = IDLE;
        step++;
        break;
    }
}

uint8_t GVRET_Comm_Handler::checksumCalc(uint8_t *buffer, int length)
{
    uint8_t valu = 0;
    for (int c = 0; c < length; c++) {
        valu ^= buffer[c];
    }
    return valu;
}
