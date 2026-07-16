#ifndef SD_LOGGER_H_
#define SD_LOGGER_H_

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "config.h"

class SDLogger {
public:
    SDLogger();
    void setup();
    void loop();
    void logFrame(CAN_FRAME &frame, int bus, int dir); // dir: 0=RX, 1=TX
    void logFrameFD(CAN_FRAME_FD &frame, int bus, int dir);

    bool isLoggingActive() { return loggingActive; }
    bool isCardPresent() { return cardPresent; }

    // Status indicators
    bool getAndClearYellowBlink() { bool b = yellowBlink; yellowBlink = false; return b; }
    bool getAndClearOrangeBlink() { bool b = orangeBlink; orangeBlink = false; return b; }
    bool getAndClearPurpleBlink() { bool b = purpleBlink; purpleBlink = false; return b; }

private:
    bool cardPresent;
    bool loggingActive;
    File logFile;
    int logIndex;

    void findHighestLogIndex();

    // Buttons state
    uint32_t btn1PressStart;
    bool btn1WasPressed;
    uint32_t btn2PressStart;
    bool btn2WasPressed;

    // Status blink triggers
    bool yellowBlink;
    bool orangeBlink;
    bool purpleBlink;

    void checkSDCard();
    void startLogging();
    void stopLogging();
    String getNextFileName();
};

extern SDLogger sdLogger;

#endif
