/*
 * sys_io.h
 *
 * Handles raw interaction with system I/O on Arduino Nano R4 RET
 */

#ifndef SYS_IO_H_
#define SYS_IO_H_

#include <Arduino.h>
#include "config.h"
#include "Logger.h"

void sys_early_setup();
void setup_sys_io();
void setLED(uint8_t pin, boolean state);
void toggleTXLED();
void toggleRXLED();

#endif
