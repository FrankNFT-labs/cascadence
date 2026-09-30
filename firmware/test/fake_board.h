// A fake Cascadence board for host-side tests: the four pots, the A/B toggle,
// the clock or gate input behind its inverting transistor, and the MCP48x2 DAC
// on outputs A and B. fake_board.cpp implements the Arduino API from
// fake_arduino.h against it.
//
// Time is virtual. Every Arduino call advances it by a rough estimate of what
// the call takes on the ATtiny84 at 8 MHz, delay() advances it by the delay, and
// changes scheduled with at() happen when the time reaches them. This keeps
// sketches that busy-wait on the clock input moving, and lets a test end a
// sketch whose loop() never returns.
#ifndef FAKE_BOARD_H
#define FAKE_BOARD_H

#include <stdint.h>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fake {

// The board's wiring, from hardware/cascadence11.sch.
const int POT_COUNT = 4;       // ADC channels 0 to 3, top to bottom
const int TOGGLE_PIN = 7;      // reads HIGH while the lever is left
const int CLOCK_IN_PIN = 8;    // reads LOW while the jack is high: the input transistor inverts it
const int DAC_DATA_PIN = 6;
const int DAC_CLOCK_PIN = 4;
const int DAC_CS_PIN = 5;      // active low

const uint64_t MS = 1000;      // virtual time runs in microseconds
const unsigned FULL_SCALE = 4095;

// A failed check. The board throws it too, for a DAC frame the module would not
// play the way the sketch intends.
struct TestFailure {
  std::string message;
};

// Thrown when the virtual time reaches the end of the scenario, to unwind out of
// a loop() that never returns.
struct ScenarioEnd {};

struct DacWrite {
  unsigned value;    // 12-bit data field of the frame (the MCP4812 uses its top 10 bits)
  uint64_t time_us;  // virtual time of the write
  bool input_high;   // whether the most recent read of the clock input saw the jack high
};

struct Board {
  int pot[POT_COUNT] = {512, 512, 512, 512};  // ADC readings, 0 to 1023
  bool toggle_left = true;
  // Whether the jack on the clock input is high, asked on every read of it.
  std::function<bool(uint64_t now_us)> clock_input = [](uint64_t) { return false; };
  // What random(howbig) returns for howbig > 0. Unset, it draws from a fixed
  // pseudo-random sequence, the same in every run.
  std::function<long(long howbig)> random_below;
  // Estimated cost of each call on the ATtiny84 at 8 MHz, in microseconds.
  uint64_t analog_read_us = 112;
  uint64_t digital_read_us = 4;
  uint64_t digital_write_us = 4;
  uint64_t shift_out_us = 50;

  uint64_t now_us = 0;
  uint64_t end_us = UINT64_MAX;
  std::multimap<uint64_t, std::function<void()>> changes;
  std::set<int> sampled_adc_channels;
  std::vector<DacWrite> dac[2];
  bool input_high = false;  // what the latest read of the clock input saw
  bool dac_selected = false;
  std::vector<uint8_t> frame;
};

extern Board board;

// Schedules a change to the board, such as a knob turn or a toggle flip.
void at(uint64_t time_us, std::function<void()> change);

// Advances the virtual time, applies the changes that fall due, and throws
// ScenarioEnd once the time reaches board.end_us.
void advance(uint64_t us);

// A clock on the input: pulses width_us long, every period_us, the first at first_us.
std::function<bool(uint64_t)> clock_pulses(uint64_t period_us, uint64_t width_us, uint64_t first_us);

// Calls setup(). Each test runs in its own process and the fork is the reset,
// so a test boots once: setup() does not reset the sketch's globals.
void boot();

// Runs loop() until the virtual time reaches until_us. A loop() that never
// returns is unwound at that point, and a second run would restart it from the
// top, so drive such a sketch with a single run and schedule the scenario
// with at() beforehand.
void run_until(uint64_t until_us);

// Trigger pulses on an output: runs of writes at half scale or more.
struct Pulse {
  uint64_t start_us;
  uint64_t end_us;  // 0 while the pulse is still high
};
std::vector<Pulse> pulses(int channel);

// The values written to an output while the clock input was high, which is
// when the clocked sketches write their new step.
std::vector<unsigned> values_written_while_clock_high(int channel);

// One character per clock: 'x' where a pulse on the output starts during that
// clock's period, '.' where none does. The clocks come every period_us from first_us.
std::string steps_with_pulses(int channel, uint64_t first_us, uint64_t period_us, int clocks);

// The lowest knob position that Arduino's map(knob, 0, in_max, out_min, out_max)
// turns into `wanted`, which is how the sketches scale their knobs: most with
// in_max 1023, the Euclidean with 1024, which gives each value an equal share
// of the knob's travel.
int knob_for(long wanted, long out_min, long out_max, long in_max = 1023);

}  // namespace fake

#endif
