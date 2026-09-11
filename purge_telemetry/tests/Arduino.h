#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
using byte = uint8_t;
constexpr int HIGH=1, LOW=0, OUTPUT=1, INPUT_PULLUP=2;
extern uint32_t testMillis;
extern int pinStates[20];
inline unsigned long millis() { return testMillis; }
inline void pinMode(int,int) {}
inline void digitalWrite(int pin,int state) { pinStates[pin]=state; }
inline int digitalRead(int pin) { return pinStates[pin]; }
