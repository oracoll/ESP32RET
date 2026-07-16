#include "sd_logger.h"
#include "can_manager.h"

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
    yellowBlink = false;
    orangeBlink = false;
    purpleBlink = false;
    lastFlush = 0;
    lastReopen = 0;
    hasNextFrame = false;
    fileBaseTime = 0;
}

void SDLogger::setup() {
    pinMode(15, INPUT_PULLDOWN);
    pinMode(34, INPUT); // D34 is input-only, no internal pull-downs in hardware

    // Enable internal pull-ups on SPI pins to ensure stable levels and prevent open-drain float on MISO (essential for many SD card adapters)
    pinMode(19, INPUT_PULLUP); // MISO
    pinMode(23, INPUT_PULLUP); // MOSI
    pinMode(18, INPUT_PULLUP); // SCK

    // Configure CS pin (4) as output and drive it HIGH to unselect the card initially
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH);

    // Provide a solid delay for SD card internal controllers to boot up completely
    delay(500);

    // Initialize SPI on pins 18, 19, 23 (pass -1 to prevent SPI driver from seizing Pin 4)
    SPI.begin(18, 19, 23, -1);

    // Generate at least 74 clock cycles with CS HIGH (120 cycles here) to cleanly put the SD card into SPI mode before initialization
    SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    digitalWrite(4, HIGH);
    for (int i = 0; i < 15; i++) {
        SPI.transfer(0xFF);
    }
    SPI.endTransaction();

    // Check if card is present on boot
    checkSDCard();
}

void SDLogger::checkSDCard() {
    if (loggingActive || playbackActive) return; // Do not interrupt active logging or playback

    // If not currently detected, attempt first-time initialization
    if (!cardPresent) {
        if (SD.begin(4, SPI, 4000000)) {
            cardPresent = true;
            Serial.println("SD Card detected and initialized successfully!");
            findHighestLogIndex();
        } else {
            cardPresent = false;
            Serial.println("No SD Card detected.");
        }
    } else {
        // If already detected, dynamically check if card is still inserted and responsive
        if (SD.cardType() != CARD_NONE) {
            cardPresent = true;
        } else {
            cardPresent = false;
            SD.end(); // Clean up if card was pulled out
            Serial.println("SD Card was removed.");
        }
    }
}

void SDLogger::findHighestLogIndex() {
    File root = SD.open("/");
    if (!root) return;

    int maxIdx = 0;
    while (true) {
        File file = root.openNextFile();
        if (!file) {
            break; // No more files
        }

        String name = file.name();
        int idx = name.indexOf("log_");
        if (idx != -1) {
            String numStr = name.substring(idx + 4, idx + 7);
            int num = numStr.toInt();
            if (num > maxIdx) {
                maxIdx = num;
            }
        }
        file.close();
    }
    root.close();

    logIndex = maxIdx + 1;
    Serial.printf("Highest log index found: %d. Next log will be log_%03d.csv\n", maxIdx, logIndex);
}

String SDLogger::findLatestLogFile() {
    char nameBuf[32];
    for (int i = logIndex - 1; i >= 1; i--) {
        sprintf(nameBuf, "/log_%03d.csv", i);
        if (SD.exists(nameBuf)) {
            return String(nameBuf);
        }
    }
    return String();
}

String SDLogger::getNextFileName() {
    char nameBuf[32];
    sprintf(nameBuf, "/log_%03d.csv", logIndex);
    logIndex++; // Increment for the next start logging session
    return String(nameBuf);
}

void SDLogger::startLogging() {
    if (playbackActive) {
        Serial.println("Cannot start logging while playback is active.");
        return;
    }

    // Only re-check if card is not already successfully detected to avoid multiple begin() locking issues
    if (!cardPresent) {
        checkSDCard();
    }

    if (!cardPresent) {
        Serial.println("Failed to start logging: SD Card not present.");
        orangeBlink = true;
        return;
    }

    currentLogFilename = getNextFileName();
    logFile = SD.open(currentLogFilename, FILE_WRITE);
    if (logFile) {
        loggingActive = true;
        yellowBlink = true;
        lastFlush = millis();
        lastReopen = millis();
        Serial.print("Started logging to: ");
        Serial.println(currentLogFilename);

        // Write SavvyCAN CSV header row with Comma separation for perfect Excel columns layout
        logFile.print("Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8\n");
        logFile.flush();
    } else {
        Serial.println("Failed to open log file for writing.");
        orangeBlink = true;
    }
}

void SDLogger::stopLogging() {
    if (loggingActive) {
        logFile.close();
        loggingActive = false;
        purpleBlink = true; // Indicate logging stopped
        Serial.println("Stopped logging to SD Card.");
    }
}

void SDLogger::startPlayback(String filename) {
    if (loggingActive) {
        Serial.println("Cannot start playback while logging is active.");
        return;
    }
    if (playbackActive) stopPlayback();

    checkSDCard();
    if (!cardPresent) {
        Serial.println("Failed to start playback: SD Card not present.");
        orangeBlink = true;
        return;
    }

    playFile = SD.open(filename, FILE_READ);
    if (playFile) {
        playbackActive = true;
        fileBaseTime = 0;
        hasNextFrame = false;
        playBaseTime = micros();
        Serial.print("Attempting playback of: ");
        Serial.println(filename);
    } else {
        Serial.printf("Failed to open file for playback: %s\n", filename.c_str());
        orangeBlink = true;
    }
}

void SDLogger::stopPlayback() {
    if (playbackActive) {
        playFile.close();
        playbackActive = false;
        hasNextFrame = false;
        purpleBlink = true; // Briefly flash purple on stop
        Serial.println("Stopped SD Card playback.");
    }
}

bool SDLogger::parseNextPlayFrame() {
    if (!playFile || !playFile.available()) return false;

    String line;
    while (playFile.available()) {
        line = playFile.readStringUntil('\n');
        line.trim();
        if (line.length() > 0 && !line.startsWith("Time")) {
            break; // Valid data row found!
        }
        if (!playFile.available()) return false;
    }

    if (line.length() == 0) return false;

    // Split columns by comma
    int commaIndex[16];
    int count = 0;
    int pos = 0;
    while ((pos = line.indexOf(',', pos)) != -1 && count < 16) {
        commaIndex[count++] = pos;
        pos++;
    }

    if (count < 5) {
        // Less than 5 commas means invalid line formatting
        return false;
    }

    // 1. Time Stamp
    String timeStr = line.substring(0, commaIndex[0]);
    nextFrameTime = strtoul(timeStr.c_str(), NULL, 10);

    // 2. ID (hex string, with or without 0x/0X prefix)
    String idStr = line.substring(commaIndex[0] + 1, commaIndex[1]);
    idStr.trim();
    if (idStr.startsWith("0x") || idStr.startsWith("0X")) {
        nextFrame.id = strtoul(idStr.c_str(), NULL, 16);
    } else {
        nextFrame.id = strtoul(idStr.c_str(), NULL, 16);
    }

    // 3. Extended
    String extStr = line.substring(commaIndex[1] + 1, commaIndex[2]);
    extStr.toUpperCase();
    nextFrame.extended = (extStr == "TRUE" || extStr == "1");
    if (nextFrame.extended) {
        nextFrame.id &= 0x1FFFFFFF;
    } else {
        nextFrame.id &= 0x7FF;
    }

    // 4. Dir (ignored during transmit)

    // 5. Bus
    String busStr = line.substring(commaIndex[3] + 1, commaIndex[4]);
    nextFrameBus = busStr.toInt();
    if (nextFrameBus < 0 || nextFrameBus >= NUM_BUSES) {
        nextFrameBus = 0;
    }

    // 6. LEN
    String lenStr = line.substring(commaIndex[4] + 1, commaIndex[5]);
    nextFrame.length = lenStr.toInt();
    if (nextFrame.length > 8) nextFrame.length = 8;

    // 7. Data bytes (up to nextFrame.length)
    int dataStartIdx = 5;
    for (int d = 0; d < nextFrame.length; d++) {
        if (dataStartIdx + d >= count) {
            nextFrame.data.uint8[d] = 0;
            continue;
        }
        int start = commaIndex[dataStartIdx + d] + 1;
        int end = (dataStartIdx + d + 1 < count) ? commaIndex[dataStartIdx + d + 1] : line.length();
        String byteStr = line.substring(start, end);
        byteStr.trim();
        nextFrame.data.uint8[d] = (uint8_t)strtoul(byteStr.c_str(), NULL, 16);
    }

    nextFrame.rtr = 0;
    return true;
}

void SDLogger::logFrame(CAN_FRAME &frame, int bus, int dir) {
    if (!loggingActive || !logFile) return;

    // Log in SavvyCAN standard CSV format
    // Format: Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8
    logFile.printf("%u,%08X,%s,%s,%d,%d",
                   micros(),
                   frame.id,
                   frame.extended ? "TRUE" : "FALSE",
                   dir == 0 ? "Rx" : "Tx",
                   bus,
                   frame.length);

    for (int i = 0; i < 8; i++) {
        if (i < frame.length) {
            logFile.printf(",%02X", frame.data.uint8[i]);
        } else {
            logFile.print(",");
        }
    }
    logFile.print("\n");
}

void SDLogger::logFrameFD(CAN_FRAME_FD &frame, int bus, int dir) {
    if (!loggingActive || !logFile) return;

    // Fallback: log FD frame as standard CAN frame in the CSV (standard loggers usually downsample or format up to 8 bytes for CSV)
    logFile.printf("%u,%08X,%s,%s,%d,%d",
                   micros(),
                   frame.id,
                   frame.extended ? "TRUE" : "FALSE",
                   dir == 0 ? "Rx" : "Tx",
                   bus,
                   frame.length > 8 ? 8 : frame.length);

    for (int i = 0; i < 8; i++) {
        if (i < frame.length) {
            logFile.printf(",%02X", frame.data.uint8[i]);
        } else {
            logFile.print(",");
        }
    }
    logFile.print("\n");
}

void SDLogger::loop() {
    // Periodically flush the file to protect against data loss
    if (loggingActive && logFile && (millis() - lastFlush > 500)) {
        logFile.flush();
        lastFlush = millis();
    }

    // Auto-commit (close and re-open in append mode) every 5 seconds to guarantee directory structure writes
    if (loggingActive && logFile && (millis() - lastReopen > 5000)) {
        logFile.close();
        logFile = SD.open(currentLogFilename, FILE_APPEND);
        lastReopen = millis();
        Serial.println("Committed SD log to disk.");
    }

    // Handle non-blocking SD playback ticks
    if (playbackActive && playFile) {
        if (!hasNextFrame) {
            if (parseNextPlayFrame()) {
                hasNextFrame = true;
                if (fileBaseTime == 0) {
                    fileBaseTime = nextFrameTime;
                    playBaseTime = micros();
                }
            } else {
                stopPlayback();
            }
        }

        if (hasNextFrame) {
            uint32_t elapsedMicros = micros() - playBaseTime;
            uint32_t fileElapsedMicros = nextFrameTime - fileBaseTime;

            if (elapsedMicros >= fileElapsedMicros) {
                // Send frame on CAN bus
                canManager.sendFrame(canBuses[nextFrameBus], nextFrame);

                // Trigger TX LED traffic animation
                extern uint32_t lastTxTraffic;
                lastTxTraffic = millis();

                hasNextFrame = false; // Move to next frame
            }
        }
    }

    // Button 1 (D15) handler: Start logging if held > 2s, stop if held > 1s
    bool btn1State = (digitalRead(15) == HIGH);
    if (btn1State) {
        if (!btn1WasPressed) {
            btn1PressStart = millis();
            btn1WasPressed = true;
        }
    } else {
        if (btn1WasPressed) {
            uint32_t pressDuration = millis() - btn1PressStart;
            if (!loggingActive) {
                if (pressDuration >= 2000) {
                    startLogging();
                }
            } else {
                if (pressDuration >= 1000) {
                    stopLogging();
                }
            }
            btn1WasPressed = false;
        }
    }

    // Button 2 (D34) handler: Hold > 3s starts playback, normal click stops playback
    bool btn2State = (digitalRead(34) == HIGH);
    if (btn2State) {
        if (!btn2WasPressed) {
            btn2PressStart = millis();
            btn2WasPressed = true;
        }
    } else {
        if (btn2WasPressed) {
            uint32_t pressDuration = millis() - btn2PressStart;
            if (playbackActive) {
                // Any normal click when playback is active stops it
                stopPlayback();
            } else {
                if (pressDuration >= 3000) {
                    // Hold for more than 3 seconds starts playback
                    if (SD.exists("/TX.csv")) {
                        startPlayback("/TX.csv");
                    } else if (SD.exists("/tx.csv")) {
                        startPlayback("/tx.csv");
                    } else {
                        String latestLog = findLatestLogFile();
                        if (latestLog.length() > 0) {
                            startPlayback(latestLog);
                        } else {
                            Serial.println("No TX.csv or logs found on SD card.");
                            orangeBlink = true;
                        }
                    }
                } else {
                    // Normal click checks SD card
                    checkSDCard();
                    if (cardPresent) {
                        yellowBlink = true; // Trigger Yellow status blink
                    } else {
                        orangeBlink = true; // Trigger Orange status blink
                    }
                }
            }
            btn2WasPressed = false;
        }
    }
}
