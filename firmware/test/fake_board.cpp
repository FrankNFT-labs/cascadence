#include "fake_board.h"

#include <random>
#include <string>

#include "fake_arduino.h"

// Defined by the sketch that each test binary includes.
void setup();
void loop();

namespace fake {

Board board;

void at(uint64_t time_us, std::function<void()> change) { board.changes.emplace(time_us, change); }

void advance(uint64_t us) {
  board.now_us += us;
  while (!board.changes.empty() && board.changes.begin()->first <= board.now_us) {
    std::function<void()> change = board.changes.begin()->second;
    board.changes.erase(board.changes.begin());
    change();
  }
  if (board.now_us >= board.end_us) throw ScenarioEnd{};
}

std::function<bool(uint64_t)> clock_pulses(uint64_t period_us, uint64_t width_us, uint64_t first_us) {
  return [=](uint64_t now_us) { return now_us >= first_us && (now_us - first_us) % period_us < width_us; };
}

void boot() {
  static bool booted = false;
  if (booted) throw TestFailure{"boot() called twice: the fork is the reset, boot once per test"};
  booted = true;
  ::setup();
}

void run_until(uint64_t until_us) {
  board.end_us = until_us;
  try {
    for (;;) ::loop();
  } catch (const ScenarioEnd &) {
  }
  board.end_us = UINT64_MAX;
}

std::vector<Pulse> pulses(int channel) {
  std::vector<Pulse> found;
  bool high = false;
  for (const DacWrite &write : board.dac[channel]) {
    bool now_high = write.value >= (FULL_SCALE + 1) / 2;
    if (now_high && !high) found.push_back(Pulse{write.time_us, 0});
    if (!now_high && high) found.back().end_us = write.time_us;
    high = now_high;
  }
  return found;
}

std::vector<unsigned> values_written_while_clock_high(int channel) {
  std::vector<unsigned> values;
  for (const DacWrite &write : board.dac[channel])
    if (write.input_high) values.push_back(write.value);
  return values;
}

std::string steps_with_pulses(int channel, uint64_t first_us, uint64_t period_us, int clocks) {
  std::string steps(clocks, '.');
  for (const Pulse &pulse : pulses(channel)) {
    if (pulse.start_us < first_us) continue;
    uint64_t step = (pulse.start_us - first_us) / period_us;
    if (step < steps.size()) steps[step] = 'x';
  }
  return steps;
}

int knob_for(long wanted, long out_min, long out_max, long in_max) {
  for (int knob = 0; knob <= 1023; ++knob)
    if (::map(knob, 0, in_max, out_min, out_max) == wanted) return knob;
  throw TestFailure{"no knob position maps to " + std::to_string(wanted)};
}

}  // namespace fake

using fake::board;

void pinMode(uint8_t, uint8_t) {}

void digitalWrite(uint8_t pin, uint8_t value) {
  fake::advance(board.digital_write_us);
  if (pin != fake::DAC_CS_PIN) return;
  if (value == LOW) {  // chip select falls: a new 16-bit frame starts
    board.dac_selected = true;
    board.frame.clear();
    return;
  }
  if (board.dac_selected) {  // chip select rises: the DAC latches the frame
    if (board.frame.size() != 2)
      throw fake::TestFailure{"DAC frame had " + std::to_string(board.frame.size()) + " bytes, expected 2"};
    unsigned word = (board.frame[0] << 8) | board.frame[1];
    if (((word >> 12) & 1) != 1)
      throw fake::TestFailure{"DAC frame has the shutdown bit clear: the output would be muted"};
    if (((word >> 13) & 1) != 0)
      throw fake::TestFailure{"DAC frame selects 1x gain: the module drives its outputs at 2x"};
    board.dac[(word >> 15) & 1].push_back(fake::DacWrite{word & 0x0FFF, board.now_us, board.input_high});
  }
  board.dac_selected = false;
}

void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t value) {
  fake::advance(board.shift_out_us);
  if (dataPin != fake::DAC_DATA_PIN || clockPin != fake::DAC_CLOCK_PIN)
    throw fake::TestFailure{"shiftOut on the wrong pins: the DAC's data line is pin 6 and its clock pin 4"};
  if (bitOrder != MSBFIRST) throw fake::TestFailure{"shiftOut LSB first: the MCP48x2 expects MSB first"};
  if (board.dac_selected) board.frame.push_back(value);
}

int digitalRead(uint8_t pin) {
  fake::advance(board.digital_read_us);
  if (pin == fake::CLOCK_IN_PIN) {
    board.input_high = board.clock_input(board.now_us);
    return board.input_high ? LOW : HIGH;  // the input transistor inverts the jack
  }
  if (pin == fake::TOGGLE_PIN) return board.toggle_left ? HIGH : LOW;
  return LOW;
}

int analogRead(uint8_t channel) {
  fake::advance(board.analog_read_us);
  board.sampled_adc_channels.insert(channel);
  return channel < fake::POT_COUNT ? board.pot[channel] : 0;  // ADC4 is the DAC clock line, which idles low
}

void delay(unsigned long ms) { fake::advance(static_cast<uint64_t>(ms) * fake::MS); }

unsigned long micros() {
  fake::advance(4);
  return board.now_us;
}

unsigned long millis() {
  fake::advance(4);
  return board.now_us / fake::MS;
}

// Arduino's own definitions from WMath.cpp, with a fixed pseudo-random sequence.
long random(long howbig) {
  if (howbig == 0) return 0;
  if (board.random_below) return board.random_below(howbig);
  static std::minstd_rand generator(1);
  return static_cast<long>(generator()) % howbig;
}

long random(long howsmall, long howbig) {
  if (howsmall >= howbig) return howsmall;
  return random(howbig - howsmall) + howsmall;
}

long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
