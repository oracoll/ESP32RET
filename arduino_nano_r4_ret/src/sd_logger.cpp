#include "sd_logger.h"
#include "can_manager.h"
#include "sys_io.h"

SDLogger sdLogger;

SDLogger::SDLogger() {
    cardPresent = false;
    loggingActive = false;
    playbackActive = false;
    logIndex = 1;
    btn1PressStart = 0;
    btn1WasPressed = false;
    btn2PressStart = 0;
    btn2WasPressed = false;
    lastFlush = 0;
    hasNextFrame = false;
    fileBaseTime = 0;
    prevFrameTime = 0;
}

void SDLogger::setup() {
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);

    checkSDCard();
}

void SDLogger::checkSDCard() {
    if (loggingActive || playbackActive) return;

    if (!cardPresent) {
        if (SD.begin(SD_CS_PIN)) {
            cardPresent = true;
            Serial.println("SD Card initialized successfully!");
            findHighestLogIndex();
        } else {
            cardPresent = false;
        }
    }
}

void SDLogger::findHighestLogIndex() {
    File root = SD.open("/");
    if (!root) return;

    int maxIdx = 0;
    while (true) {
        File file = root.openNextFile();
        if (!file) break;

        String name = file.name();
        int idx = name.indexOf("LOG_");
        if (idx == -1) idx = name.indexOf("log_");
        if (idx != -1) {
            String numStr = name.substring(idx + 4, idx + 7);
            int num = numStr.toInt();
            if (num > maxIdx) maxIdx = num;
        }
        file.close();
    }
    root.close();

    logIndex = maxIdx + 1;
}

String SDLogger::findLatestLogFile() {
    char nameBuf[32];
    for (int i = logIndex - 1; i >= 1; i--) {
        sprintf(nameBuf, "/LOG_%03d.CSV", i);
        if (SD.exists(nameBuf)) return String(nameBuf);
        sprintf(nameBuf, "/log_%03d.csv", i);
        if (SD.exists(nameBuf)) return String(nameBuf);
    }
    return String();
}

String SDLogger::getNextFileName() {
    char nameBuf[32];
    sprintf(nameBuf, "/LOG_%03d.CSV", logIndex);
    logIndex++;
    return String(nameBuf);
}

void SDLogger::startLogging() {
    if (loggingActive) return;
    if (playbackActive) stopPlayback();

    if (!cardPresent) checkSDCard();
    if (!cardPresent) {
        Serial.println("SD card not present, cannot start logging.");
        return;
    }

    currentLogFilename = getNextFileName();
    logFile = SD.open(currentLogFilename, FILE_WRITE);
    if (logFile) {
        loggingActive = true;
        lastFlush = millis();
        logFile.print("Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8\n");
        logFile.flush();
        Serial.print("Started logging to: ");
        Serial.println(currentLogFilename);
    } else {
        Serial.println("Failed to open log file.");
    }
}

void SDLogger::stopLogging() {
    if (loggingActive) {
        if (logFile) {
            logFile.flush();
            logFile.close();
        }
        loggingActive = false;
        Serial.println("Stopped logging.");
    }
}

void SDLogger::startPlayback(String filename) {
    if (loggingActive) stopLogging();
    if (playbackActive) stopPlayback();

    if (!cardPresent) checkSDCard();
    if (!cardPresent) return;

    playFile = SD.open(filename, FILE_READ);
    if (playFile) {
        playbackActive = true;
        fileBaseTime = 0;
        prevFrameTime = 0;
        hasNextFrame = false;
        playBaseTime = micros();
        Serial.print("Started playback of: ");
        Serial.println(filename);

        if (parseNextPlayFrame()) {
            hasNextFrame = true;
            fileBaseTime = nextFrameTime;
            prevFrameTime = nextFrameTime;
            playBaseTime = micros();
        } else {
            stopPlayback();
        }
    }
}

void SDLogger::stopPlayback() {
    if (playbackActive) {
        if (playFile) playFile.close();
        playbackActive = false;
        hasNextFrame = false;
        Serial.println("Stopped playback.");
    }
}

void SDLogger::logFrame(CAN_FRAME &frame, int bus, int dir) {
    if (!loggingActive || !logFile) return;

    LogQueueItem item;
    item.timestamp = micros();
    item.id = frame.id;
    item.extended = frame.extended;
    item.length = frame.length;
    item.bus = bus;
    item.dir = dir;
    memcpy(item.data, frame.data, 8);

    writeLoggedFrameToFile(item);
}

void SDLogger::writeLoggedFrameToFile(const LogQueueItem &item) {
    char buf[128];
    int len = sprintf(buf, "%lu,%08X,%s,%s,%d,%d",
                      (unsigned long)item.timestamp,
                      (unsigned int)item.id,
                      item.extended ? "TRUE" : "FALSE",
                      item.dir == 0 ? "Rx" : "Tx",
                      item.bus,
                      item.length);
    logFile.print(buf);

    for (int i = 0; i < 8; i++) {
        if (i < item.length) {
            sprintf(buf, ",%02X", item.data[i]);
            logFile.print(buf);
        } else {
            logFile.print(",");
        }
    }
    logFile.print("\n");
}

bool SDLogger::parseNextPlayFrame() {
    if (!playFile || !playFile.available()) return false;

    String line;
    while (playFile.available()) {
        line = playFile.readStringUntil('\n');
        line.trim();
        if (line.length() > 0 && !line.startsWith("Time") && !line.startsWith("time")) {
            break;
        }
    }

    if (line.length() == 0) return false;
    return parseLine(line);
}

bool SDLogger::parseLine(String line) {
    char separator = ',';
    if (line.indexOf('\t') != -1) separator = '\t';

    int commaIndex[16];
    int count = 0;
    int pos = 0;
    while ((pos = line.indexOf(separator, pos)) != -1 && count < 16) {
        commaIndex[count++] = pos;
        pos++;
    }

    if (count < 5) return false;

    nextFrameTime = strtoul(line.substring(0, commaIndex[0]).c_str(), NULL, 10);

    String idStr = line.substring(commaIndex[0] + 1, commaIndex[1]);
    idStr.trim();
    nextFrame.id = strtoul(idStr.c_str(), NULL, 16);

    String extStr = line.substring(commaIndex[1] + 1, commaIndex[2]);
    extStr.toUpperCase();
    nextFrame.extended = (extStr == "TRUE" || extStr == "1");

    nextFrameBus = 0;

    String lenStr = line.substring(commaIndex[4] + 1, commaIndex[5]);
    nextFrame.length = lenStr.toInt();
    if (nextFrame.length > 8) nextFrame.length = 8;

    int dataStartIdx = 5;
    for (int d = 0; d < nextFrame.length; d++) {
        if (dataStartIdx + d >= count) {
            nextFrame.data[d] = 0;
            continue;
        }
        int start = commaIndex[dataStartIdx + d] + 1;
        int end = (dataStartIdx + d + 1 < count) ? commaIndex[dataStartIdx + d + 1] : line.length();
        String byteStr = line.substring(start, end);
        byteStr.trim();
        nextFrame.data[d] = (uint8_t)strtoul(byteStr.c_str(), NULL, 16);
    }

    nextFrame.rtr = 0;
    return true;
}

void SDLogger::processPlaybackTick() {
    if (!playbackActive || !playFile) return;

    if (!hasNextFrame) {
        if (parseNextPlayFrame()) {
            hasNextFrame = true;
            if (fileBaseTime == 0) {
                fileBaseTime = nextFrameTime;
                playBaseTime = micros();
                prevFrameTime = nextFrameTime;
            } else if (nextFrameTime < prevFrameTime) {
                fileBaseTime = nextFrameTime;
                playBaseTime = micros();
            }
        } else {
            stopPlayback();
            return;
        }
    }

    if (hasNextFrame) {
        uint32_t elapsedMicros = micros() - playBaseTime;
        uint32_t fileElapsedMicros = nextFrameTime - fileBaseTime;

        if (elapsedMicros >= fileElapsedMicros) {
            canManager.sendFrame(nextFrame);
            prevFrameTime = nextFrameTime;
            hasNextFrame = false;
        }
    }
}

void SDLogger::loop() {
    // Process continuous playback and flush
    if (playbackActive) {
        processPlaybackTick();
    }

    if (loggingActive && logFile && (millis() - lastFlush > 1000)) {
        logFile.flush();
        lastFlush = millis();
    }

    // Button 1 (D2 LOG) - Active LOW
    bool btn1State = (digitalRead(BTN_LOG_PIN) == LOW);
    if (btn1State && !btn1WasPressed) {
        btn1WasPressed = true;
        btn1PressStart = millis();
    } else if (!btn1State && btn1WasPressed) {
        btn1WasPressed = false;
        if (millis() - btn1PressStart > 50) { // debounced
            if (loggingActive) stopLogging();
            else startLogging();
        }
    }

    // Button 2 (D3 REPLAY) - Active LOW
    bool btn2State = (digitalRead(BTN_REPLAY_PIN) == LOW);
    if (btn2State && !btn2WasPressed) {
        btn2WasPressed = true;
        btn2PressStart = millis();
    } else if (!btn2State && btn2WasPressed) {
        btn2WasPressed = false;
        if (millis() - btn2PressStart > 50) { // debounced
            if (playbackActive) {
                stopPlayback();
            } else {
                String latest = findLatestLogFile();
                if (latest.length() > 0) startPlayback(latest);
                else if (SD.exists("/TX.CSV")) startPlayback("/TX.CSV");
                else if (SD.exists("/tx.csv")) startPlayback("/tx.csv");
            }
        }
    }
}
