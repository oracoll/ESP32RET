/*
 ESP32RET.ino

 Created: June 1, 2020
 Author: Collin Kidder

Copyright (c) 2014-2020 Collin Kidder, Michael Neuweiler

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "config.h"
#include <esp32_can.h>
#include <SPI.h>
#include <esp32_mcp2517fd.h>
#include <Preferences.h>
#include <FastLED.h>
#include "SerialConsole.h"
#include "gvret_comm.h"
#include "can_manager.h"
#include "lawicel.h"
#include "sd_logger.h"

//on the S3 we want the default pins to be different
#ifdef CONFIG_IDF_TARGET_ESP32S3
MCP2517FD CAN1(10, 3);
#endif

byte i = 0;

uint32_t lastFlushMicros = 0;

bool markToggle[6];
uint32_t lastMarkTrigger = 0;

EEPROMSettings settings;
SystemSettings SysSettings;
Preferences nvPrefs;
char deviceName[20];
char otaHost[40];
char otaFilename[100];

uint8_t espChipRevision;

GVRET_Comm_Handler serialGVRET; //gvret protocol over the serial to USB connection
CANManager canManager; //keeps track of bus load and abstracts away some details of how things are done
LAWICELHandler lawicel;

SerialConsole console;

CRGB leds[A5_NUM_LEDS]; //A5 has the largest # of LEDs so use that one even for A0 or EVTV

CAN_COMMON *canBuses[NUM_BUSES];

// Custom feature global variables
uint32_t lastHostActivity = 0;
uint32_t lastTxTraffic = 0;
uint32_t lastRxTraffic = 0;

uint32_t orangeBlinkActiveUntil = 0;
uint32_t yellowBlinkActiveUntil = 0;
uint32_t purpleBlinkActiveUntil = 0;

//initializes all the system EEPROM values. Chances are this should be broken out a bit but
//there is only one checksum check for all of them so it's simple to do it all here.
void loadSettings()
{
    Logger::console("Loading settings....");

    for (int i = 0; i < NUM_BUSES; i++) canBuses[i] = nullptr;

    nvPrefs.begin(PREF_NAME, false);

    settings.useBinarySerialComm = nvPrefs.getBool("binarycomm", false);
    settings.logLevel = nvPrefs.getUChar("loglevel", 1); //info
    settings.enableLawicel = nvPrefs.getBool("enableLawicel", true);

    uint8_t defaultVal = (espChipRevision > 2) ? 0 : 1; //0 = A0, 1 = EVTV ESP32
#ifdef CONFIG_IDF_TARGET_ESP32S3
    defaultVal = 3;
#endif
    settings.systemType = nvPrefs.getUChar("systype", defaultVal);

    if (settings.systemType == 0)
    {
        Logger::console("Running on Macchina A0");
        canBuses[0] = &CAN0;
        SysSettings.LED_CANTX = 255;
        SysSettings.LED_CANRX = 255;
        SysSettings.LED_LOGGING = 255;
        SysSettings.LED_CONNECTION_STATUS = 0;
        SysSettings.fancyLED = true;
        SysSettings.logToggle = false;
        SysSettings.txToggle = true;
        SysSettings.rxToggle = true;
        SysSettings.lawicelAutoPoll = false;
        SysSettings.lawicelMode = false;
        SysSettings.lawicellExtendedMode = false;
        SysSettings.lawicelTimestamping = false;
        SysSettings.numBuses = 1; //Currently we support CAN0
        strcpy(deviceName, MACC_NAME);
        strcpy(otaHost, "macchina.cc");
        strcpy(otaFilename, "/a0/files/a0ret.bin");
        pinMode(13, OUTPUT);
        digitalWrite(13, LOW);
        delay(100);
        FastLED.addLeds<LED_TYPE, A0_LED_PIN, COLOR_ORDER>(leds, A0_NUM_LEDS).setCorrection( TypicalLEDStrip );
        FastLED.setBrightness(  BRIGHTNESS );
        leds[0] = CRGB::Red;
        FastLED.show();
        pinMode(21, OUTPUT);
        digitalWrite(21, LOW);
        CAN0.setCANPins(GPIO_NUM_4, GPIO_NUM_5);
    }

    if (settings.systemType == 1)
    {
        Logger::console("Running on EVTV ESP32 Board");
        canBuses[0] = &CAN0;
        canBuses[1] = &CAN1;
        SysSettings.LED_CANTX = 255;
        SysSettings.LED_CANRX = 255;
        SysSettings.LED_LOGGING = 255;
        SysSettings.LED_CONNECTION_STATUS = 255;
        SysSettings.fancyLED = false;
        SysSettings.logToggle = false;
        SysSettings.txToggle = true;
        SysSettings.rxToggle = true;
        SysSettings.lawicelAutoPoll = false;
        SysSettings.lawicelMode = false;
        SysSettings.lawicellExtendedMode = false;
        SysSettings.lawicelTimestamping = false;
        SysSettings.numBuses = 2;
        strcpy(deviceName, EVTV_NAME);
        strcpy(otaHost, "media3.evtv.me");
        strcpy(otaFilename, "/esp32ret.bin");
    }

    if (settings.systemType == 2)
    {
        Logger::console("Running on Macchina 5-CAN");
        canBuses[0] = &CAN0; //SWCAN on this hardware - DLC pin 1
        canBuses[1] = &CAN1; //DLC pins 1 and 9. Overlaps with SWCAN
        canBuses[2] = new MCP2517FD(33, 39); //DLC pins 3/11
        canBuses[3] = new MCP2517FD(25, 34); //DLC pins 6/14
        canBuses[4] = new MCP2517FD(14, 13); //DLC pins 12/13

        //reconfigure the two already defined CAN buses to use the actual pins for this board.
        CAN0.setCANPins(GPIO_NUM_4, GPIO_NUM_5); //rx, tx - This is the SWCAN interface
        CAN1.setINTPin(36);
        CAN1.setCSPin(32);
        SysSettings.LED_CANTX = 0;
        SysSettings.LED_CANRX = 1;
        SysSettings.LED_LOGGING = 2;
        SysSettings.LED_CONNECTION_STATUS = 3;
        SysSettings.fancyLED = true;
        SysSettings.logToggle = false;
        SysSettings.txToggle = true;
        SysSettings.rxToggle = true;
        SysSettings.lawicelAutoPoll = false;
        SysSettings.lawicelMode = false;
        SysSettings.lawicellExtendedMode = false;
        SysSettings.lawicelTimestamping = false;
        SysSettings.numBuses = 5;


        FastLED.addLeds<LED_TYPE, A5_LED_PIN, COLOR_ORDER>(leds, A5_NUM_LEDS).setCorrection( TypicalLEDStrip );
        FastLED.setBrightness(  BRIGHTNESS );
        //With the board facing up and looking at the USB end the LEDs are 0 1 2 (USB) 3
        //can test LEDs here for debugging but normally leave first three off and set connection to RED.
        //leds[0] = CRGB::White;
        //leds[1] = CRGB::Blue;
        //leds[2] = CRGB::Green;
        leds[3] = CRGB::Red;
        FastLED.show();

        strcpy(deviceName, MACC_NAME);
        strcpy(otaHost, "macchina.cc");
        strcpy(otaFilename, "/a0/files/a0ret.bin");
        //Single wire interface
        pinMode(SW_EN, OUTPUT);
        pinMode(SW_MODE0, OUTPUT);
        pinMode(SW_MODE1, OUTPUT);
        digitalWrite(SW_EN, LOW);      //MUST be LOW to use CAN1 channel
        //HH = Normal Mode
        digitalWrite(SW_MODE0, HIGH);
        digitalWrite(SW_MODE1, HIGH);
    }

    if (settings.systemType == 3)
    {
        Logger::console("Running on EVTV ESP32-S3 Board");
        canBuses[0] = &CAN0;
        canBuses[1] = &CAN1;
        SysSettings.LED_CANTX = 255;//18;
        SysSettings.LED_CANRX = 255;//18;
        SysSettings.LED_LOGGING = 255;
        SysSettings.LED_CONNECTION_STATUS = 255;
        SysSettings.fancyLED = false;
        SysSettings.logToggle = false;
        SysSettings.txToggle = true;
        SysSettings.rxToggle = true;
        SysSettings.lawicelAutoPoll = false;
        SysSettings.lawicelMode = false;
        SysSettings.lawicellExtendedMode = false;
        SysSettings.lawicelTimestamping = false;
        SysSettings.numBuses = 2;
        strcpy(deviceName, EVTV_NAME);
        strcpy(otaHost, "media3.evtv.me");
        strcpy(otaFilename, "/esp32s3ret.bin");
    }

    char buff[80];
    for (int i = 0; i < SysSettings.numBuses; i++)
    {
        sprintf(buff, "can%ispeed", i);
        settings.canSettings[i].nomSpeed = nvPrefs.getUInt(buff, 500000);
        sprintf(buff, "can%i_en", i);
        settings.canSettings[i].enabled = nvPrefs.getBool(buff, (i < 2)?true:false);
        sprintf(buff, "can%i-listenonly", i);
        settings.canSettings[i].listenOnly = nvPrefs.getBool(buff, false);
        sprintf(buff, "can%i-fdspeed", i);
        settings.canSettings[i].fdSpeed = nvPrefs.getUInt(buff, 5000000);
        sprintf(buff, "can%i-fdmode", i);
        settings.canSettings[i].fdMode = nvPrefs.getBool(buff, false);
    }

    nvPrefs.end();

    Logger::setLoglevel((Logger::LogLevel)settings.logLevel);

    for (int rx = 0; rx < NUM_BUSES; rx++) SysSettings.lawicelBusReception[rx] = true; //default to showing messages on RX
}

void setup()
{
#ifdef CONFIG_IDF_TARGET_ESP32S3
    //for the ESP32S3 it will block if nothing is connected to USB and that can slow down the program
    //if nothing is connected. But, you can't set 0 or writing rapidly to USB will lose data. It needs
    //some sort of timeout but I'm not sure exactly how much is needed or if there is a better way
    //to deal with this issue.
    Serial.setTxTimeoutMs(2);
#endif
    Serial.begin(1000000); //for production

    espChipRevision = ESP.getChipRevision();

    Serial.print("Build number: ");
    Serial.println(CFG_BUILD_NUM);

    loadSettings();

    // Initialize custom Addressable RGB LED (WS2812) on Pin D2
    FastLED.addLeds<WS2812B, 2, GRB>(leds, 1).setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(190);
    leds[0] = CRGB::Blue;
    FastLED.show();

    // Setup custom SD Card Logger module (handles D15, D34, SPI, SD)
    sdLogger.setup();

    canManager.setup();

    SysSettings.lawicelMode = false;
    SysSettings.lawicelAutoPoll = false;
    SysSettings.lawicelTimestamping = false;
    SysSettings.lawicelPollCounter = 0;

    Serial.print("Free heap after setup: ");
    Serial.println(esp_get_free_heap_size());

    Serial.print("Done with init\n");
}

/*
Send a fake frame out USB and maybe to file to show where the mark was triggered at. The fake frame has bits 31 through 3
set which can never happen in reality since frames are either 11 or 29 bit IDs. So, this is a sign that it is a mark frame
and not a real frame. The bottom three bits specify which mark triggered.
*/
void sendMarkTriggered(int which)
{
    CAN_FRAME frame;
    frame.id = 0xFFFFFFF8ull + which;
    frame.extended = true;
    frame.length = 0;
    frame.rtr = 0;
    canManager.displayFrame(frame, 0);
}

/*
Loop executes as often as possible all the while interrupts fire in the background.
The serial comm protocol is as follows:
All commands start with 0xF1 this helps to synchronize if there were comm issues
Then the next byte specifies which command this is.
Then the command data bytes which are specific to the command
Lastly, there is a checksum byte just to be sure there are no missed or duped bytes
Any bytes between checksum and 0xF1 are thrown away

Yes, this should probably have been done more neatly but this way is likely to be the
fastest and safest with limited function calls
*/
void loop()
{
    bool isConnected = false;
    int serialCnt;
    uint8_t in_byte;

    isConnected = true;

    if (SysSettings.lawicelPollCounter > 0) SysSettings.lawicelPollCounter--;

    canManager.loop();

    // Loop the SD Logger and Button processing
    sdLogger.loop();

    // Custom LED State Machine and Priority Logic
    if (sdLogger.getAndClearOrangeBlink()) {
        orangeBlinkActiveUntil = millis() + 1500;
    }
    if (sdLogger.getAndClearYellowBlink()) {
        yellowBlinkActiveUntil = millis() + 1500;
    }
    if (sdLogger.getAndClearPurpleBlink()) {
        purpleBlinkActiveUntil = millis() + 1500;
    }

    CRGB ledColor = CRGB::Black;

    if (millis() < orangeBlinkActiveUntil) {
        // Blink Orange (200ms cycle: 100ms orange, 100ms off)
        ledColor = (millis() % 200 < 100) ? CRGB(255, 60, 0) : CRGB::Black;
    } else if (millis() < yellowBlinkActiveUntil) {
        // Blink Yellow (200ms cycle: 100ms yellow, 100ms off)
        ledColor = (millis() % 200 < 100) ? CRGB(255, 255, 0) : CRGB::Black;
    } else if (millis() < purpleBlinkActiveUntil) {
        // Blink Purple (200ms cycle: 100ms purple, 100ms off)
        ledColor = (millis() % 200 < 100) ? CRGB(128, 0, 128) : CRGB::Black;
    } else if (millis() - lastTxTraffic < 80) {
        // Blink Red for CAN TX Traffic
        ledColor = CRGB::Red;
    } else if (millis() - lastRxTraffic < 80) {
        // Blink Green for CAN RX Traffic
        ledColor = CRGB::Green;
    } else if (sdLogger.isLoggingActive()) {
        // Blink fast purple for SD logging active (300ms cycle: 150ms purple, 150ms off)
        ledColor = (millis() % 300 < 150) ? CRGB(128, 0, 128) : CRGB::Black;
    } else if (millis() - lastHostActivity < 2000) {
        // Connected to SavvyCAN - Blue Heartbeat (double pulse, 1000ms cycle)
        uint32_t t = millis() % 1000;
        uint8_t b = 0;
        if (t < 150) {
            b = map(t, 0, 150, 0, 255);
        } else if (t < 300) {
            b = map(t, 150, 300, 255, 0);
        } else if (t < 450) {
            b = map(t, 300, 450, 0, 255);
        } else if (t < 600) {
            b = map(t, 450, 600, 255, 0);
        }
        ledColor = CRGB(0, 0, b);
    } else {
        // Solid Blue when not connected to SavvyCAN
        ledColor = CRGB::Blue;
    }

    leds[0] = ledColor;
    FastLED.show();

    size_t serialLength = serialGVRET.numAvailableBytes();

    //If the max time has passed or the buffer is almost filled then send buffered data out
    if ((micros() - lastFlushMicros > SER_BUFF_FLUSH_INTERVAL) || (serialLength > (SER_BUFF_SIZE - 40)) )
    {
        lastFlushMicros = micros();
        if (serialLength > 0)
        {
            Serial.write(serialGVRET.getBufferedBytes(), serialLength);
            serialGVRET.clearBufferedBytes();
        }
    }

    serialCnt = 0;
    while ( (Serial.available() > 0) && serialCnt < 128 )
    {
        serialCnt++;
        in_byte = Serial.read();
        serialGVRET.processIncomingByte(in_byte);
    }
}
