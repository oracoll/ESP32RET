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
}

void SDLogger::setup() {
    pinMode(15, INPUT_PULLDOWN);
    pinMode(34, INPUT); // D34 is input-only, no internal pull-downs in hardware

    // Initialize SPI on pins 18, 19, 23, 5
    SPI.begin(18, 19, 23, 5);

    // Check if card is present on boot
    checkSDCard();
}

void SDLogger::checkSDCard() {
    // Try to initialize SD card
    if (SD.begin(5, SPI)) {
        cardPresent = true;
        Serial.println("SD Card detected and initialized successfully!");
    } else {
        cardPresent = false;
        Serial.println("No SD Card detected.");
    }
}

String SDLogger::getNextFileName() {
    char nameBuf[32];
    // Find next unused log file index
    while (logIndex < 1000) {
        sprintf(nameBuf, "/log_%03d.csv", logIndex);
        if (!SD.exists(nameBuf)) {
            return String(nameBuf);
        }
        logIndex++;
    }
    return String("/log_999.csv");
}

void SDLogger::startLogging() {
    checkSDCard();
    if (!cardPresent) {
        Serial.println("Failed to start logging: SD Card not present.");
        orangeBlink = true;
        return;
    }

    String filename = getNextFileName();
    logFile = SD.open(filename, FILE_WRITE);
    if (logFile) {
        loggingActive = true;
        yellowBlink = true;
        Serial.print("Started logging to: ");
        Serial.println(filename);

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
    static uint32_t lastFlush = 0;

    // Periodically flush the file to protect against data loss
    if (loggingActive && logFile && (millis() - lastFlush > 500)) {
        logFile.flush();
        lastFlush = millis();
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
