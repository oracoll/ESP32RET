#ifndef SD_LOGGER_H_
#define SD_LOGGER_H_

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "config.h"

struct LogQueueItem {
    uint32_t timestamp;
    uint32_t id;
    bool extended;
    uint8_t length;
    uint8_t bus;
    uint8_t dir; // 0 = RX, 1 = TX
    uint8_t data[8];
};

class SDLogger {
public:
    SDLogger();
    void setup();
    void loop();
    void logFrame(CAN_FRAME &frame, int bus, int dir); // dir: 0=RX, 1=TX
    void logFrameFD(CAN_FRAME_FD &frame, int bus, int dir);

    bool isLoggingActive() { return loggingActive; }
    bool isPlaybackActive() { return playbackActive; }
    bool isCardPresent() { return cardPresent; }

    // Status indicators
    bool getAndClearYellowBlink() { bool b = yellowBlink; yellowBlink = false; return b; }
    bool getAndClearOrangeBlink() { bool b = orangeBlink; orangeBlink = false; return b; }
    bool getAndClearPurpleBlink() { bool b = purpleBlink; purpleBlink = false; return b; }

    void startPlayback(String filename);
    void stopPlayback();

    // Multitasking helper methods
    void writeLoggedFrameToFile(const LogQueueItem &item);
    void processPlaybackTick();
    void processPeriodicCommit();

private:
    bool cardPresent;
    bool loggingActive;
    bool playbackActive;
    File logFile;
    File playFile;
    int logIndex;
    String currentLogFilename;
    uint32_t lastFlush;
    uint32_t lastReopen;

    // Playback state variables
    uint32_t playBaseTime;
    uint32_t fileBaseTime;
    uint32_t nextFrameTime;
    uint32_t prevFrameTime;
    CAN_FRAME nextFrame;
    int nextFrameBus;
    bool hasNextFrame;
    char playLineBuffer[128];
    int playLineBufferLen;

    // Buttons state
    uint32_t btn1PressStart;
    bool btn1WasPressed;
    bool btn1Triggered;
    uint32_t btn2PressStart;
    bool btn2WasPressed;
    bool btn2Triggered;

    // Status blink triggers
    bool yellowBlink;
    bool orangeBlink;
    bool purpleBlink;

    void checkSDCard();
    void startLogging();
    void stopLogging();
    String getNextFileName();
    String findLatestLogFile();
    void findHighestLogIndex();
    bool parseNextPlayFrame();
    bool parseLine(String line);
};

extern SDLogger sdLogger;

#endif
