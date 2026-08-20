#include "SerialConsole.h"
#include "can_manager.h"
#include "sd_logger.h"
#include "lawicel.h"
#include "gvret_comm.h"

SerialConsole::SerialConsole() {
    init();
}

void SerialConsole::init() {
    ptrBuffer = 0;
}

void SerialConsole::printMenu() {
    Logger::console("\n=== Arduino Nano R4 RET Console ===");
    Logger::console("0 - Enable/Disable CAN0");
    Logger::console("B - Set CAN0 Speed (e.g. B 500000)");
    Logger::console("L - Toggle LAWICEL Mode");
    Logger::console("S - Save Settings to EEPROM");
    Logger::console("H - Print this help menu");
}

void SerialConsole::rcvCharacter(uint8_t chr) {
    if (chr == 10 || chr == 13) {
        cmdBuffer[ptrBuffer] = 0;
        if (ptrBuffer > 0) {
            handleConsoleCmd();
            ptrBuffer = 0;
        }
    } else if (ptrBuffer < 79) {
        cmdBuffer[ptrBuffer++] = chr;
    }
}

void SerialConsole::handleConsoleCmd() {
    if (SysSettings.lawicelMode) {
        if (ptrBuffer == 1) lawicel.handleShortCmd(cmdBuffer[0]);
        else lawicel.handleLongCmd(cmdBuffer);
        return;
    }

    switch (cmdBuffer[0]) {
    case '0':
        settings.canSettings[0].enabled = !settings.canSettings[0].enabled;
        Logger::console("CAN0 Enabled: %s", settings.canSettings[0].enabled ? "TRUE" : "FALSE");
        break;
    case 'B':
    case 'b':
        if (ptrBuffer > 2) {
            uint32_t speed = strtoul(cmdBuffer + 2, NULL, 10);
            if (speed >= 10000 && speed <= 1000000) {
                settings.canSettings[0].nomSpeed = speed;
                canManager.setup();
                Logger::console("CAN0 Baud rate set to %lu", (unsigned long)speed);
            } else {
                Logger::console("Invalid speed. Supported range: 10000 to 1000000");
            }
        } else {
            Logger::console("Current CAN0 Speed: %lu. Usage: B <speed>", (unsigned long)settings.canSettings[0].nomSpeed);
        }
        break;
    case 'L':
    case 'l':
        SysSettings.lawicelMode = !SysSettings.lawicelMode;
        Logger::console("LAWICEL Mode: %s", SysSettings.lawicelMode ? "ENABLED" : "DISABLED");
        break;
    case 'H':
    case 'h':
    case '?':
        printMenu();
        break;
    case 'S':
    case 's':
        saveSettings();
        Logger::console("Settings saved to EEPROM.");
        break;
    }
}

void SerialConsole::printBusName(int bus) {
    Logger::console("CAN0");
}
