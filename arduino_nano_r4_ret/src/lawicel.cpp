#include "lawicel.h"
#include "config.h"
#include "utility.h"

void LAWICELHandler::handleShortCmd(char cmd)
{
    switch (cmd)
    {
    case 'O':
        CAN.begin((CanBitRate)settings.canSettings[0].nomSpeed);
        Serial.write(13);
        SysSettings.lawicelMode = true;
        break;
    case 'C':
        Serial.write(13);
        break;
    case 'L':
        CAN.begin((CanBitRate)settings.canSettings[0].nomSpeed);
        Serial.write(13);
        SysSettings.lawicelMode = true;
        break;
    case 'P':
        if (CAN.available()) SysSettings.lawicelPollCounter = 1;
        else Serial.write(13);
        break;
    case 'A':
        SysSettings.lawicelPollCounter = CAN.available();
        if (SysSettings.lawicelPollCounter == 0) Serial.write(13);
        break;
    case 'F':
        Serial.print("F00");
        Serial.write(13);
        break;
    case 'V':
        Serial.print("V1013\n");
        SysSettings.lawicelMode = true;
        break;
    case 'N':
        Serial.print("NANO4RET\n");
        SysSettings.lawicelMode = true;
        break;
    case 'x':
        SysSettings.lawicellExtendedMode = !SysSettings.lawicellExtendedMode;
        if (SysSettings.lawicellExtendedMode) {
            Serial.print("V2\n");
        } else {
            Serial.print("LAWICEL\n");
        }
        break;
    case 'B':
        if (SysSettings.lawicellExtendedMode) {
            printBusName(0);
            Serial.print("\n");
        }
        break;
    }
}

void LAWICELHandler::handleLongCmd(char *buffer)
{
    CAN_FRAME outFrame;
    int val;

    tokenizeCmdString(buffer);

    switch (buffer[0]) {
    case 't':
        outFrame.id = Utility::parseHexString(buffer + 1, 3);
        outFrame.length = buffer[4] - '0';
        outFrame.extended = false;
        if (outFrame.length > 8) outFrame.length = 8;
        for (int data = 0; data < outFrame.length; data++) {
            outFrame.data[data] = Utility::parseHexString(buffer + 5 + (2 * data), 2);
        }
        canManager.sendFrame(outFrame);
        if (SysSettings.lawicelAutoPoll) Serial.print("z");
        break;
    case 'T':
        outFrame.id = Utility::parseHexString(buffer + 1, 8);
        outFrame.length = buffer[9] - '0';
        outFrame.extended = true;
        if (outFrame.length > 8) outFrame.length = 8;
        for (int data = 0; data < outFrame.length; data++) {
            outFrame.data[data] = Utility::parseHexString(buffer + 10 + (2 * data), 2);
        }
        canManager.sendFrame(outFrame);
        if (SysSettings.lawicelAutoPoll) Serial.print("Z");
        break;
    case 'S':
        if (!SysSettings.lawicellExtendedMode) {
            val = Utility::parseHexCharacter(buffer[1]);
            switch (val) {
            case 0: settings.canSettings[0].nomSpeed = 10000; break;
            case 1: settings.canSettings[0].nomSpeed = 20000; break;
            case 2: settings.canSettings[0].nomSpeed = 50000; break;
            case 3: settings.canSettings[0].nomSpeed = 100000; break;
            case 4: settings.canSettings[0].nomSpeed = 125000; break;
            case 5: settings.canSettings[0].nomSpeed = 250000; break;
            case 6: settings.canSettings[0].nomSpeed = 500000; break;
            case 7: settings.canSettings[0].nomSpeed = 800000; break;
            case 8: settings.canSettings[0].nomSpeed = 1000000; break;
            }
        }
        break;
    case 'X':
        if (buffer[1] == '1') SysSettings.lawicelAutoPoll = true;
        else SysSettings.lawicelAutoPoll = false;
        break;
    case 'Z':
        if (buffer[1] == '1') SysSettings.lawicelTimestamping = true;
        else SysSettings.lawicelTimestamping = false;
        break;
    }
    Serial.write(13);
}

void LAWICELHandler::tokenizeCmdString(char *buff) {
   int idx = 0;
   char *tok;

   for (int i = 0; i < 13; i++) tokens[i][0] = 0;

   tok = strtok(buff, " ");
   if (tok != nullptr) strcpy(tokens[idx], tok);
   else tokens[idx][0] = 0;

   while (tokens[idx] != nullptr && idx < 13) {
       idx++;
       tok = strtok(nullptr, " ");
       if (tok != nullptr) strcpy(tokens[idx], tok);
       else tokens[idx][0] = 0;
   }
}

void LAWICELHandler::uppercaseToken(char *token) {
    int idx = 0;
    while (token[idx] != 0 && idx < 9) {
        token[idx] = toupper(token[idx]);
        idx++;
    }
    token[idx] = 0;
}

void LAWICELHandler::printBusName(int bus) {
    Serial.print("CAN0");
}

bool LAWICELHandler::parseLawicelCANCmd(CAN_FRAME &frame) {
    if (tokens[2] == nullptr) return false;
    frame.id = strtol(tokens[2], nullptr, 16);
    int idx = 3;
    int dataLen = 0;
    while (tokens[idx] != nullptr) {
        frame.data[dataLen++] = strtol(tokens[idx], nullptr, 16);
        idx++;
    }
    frame.length = dataLen;
    return true;
}

void LAWICELHandler::sendFrameToBuffer(CAN_FRAME &frame, int whichBus)
{
    char buff[40];

    if (SysSettings.lawicellExtendedMode)
    {
        Serial.print(micros());
        Serial.print(" - ");
        Serial.print(frame.id, HEX);
        if (frame.extended) Serial.print(" X ");
        else Serial.print(" S ");

        printBusName(whichBus);
        for (int d = 0; d < frame.length; d++)
        {
            Serial.print(" ");
            Serial.print(frame.data[d], HEX);
        }
    }
    else
    {
        if (frame.extended)
        {
            Serial.print("T");
            sprintf(buff, "%08lX", (unsigned long)frame.id);
            Serial.print(buff);
        }
        else
        {
            Serial.print("t");
            sprintf(buff, "%03lX", (unsigned long)frame.id);
            Serial.print(buff);
        }
        Serial.print(frame.length);
        for (int i = 0; i < frame.length; i++)
        {
            sprintf(buff, "%02X", frame.data[i]);
            Serial.print(buff);
        }
        if (SysSettings.lawicelTimestamping)
        {
            uint16_t timestamp = (uint16_t)millis();
            sprintf(buff, "%04X", timestamp);
            Serial.print(buff);
        }
    }
    Serial.write(13);
}
