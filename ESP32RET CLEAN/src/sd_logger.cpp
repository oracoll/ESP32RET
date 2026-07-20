#include "sd_logger.h"
#include "can_manager.h"
#include <esp32_mcp2517fd.h>

#if SOC_TWAI_CONTROLLER_NUM == 2 and ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
#define MCP_CAN_INST CAN2
#else
#define MCP_CAN_INST CAN1
#endif

static void suspendSpiTasks() {
    // Left empty: SPI mutual exclusion is handled safely via recursive mutexes instead of task suspension
}

static void resumeSpiTasks() {
    // Left empty: SPI mutual exclusion is handled safely via recursive mutexes instead of task suspension
}

static SemaphoreHandle_t sdMutex = NULL;

class SPILock {
public:
    SPILock() {
        suspendSpiTasks();
        if (sdMutex != NULL) {
            xSemaphoreTakeRecursive(sdMutex, portMAX_DELAY);
        }
    }
    ~SPILock() {
        if (sdMutex != NULL) {
            xSemaphoreGiveRecursive(sdMutex);
        }
        resumeSpiTasks();
    }
};

static QueueHandle_t logQueue = NULL;
static TaskHandle_t sdLoggerTaskHandle = NULL;

void vSDLoggerTask(void *pvParameters) {
    while (true) {
        // 0. Handle state change requests on the correct thread context
        sdLogger.processStateTransitions();

        // 1. Process logging queue
        if (logQueue != NULL) {
            LogQueueItem item;
            int count = 0;
            // Process up to 15 frames per task iteration to stay responsive
            while (count < 15 && xQueueReceive(logQueue, &item, 0) == pdTRUE) {
                sdLogger.writeLoggedFrameToFile(item);
                count++;
            }
        }

        // 2. Process playback ticks (prioritize SavvyCAN active USB connection if connected)
        if (sdLogger.isPlaybackActive()) {
            extern uint32_t lastHostActivity;
            if (millis() - lastHostActivity < 2000) {
                Serial.println("SavvyCAN connection active. Prioritizing SavvyCAN and stopping SD Playback.");
                sdLogger.stopPlayback();
            } else {
                sdLogger.processPlaybackTick();
            }
        }

        // 3. Process periodic auto-commit / flush
        sdLogger.processPeriodicCommit();

        // Dynamic sleep to stay responsive without wasting CPU when idle
        if (sdLogger.isPlaybackActive()) {
            vTaskDelay(pdMS_TO_TICKS(1)); // Sleep 1ms during active playback
        } else {
            vTaskDelay(pdMS_TO_TICKS(10)); // Sleep 10ms during logging / idle
        }
    }
}

SDLogger sdLogger;

SDLogger::SDLogger() {
    cardPresent = false;
    loggingActive = false;
    playbackActive = false;
    requestLoggingStart = false;
    requestLoggingStop = false;
    requestPlaybackStart = false;
    requestPlaybackStop = false;
    requestPlayFilename = "";
    logIndex = 1;
    btn1PressStart = 0;
    btn1WasPressed = false;
    btn1Triggered = false;
    btn2PressStart = 0;
    btn2WasPressed = false;
    btn2Triggered = false;
    yellowBlink = false;
    orangeBlink = false;
    purpleBlink = false;
    lastFlush = 0;
    lastReopen = 0;
    hasNextFrame = false;
    fileBaseTime = 0;
    prevFrameTime = 0;
    playLineBufferLen = 0;
}

void SDLogger::setup() {
    pinMode(15, INPUT_PULLDOWN);
    pinMode(34, INPUT); // D34 button handler

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

    // Initialize FreeRTOS recursive mutex for safe nested task locking
    sdMutex = xSemaphoreCreateRecursiveMutex();

    // Initialize FreeRTOS queue and background task
    logQueue = xQueueCreate(256, sizeof(LogQueueItem));
    if (logQueue != NULL) {
        // Pin to Core 0 (dual core) to offload Main loop entirely from SD latency
        xTaskCreatePinnedToCore(vSDLoggerTask, "SD_LOGGER_TASK", 8192, NULL, 2, &sdLoggerTaskHandle, 0);
        Serial.println("Multitasking SD logger task started successfully.");
    }

    // Check if card is present on boot
    checkSDCard();
}

void SDLogger::checkSDCard() {
    if (loggingActive || playbackActive) return; // Do not interrupt active logging or playback

    SPILock lock;
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
    SPILock lock;
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
    SPILock lock;
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
    if (loggingActive || requestLoggingStart) return;
    if (playbackActive || requestPlaybackStart) {
        Serial.println("Cannot start logging while playback is active.");
        return;
    }
    requestLoggingStart = true;
}

void SDLogger::stopLogging() {
    if (!loggingActive || requestLoggingStop) return;
    requestLoggingStop = true;
}

void SDLogger::startPlayback(String filename) {
    if (loggingActive || requestLoggingStart) {
        Serial.println("Cannot start playback while logging is active.");
        return;
    }
    requestPlayFilename = filename;
    requestPlaybackStart = true;
}

void SDLogger::stopPlayback() {
    if (!playbackActive || requestPlaybackStop) return;
    requestPlaybackStop = true;
}

void SDLogger::executeStartLogging() {
    if (playbackActive) return;
    if (!cardPresent) {
        checkSDCard();
    }
    if (!cardPresent) {
        Serial.println("Failed to start logging: SD Card not present.");
        orangeBlink = true;
        return;
    }

    SPILock lock;
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

void SDLogger::executeStopLogging() {
    if (loggingActive) {
        SPILock lock;
        logFile.close();
        loggingActive = false;
        purpleBlink = true; // Indicate logging stopped
        Serial.println("Stopped logging to SD Card.");
    }
}

void SDLogger::executeStartPlayback(String filename) {
    if (loggingActive) return;
    if (playbackActive) executeStopPlayback();

    if (!cardPresent) {
        checkSDCard();
    }
    if (!cardPresent) {
        Serial.println("Failed to start playback: SD Card not present.");
        orangeBlink = true;
        return;
    }

    SPILock lock;
    playFile = SD.open(filename, FILE_READ);
    if (playFile) {
        playbackActive = true;
        fileBaseTime = 0;
        prevFrameTime = 0;
        hasNextFrame = false;
        playLineBufferLen = 0;
        playBaseTime = micros();
        Serial.print("Attempting playback of: ");
        Serial.println(filename);

        // Pre-parse the first frame to validate file formatting and align baseline times
        if (parseNextPlayFrame()) {
            hasNextFrame = true;
            fileBaseTime = nextFrameTime;
            prevFrameTime = nextFrameTime;
            playBaseTime = micros();
            Serial.printf("Playback initialized successfully. Baseline time: %u micros\n", fileBaseTime);
        } else {
            Serial.println("Failed to parse first playback frame.");
            executeStopPlayback();
            orangeBlink = true;
        }
    } else {
        Serial.printf("Failed to open file for playback: %s\n", filename.c_str());
        orangeBlink = true;
    }
}

void SDLogger::executeStopPlayback() {
    if (playbackActive) {
        SPILock lock;
        playFile.close();
        playbackActive = false;
        hasNextFrame = false;
        playLineBufferLen = 0;
        purpleBlink = true; // Briefly flash purple on stop
        Serial.println("Stopped SD Card playback.");
    }
}

void SDLogger::processStateTransitions() {
    if (requestLoggingStart) {
        requestLoggingStart = false;
        executeStartLogging();
    }
    if (requestLoggingStop) {
        requestLoggingStop = false;
        executeStopLogging();
    }
    if (requestPlaybackStart) {
        requestPlaybackStart = false;
        executeStartPlayback(requestPlayFilename);
    }
    if (requestPlaybackStop) {
        requestPlaybackStop = false;
        executeStopPlayback();
    }
}

bool SDLogger::parseNextPlayFrame() {
    SPILock lock;
    if (!playFile || !playFile.available()) return false;

    String line;
    while (playFile.available()) {
        line = playFile.readStringUntil('\n');
        line.trim();
        if (line.length() > 0 && !line.startsWith("Time")) {
            break; // Valid data row found!
        }
    }

    if (line.length() == 0) return false;
    return parseLine(line);
}

bool SDLogger::parseLine(String line) {
    // Detect column separator dynamically (supports both Comma-separated and Tab-separated formats)
    char separator = ',';
    if (line.indexOf('\t') != -1) {
        separator = '\t';
    }

    int commaIndex[16];
    int count = 0;
    int pos = 0;
    while ((pos = line.indexOf(separator, pos)) != -1 && count < 16) {
        commaIndex[count++] = pos;
        pos++;
    }

    if (count < 5) {
        return false; // Invalid CSV line structure
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

    // 4. Dir (ignored during playback transmit)

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
    if (!loggingActive || !logQueue) return;

    LogQueueItem item;
    item.timestamp = micros();
    item.id = frame.id;
    item.extended = frame.extended;
    item.length = frame.length;
    item.bus = bus;
    item.dir = dir;
    for (int i = 0; i < 8; i++) {
        item.data[i] = frame.data.uint8[i];
    }

    xQueueSend(logQueue, &item, 0); // Non-blocking push
}

void SDLogger::logFrameFD(CAN_FRAME_FD &frame, int bus, int dir) {
    if (!loggingActive || !logQueue) return;

    LogQueueItem item;
    item.timestamp = micros();
    item.id = frame.id;
    item.extended = frame.extended;
    item.length = frame.length > 8 ? 8 : frame.length;
    item.bus = bus;
    item.dir = dir;
    for (int i = 0; i < 8; i++) {
        item.data[i] = (i < frame.length) ? frame.data.uint8[i] : 0;
    }

    xQueueSend(logQueue, &item, 0); // Non-blocking push
}

void SDLogger::writeLoggedFrameToFile(const LogQueueItem &item) {
    if (!loggingActive || !logFile) return;

    SPILock lock;
    // Log in SavvyCAN standard CSV format
    // Format: Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8
    logFile.printf("%u,%08X,%s,%s,%d,%d",
                   item.timestamp,
                   item.id,
                   item.extended ? "TRUE" : "FALSE",
                   item.dir == 0 ? "Rx" : "Tx",
                   item.bus,
                   item.length);

    for (int i = 0; i < 8; i++) {
        if (i < item.length) {
            logFile.printf(",%02X", item.data[i]);
        } else {
            logFile.print(",");
        }
    }
    logFile.print("\n");
}

void SDLogger::processPeriodicCommit() {
    // Periodically flush the file to protect against data loss
    if (loggingActive && logFile && (millis() - lastFlush > 500)) {
        SPILock lock;
        logFile.flush();
        lastFlush = millis();
    }

    // Auto-commit (close and re-open in append mode) every 5 seconds to guarantee directory structure writes
    if (loggingActive && logFile && (millis() - lastReopen > 5000)) {
        SPILock lock;
        logFile.close();
        logFile = SD.open(currentLogFilename, FILE_APPEND);
        lastReopen = millis();
        Serial.println("Committed SD log to disk.");
    }
}

void SDLogger::processPlaybackTick() {
    if (!playbackActive || !playFile) return;

    if (!hasNextFrame) {
        // Safe scope for SD card reads
        {
            SPILock lock;
            if (parseNextPlayFrame()) {
                hasNextFrame = true;
                if (fileBaseTime == 0) {
                    fileBaseTime = nextFrameTime;
                    playBaseTime = micros();
                    prevFrameTime = nextFrameTime;
                } else {
                    // Detect timestamp rollback in file!
                    if (nextFrameTime < prevFrameTime) {
                        Serial.printf("Rollback detected! Resetting time base. Old: %u, New: %u\n", prevFrameTime, nextFrameTime);
                        fileBaseTime = nextFrameTime;
                        playBaseTime = micros();
                    }
                }
            } else {
                stopPlayback();
            }
        }
    }

    if (hasNextFrame) {
        uint32_t elapsedMicros = micros() - playBaseTime;
        uint32_t fileElapsedMicros = nextFrameTime - fileBaseTime;

        if (elapsedMicros >= fileElapsedMicros) {
            // Send frame on CAN bus (No SPILock held to ensure MCP2517FD tasks can schedule freely)
            canManager.sendFrame(canBuses[nextFrameBus], nextFrame);

            // Trigger TX LED traffic animation
            extern uint32_t lastTxTraffic;
            lastTxTraffic = millis();

            prevFrameTime = nextFrameTime; // Update sequential tracking
            hasNextFrame = false; // Move to next frame
        }
    }
}

void SDLogger::loop() {
    // Read raw Button 1 state and apply robust 50ms software debouncing
    bool rawBtn1 = (digitalRead(15) == HIGH);
    static bool btn1State = false;
    static uint32_t lastBtn1Change = 0;
    if (rawBtn1 != btn1State) {
        if (millis() - lastBtn1Change > 50) {
            btn1State = rawBtn1;
            lastBtn1Change = millis();
        }
    }

    // Button 1 (D15) handler: Start logging if held > 2s, stop if held > 1s
    if (btn1State) {
        if (!btn1WasPressed) {
            btn1PressStart = millis();
            btn1WasPressed = true;
            btn1Triggered = false;
        } else if (!btn1Triggered) {
            uint32_t pressDuration = millis() - btn1PressStart;
            if (!loggingActive) {
                if (pressDuration >= 2000) {
                    startLogging();
                    btn1Triggered = true;
                }
            } else {
                if (pressDuration >= 1000) {
                    stopLogging();
                    btn1Triggered = true;
                }
            }
        }
    } else {
        btn1WasPressed = false;
        btn1Triggered = false;
    }

    // Read raw Button 2 state and apply robust 50ms software debouncing
    bool rawBtn2 = (digitalRead(34) == HIGH);
    static bool btn2State = false;
    static uint32_t lastBtn2Change = 0;
    if (rawBtn2 != btn2State) {
        if (millis() - lastBtn2Change > 50) {
            btn2State = rawBtn2;
            lastBtn2Change = millis();
        }
    }

    // Button 2 (D34) handler: Hold > 3s starts playback, normal click stops playback
    if (btn2State) {
        if (!btn2WasPressed) {
            btn2PressStart = millis();
            btn2WasPressed = true;
            btn2Triggered = false;
        } else if (!btn2Triggered) {
            uint32_t pressDuration = millis() - btn2PressStart;
            if (!playbackActive && pressDuration >= 3000) {
                // Hold for more than 3 seconds starts playback immediately while held
                SPILock lock;
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
                btn2Triggered = true;
            }
        }
    } else {
        if (btn2WasPressed) {
            if (playbackActive) {
                // Any normal click when playback is active stops it
                if (!btn2Triggered) {
                    stopPlayback();
                }
            } else {
                if (!btn2Triggered) {
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
            btn2Triggered = false;
        }
    }
}
