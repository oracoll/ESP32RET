#include "display_manager.h"
#include "sd_logger.h"

DisplayManager displayManager;

DisplayManager::DisplayManager()
    : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET),
      trackedCount(0), lastDisplayUpdate(0), totalFramesRx(0)
{
    memset(tracked, 0, sizeof(tracked));
}

void DisplayManager::setup() {
    Wire.begin();
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("SSD1306 OLED initialization failed!");
    } else {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("Nano R4 RET Ready");
        display.display();
    }
}

void DisplayManager::updateFrame(const CAN_FRAME &frame) {
    totalFramesRx++;
    int foundIdx = -1;

    for (int i = 0; i < trackedCount; i++) {
        if (tracked[i].id == frame.id) {
            foundIdx = i;
            break;
        }
    }

    if (foundIdx != -1) {
        // Update data in place for existing CAN ID
        tracked[foundIdx].length = frame.length;
        memcpy(tracked[foundIdx].data, frame.data, frame.length);
        tracked[foundIdx].lastSeen = millis();
    } else {
        // Add new CAN ID or overwrite oldest
        if (trackedCount < MAX_TRACKED_IDS) {
            foundIdx = trackedCount++;
        } else {
            uint32_t oldest = millis();
            foundIdx = 0;
            for (int i = 0; i < MAX_TRACKED_IDS; i++) {
                if (tracked[i].lastSeen < oldest) {
                    oldest = tracked[i].lastSeen;
                    foundIdx = i;
                }
            }
        }
        tracked[foundIdx].id = frame.id;
        tracked[foundIdx].extended = frame.extended;
        tracked[foundIdx].length = frame.length;
        memcpy(tracked[foundIdx].data, frame.data, frame.length);
        tracked[foundIdx].lastSeen = millis();
    }
}

void DisplayManager::renderDisplay() {
    display.clearDisplay();

    // Top status line
    display.setCursor(0, 0);
    display.print("LOG:");
    display.print(sdLogger.isLoggingActive() ? "REC" : "OFF");

    display.print(" RPL:");
    display.print(sdLogger.isPlaybackActive() ? "PLAY" : "OFF");

    display.setCursor(96, 0);
    display.print(settings.canSettings[0].nomSpeed / 1000);
    display.print("k");

    display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

    // Dynamic CAN table header / rows
    int y = 12;
    for (int i = 0; i < trackedCount && i < MAX_TRACKED_IDS; i++) {
        display.setCursor(0, y);
        if (tracked[i].extended) {
            char idBuf[10];
            sprintf(idBuf, "%06lX", (unsigned long)tracked[i].id);
            display.print(idBuf);
        } else {
            char idBuf[6];
            sprintf(idBuf, "%03lX", (unsigned long)tracked[i].id);
            display.print(idBuf);
        }

        display.print(" ");
        for (int d = 0; d < tracked[i].length && d < 4; d++) {
            char bBuf[4];
            sprintf(bBuf, "%02X", tracked[i].data[d]);
            display.print(bBuf);
        }

        y += 10;
    }

    display.display();
}

void DisplayManager::loop() {
    if (millis() - lastDisplayUpdate >= 100) { // Update OLED every 100ms
        lastDisplayUpdate = millis();
        renderDisplay();
    }
}
