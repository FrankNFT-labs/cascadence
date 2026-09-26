// Host-side stand-in for the parts of the Arduino API that ADSR.ino uses, so the
// sketch compiles as ordinary C++ on a PC. The functions are defined as a fake
// Cascadence board in test_adsr.cpp, after the sketch is included, so the fake
// can use the sketch's own pin constants.
#ifndef FAKE_ARDUINO_H
#define FAKE_ARDUINO_H

#include <math.h>
#include <stdint.h>

typedef bool boolean;
typedef uint8_t byte;

enum { LOW = 0, HIGH = 1 };
enum { INPUT = 0, OUTPUT = 1, INPUT_PULLUP = 2 };
enum { LSBFIRST = 0, MSBFIRST = 1 };

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);
int analogRead(uint8_t channel);
void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t value);

#endif
