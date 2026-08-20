/*
 * config.h
 *
 * Configured specifically for Arduino Nano R4 RET.
 */

#ifndef CONFIG_H_
#define CONFIG_H_

#include <Arduino.h>
#include <EEPROM.h>
#include <Arduino_CAN.h>

// Size to use for buffering writes to USB Serial
#define SER_BUFF_SIZE       2048

// Number of microseconds between hard flushes of the serial buffer
#define SER_BUFF_FLUSH_INTERVAL 20000

#define CFG_BUILD_NUM   700
#define CFG_VERSION "Nano R4 RET v1.0"
#define PREF_NAME   "NANO4RET"
#define EVTV_NAME   "NANO4RET"
#define MACC_NAME   "NANO4RET"

#define NUM_ANALOG  4
#define NUM_DIGITAL 2
#define NUM_OUTPUT  2

#define NUM_BUSES   1   // Arduino Nano R4 has 1 internal CAN bus

#define BLINK_SLOWNESS  10

// Pin map definitions for Arduino Nano R4 RET
#define CAN_TX_PIN      4  // D4 -> TJA1050 TXD (RA4M1 fixed pin)
#define CAN_RX_PIN      5  // D5 <- TJA1050 RXD (RA4M1 fixed pin)
#define WS2812_PIN      9  // D9 -> WS2812B DIN
#define SD_CS_PIN      10  // D10 -> SD Card CS (SPI: D11 MOSI, D12 MISO, D13 SCK)
#define BTN_LOG_PIN     2  // D2 -> Button 1 (LOG), active LOW
#define BTN_REPLAY_PIN  3  // D3 -> Button 2 (REPLAY), active LOW
#define OLED_SDA_PIN   A4  // A4 <> OLED SDA (I2C)
#define OLED_SCL_PIN   A5  // A5 <> OLED SCL (I2C)

#define NUM_LEDS        1

struct FILTER {  // 10 bytes
    uint32_t id;
    uint32_t mask;
    boolean extended;
    boolean enabled;
} __attribute__((__packed__));

struct CANFDSettings {
    uint32_t nomSpeed;
    uint32_t fdSpeed;
    boolean enabled;
    boolean listenOnly;
    boolean fdMode;
};

struct EEPROMSettings {
    CANFDSettings canSettings[NUM_BUSES];
    boolean useBinarySerialComm; // use binary protocol or human readable
    uint8_t logLevel;            // level of logging
    uint8_t systemType;          // 0 = Nano R4 RET
    boolean enableLawicel;
} __attribute__((__packed__));

struct SystemSettings {
    uint8_t LED_CANTX;
    uint8_t LED_CANRX;
    uint8_t LED_LOGGING;
    uint8_t LED_CONNECTION_STATUS;
    boolean fancyLED;
    boolean txToggle;
    boolean rxToggle;
    boolean logToggle;
    boolean lawicelMode;
    boolean lawicellExtendedMode;
    boolean lawicelAutoPoll;
    boolean lawicelTimestamping;
    int lawicelPollCounter;
    boolean lawicelBusReception[NUM_BUSES];
    int8_t numBuses;
};

class GVRET_Comm_Handler;
class SerialConsole;
class CANManager;
class LAWICELHandler;

extern EEPROMSettings settings;
extern SystemSettings SysSettings;
extern GVRET_Comm_Handler serialGVRET;
extern SerialConsole console;
extern CANManager canManager;
extern LAWICELHandler lawicel;
extern char deviceName[20];

void loadSettings();
void saveSettings();

#endif /* CONFIG_H_ */
