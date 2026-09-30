// Host-side stand-in for the avr-libc EEPROM calls the sketches use, so a
// sketch that includes <avr/eeprom.h> compiles on a PC. fake_board.cpp backs
// them with the fake board's EEPROM, which starts erased, every byte 0xFF.
#ifndef FAKE_AVR_EEPROM_H
#define FAKE_AVR_EEPROM_H

#include <stdint.h>

uint8_t eeprom_read_byte(const uint8_t *address);
void eeprom_update_byte(uint8_t *address, uint8_t value);

#endif
