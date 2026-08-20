#ifndef SD_LOGGER_H_
#define SD_LOGGER_H_

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "config.h"
#include "can_manager.h"

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

    bool isLoggingActive() { return loggingActive; }
    bool isPlaybackActive() { return playbackActive; }
    bool isCardPresent() { return cardPresent; }

    void startLogging();
    void stopLogging();
    void startPlayback(String filename);
    void stopPlayback();

    String getCurrentLogFilename() { return currentLogFilename; }

private:
    bool cardPresent;
    bool loggingActive;
    bool playbackActive;
    File logFile;
    File playFile;
    int logIndex;
    String currentLogFilename;
    uint32_t lastFlush;

    // Playback state variables
    uint32_t playBaseTime;
    uint32_t fileBaseTime;
    uint32_t nextFrameTime;
    uint32_t prevFrameTime;
    CAN_FRAME nextFrame;
    int nextFrameBus;
    bool hasNextFrame;

    // Button states
    uint32_t btn1PressStart;
    bool btn1WasPressed;
    uint32_t btn2PressStart;
    bool btn2WasPressed;

    void checkSDCard();
    String getNextFileName();
    String findLatestLogFile();
    void findHighestLogIndex();
    bool parseNextPlayFrame();
    bool parseLine(String line);
    void writeLoggedFrameToFile(const LogQueueItem &item);
    void processPlaybackTick();
};

extern SDLogger sdLogger;

#endif
