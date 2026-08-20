#ifndef DISPLAY_MANAGER_H_
#define DISPLAY_MANAGER_H_

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "can_manager.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

#define MAX_TRACKED_IDS 5

struct TrackedCANMessage {
    uint32_t id;
    uint8_t length;
    uint8_t data[8];
    uint32_t lastSeen;
    bool extended;
};

class DisplayManager {
public:
    DisplayManager();
    void setup();
    void updateFrame(const CAN_FRAME &frame);
    void loop();

private:
    Adafruit_SSD1306 display;
    TrackedCANMessage tracked[MAX_TRACKED_IDS];
    int trackedCount;
    uint32_t lastDisplayUpdate;
    uint32_t totalFramesRx;

    void renderDisplay();
};

extern DisplayManager displayManager;

#endif
