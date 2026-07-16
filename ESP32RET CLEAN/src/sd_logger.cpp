#include "sd_logger.h"

SDLogger sdLogger;

SDLogger::SDLogger() {
    cardPresent = false;
    loggingActive = false;
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
    if (loggingActive) return; // Do not interrupt active logging

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

String SDLogger::getNextFileName() {
    char nameBuf[32];
    sprintf(nameBuf, "/log_%03d.csv", logIndex);
    logIndex++; // Increment for the next start logging session
    return String(nameBuf);
}

void SDLogger::startLogging() {
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

        // Write SavvyCAN CSV header row with Tab separation
        logFile.print("Time Stamp\tID\tExtended\tDir\tBus\tLEN\tD1\tD2\tD3\tD4\tD5\tD6\tD7\tD8\n");
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

void SDLogger::logFrame(CAN_FRAME &frame, int bus, int dir) {
    if (!loggingActive || !logFile) return;

    // Log in SavvyCAN tab-separated format
    // Format: Time Stamp\tID\tExtended\tDir\tBus\tLEN\tD1\tD2\tD3\tD4\tD5\tD6\tD7\tD8
    logFile.printf("%u\t0x%X\t%s\t%d\t%d\t%d",
                   micros(),
                   frame.id,
                   frame.extended ? "true" : "false",
                   dir,
                   bus,
                   frame.length);

    for (int i = 0; i < 8; i++) {
        if (i < frame.length) {
            logFile.printf("\t%02X", frame.data.uint8[i]);
        } else {
            logFile.print("\t");
        }
    }
    logFile.print("\n");
}

void SDLogger::logFrameFD(CAN_FRAME_FD &frame, int bus, int dir) {
    if (!loggingActive || !logFile) return;

    // Fallback: log FD frame as standard CAN frame in the CSV (standard loggers usually downsample or format up to 8 bytes for CSV)
    logFile.printf("%u\t0x%X\t%s\t%d\t%d\t%d",
                   micros(),
                   frame.id,
                   frame.extended ? "true" : "false",
                   dir,
                   bus,
                   frame.length > 8 ? 8 : frame.length);

    for (int i = 0; i < 8; i++) {
        if (i < frame.length) {
            logFile.printf("\t%02X", frame.data.uint8[i]);
        } else {
            logFile.print("\t");
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

    // Button 2 (D34) handler: Check SD card status on release
    bool btn2State = (digitalRead(34) == HIGH);
    if (btn2State) {
        if (!btn2WasPressed) {
            btn2PressStart = millis();
            btn2WasPressed = true;
        }
    } else {
        if (btn2WasPressed) {
            checkSDCard();
            if (cardPresent) {
                yellowBlink = true; // Trigger Yellow status blink
            } else {
                orangeBlink = true; // Trigger Orange status blink
            }
            btn2WasPressed = false;
        }
    }
}
