#ifndef SERIALCONSOLE_H_
#define SERIALCONSOLE_H_

#include "config.h"
#include "sys_io.h"

class SerialConsole {
public:
    SerialConsole();
    void printMenu();
    void rcvCharacter(uint8_t chr);
    void printBusName(int bus);

private:
    char cmdBuffer[80];
    int ptrBuffer;

    void init();
    void handleConsoleCmd();
};

#endif /* SERIALCONSOLE_H_ */
