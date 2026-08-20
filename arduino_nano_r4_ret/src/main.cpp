#include <Arduino.h>
#include "config.h"
#include "sys_io.h"
#include "can_manager.h"
#include "sd_logger.h"
#include "gvret_comm.h"
#include "lawicel.h"
#include "SerialConsole.h"
#include "display_manager.h"

GVRET_Comm_Handler serialGVRET;
SerialConsole console;
CANManager canManager;
LAWICELHandler lawicel;

uint32_t lastHostActivity = 0;
uint32_t lastTxTraffic = 0;
uint32_t lastRxTraffic = 0;

static uint32_t lastSerBufferFlush = 0;

void setup() {
    sys_early_setup();

    Serial.begin(115200);

    loadSettings();
    setup_sys_io();

    displayManager.setup();
    canManager.setup();
    sdLogger.setup();

    console.printMenu();
}

void loop() {
    // Process incoming serial traffic
    while (Serial.available() > 0) {
        uint8_t in_byte = Serial.read();
        serialGVRET.processIncomingByte(in_byte);
    }

    // Flush outgoing serial buffer
    size_t availBytes = serialGVRET.numAvailableBytes();
    if (availBytes > 0) {
        if (availBytes >= (SER_BUFF_SIZE / 2) || (micros() - lastSerBufferFlush >= SER_BUFF_FLUSH_INTERVAL)) {
            Serial.write(serialGVRET.getBufferedBytes(), availBytes);
            serialGVRET.clearBufferedBytes();
            lastSerBufferFlush = micros();
        }
    }

    canManager.loop();
    sdLogger.loop();
    displayManager.loop();
}
