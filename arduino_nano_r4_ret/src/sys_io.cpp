/*
 * sys_io.cpp
 *
 * Low level I/O and WS2812 NeoPixel handling for Arduino Nano R4 RET
 */

#include "sys_io.h"
#include <Adafruit_NeoPixel.h>

static Adafruit_NeoPixel neoPixel(NUM_LEDS, WS2812_PIN, NEO_GRB + NEO_KHZ800);

EEPROMSettings settings;
SystemSettings SysSettings;
char deviceName[20] = "Nano R4 RET";

void loadSettings() {
    EEPROM.get(0, settings);
    if (settings.systemType != 0x99) { // default setup if uninitialized
        settings.canSettings[0].nomSpeed = 500000;
        settings.canSettings[0].enabled = true;
        settings.canSettings[0].listenOnly = false;
        settings.canSettings[0].fdMode = false;
        settings.useBinarySerialComm = true;
        settings.logLevel = 1;
        settings.systemType = 0x99; // valid header signature
        settings.enableLawicel = true;
        saveSettings();
    }
}

void saveSettings() {
    EEPROM.put(0, settings);
}

void sys_early_setup() {
    pinMode(BTN_LOG_PIN, INPUT_PULLUP);
    pinMode(BTN_REPLAY_PIN, INPUT_PULLUP);
}

void setup_sys_io() {
    neoPixel.begin();
    neoPixel.setBrightness(50);
    neoPixel.setPixelColor(0, neoPixel.Color(0, 0, 255)); // Blue on startup
    neoPixel.show();

    SysSettings.LED_CANRX = 0;
    SysSettings.LED_CANTX = 0;
    SysSettings.LED_LOGGING = 0;
    SysSettings.fancyLED = true;
    SysSettings.numBuses = 1;
    SysSettings.lawicelBusReception[0] = true;
}

void setLED(uint8_t pin, boolean state) {
    if (state) {
        neoPixel.setPixelColor(0, neoPixel.Color(0, 255, 0)); // Green
    } else {
        neoPixel.setPixelColor(0, neoPixel.Color(0, 0, 0));
    }
    neoPixel.show();
}

void toggleRXLED() {
    static int counter = 0;
    counter++;
    if (counter >= BLINK_SLOWNESS) {
        counter = 0;
        SysSettings.rxToggle = !SysSettings.rxToggle;
        if (SysSettings.rxToggle) {
            neoPixel.setPixelColor(0, neoPixel.Color(0, 0, 255)); // Blue for RX
        } else {
            neoPixel.setPixelColor(0, neoPixel.Color(0, 0, 0));
        }
        neoPixel.show();
    }
}

void toggleTXLED() {
    static int counter = 0;
    counter++;
    if (counter >= BLINK_SLOWNESS) {
        counter = 0;
        SysSettings.txToggle = !SysSettings.txToggle;
        if (SysSettings.txToggle) {
            neoPixel.setPixelColor(0, neoPixel.Color(0, 255, 0)); // Green for TX
        } else {
            neoPixel.setPixelColor(0, neoPixel.Color(0, 0, 0));
        }
        neoPixel.show();
    }
}
