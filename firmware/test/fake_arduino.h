// Host-side stand-in for the parts of the Arduino API that the Cascadence
// sketches use, so a sketch compiles as ordinary C++ on a PC. fake_board.cpp
// defines these functions as a fake Cascadence board. Include this header after
// every standard header: the round() macro below must not reach <cmath>.
#ifndef FAKE_ARDUINO_H
#define FAKE_ARDUINO_H

#include <math.h>
#include <stdint.h>

// Arduino's round() is a macro, not libm's function: it adds a half and
// truncates to long, in the target's 4-byte float arithmetic, hence 0.5f.
#define round(x) ((x) >= 0 ? (long)((x) + 0.5f) : (long)((x) - 0.5f))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))

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
void delay(unsigned long ms);
unsigned long micros();
unsigned long millis();
long random(long howbig);
long random(long howsmall, long howbig);
void randomSeed(unsigned long seed);
long map(long x, long in_min, long in_max, long out_min, long out_max);

#endif
