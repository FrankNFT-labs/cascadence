// Host-side tests for the Turing Machine: a shift register that recycles its
// last bit on every clock, flipped with a chance the randomness knob sets. The
// register's value, scaled and offset, is the CV on A, and recycling a 1 sends
// a pulse on B. With the toggle left, the CV snaps to semitones.

#include <algorithm>
#include <set>

#include "test_runner.h"
#include "fake_board.h"
#include "fake_arduino.h"

// The prototypes the Arduino builder generates for the sketch.
void updatevalues(void);
void StartPulse();
void EndPulse();
void setOutput(byte channel, byte gain, byte shutdown, unsigned int val);

#include "../TuringMachine/TuringMachine.ino"

namespace {

using fake::board;

// Knobs from top to bottom.
const int RANDOMNESS = POTS[0];
const int LENGTH = POTS[1];
const int SCALE = POTS[2];
const int OFFSET = POTS[3];

// Fully counter-clockwise flips the recycled bit on every step. Fully
// clockwise never flips it, which locks the loop.
const int ALWAYS_FLIP = 0;
const int NEVER_FLIP = 1023;

const uint64_t FIRST_CLOCK = 50 * fake::MS;
const uint64_t CLOCK_PERIOD = 100 * fake::MS;  // longer than the 40 ms pulse on B
const uint64_t CLOCK_WIDTH = 5 * fake::MS;

void set_up(int randomness, int length, int scale, int offset, bool quantized) {
  board.pot[RANDOMNESS] = randomness;
  board.pot[LENGTH] = fake::knob_for(length, 1, MAXSEQLENGTH);
  board.pot[SCALE] = scale;
  board.pot[OFFSET] = offset;
  board.toggle_left = quantized;
}

// Clocks the sketch `clocks` times. loop() never returns, so this is the
// test's only run: schedule knob and toggle changes before calling it.
void run_clocks(int clocks) {
  board.clock_input = fake::clock_pulses(CLOCK_PERIOD, CLOCK_WIDTH, FIRST_CLOCK);
  fake::run_until(FIRST_CLOCK + (clocks - 1) * CLOCK_PERIOD + CLOCK_PERIOD / 2);
}

long distinct(const std::vector<unsigned> &values) {
  return std::set<unsigned>(values.begin(), values.end()).size();
}

TEST(randomness_fully_counter_clockwise_inverts_the_loop_so_it_repeats_every_twice_the_length) {
  set_up(ALWAYS_FLIP, 4, 1023, 0, false);
  fake::boot();
  run_clocks(24);
  std::vector<unsigned> cv = fake::values_written_while_clock_high(A);
  EXPECT_EQ(cv.size(), 24);
  for (size_t step = 8; step < cv.size(); ++step) EXPECT_EQ(cv[step], cv[step - 8]);
  EXPECT_EQ(distinct(cv), 8);
  EXPECT_EQ(fake::pulses(B).size(), 12);  // the recycled bit is a 1 on half the steps
}

TEST(randomness_fully_clockwise_locks_the_loop_to_its_length) {
  set_up(ALWAYS_FLIP, 4, 1023, 0, false);  // flipping first fills the register with a pattern
  fake::boot();
  fake::at(FIRST_CLOCK + 2 * CLOCK_PERIOD + CLOCK_PERIOD / 2, [] { board.pot[RANDOMNESS] = NEVER_FLIP; });
  run_clocks(16);
  std::vector<unsigned> cv = fake::values_written_while_clock_high(A);
  EXPECT_EQ(cv.size(), 16);
  for (size_t step = 3 + 4; step < cv.size(); ++step) EXPECT_EQ(cv[step], cv[step - 4]);  // locked from step 3
  EXPECT_EQ(distinct(std::vector<unsigned>(cv.begin() + 3, cv.end())), 4);
}

TEST(with_scale_at_zero_the_offset_alone_sets_the_cv) {
  set_up(ALWAYS_FLIP, 8, 0, 1023, false);  // the full offset is half the DAC range
  fake::boot();
  run_clocks(8);
  EXPECT_EQ(fake::values_written_while_clock_high(A), std::vector<unsigned>(8, 2047));
}

TEST(with_scale_and_offset_both_full_the_cv_pins_at_the_top_instead_of_wrapping) {
  set_up(ALWAYS_FLIP, 4, 1023, 1023, false);
  fake::boot();
  run_clocks(16);
  std::vector<unsigned> cv = fake::values_written_while_clock_high(A);
  EXPECT_EQ(cv.size(), 16);
  for (unsigned value : cv) EXPECT_AT_LEAST(value, 2047);  // never below the offset
  EXPECT_EQ(*std::max_element(cv.begin(), cv.end()), fake::FULL_SCALE);
}

TEST(the_cv_changes_at_the_clock_also_on_steps_with_a_pulse) {
  set_up(ALWAYS_FLIP, 4, 1023, 0, false);
  fake::boot();
  run_clocks(8);
  EXPECT_EQ(board.dac[A].size(), 8);  // one CV per step
  for (size_t step = 0; step < board.dac[A].size(); ++step)
    EXPECT_LESS(board.dac[A][step].time_us - (FIRST_CLOCK + step * CLOCK_PERIOD), fake::MS);
  EXPECT_AT_LEAST(fake::pulses(B).size(), 1);
}

TEST(a_clock_faster_than_a_pulse_still_moves_the_register_on_every_clock) {
  set_up(ALWAYS_FLIP, 4, 1023, 0, false);
  fake::boot();
  const uint64_t period = 30 * fake::MS;  // shorter than the 40 ms pulse
  board.clock_input = fake::clock_pulses(period, CLOCK_WIDTH, FIRST_CLOCK);
  fake::run_until(FIRST_CLOCK + 7 * period + period / 2);
  EXPECT_EQ(board.dac[A].size(), 8);
}

TEST(a_pulse_lasts_40_ms_while_the_clock_stays_high_for_longer) {
  set_up(ALWAYS_FLIP, 4, 1023, 0, false);
  fake::boot();
  board.clock_input = fake::clock_pulses(CLOCK_PERIOD, CLOCK_PERIOD / 2, FIRST_CLOCK);  // high for 50 ms
  fake::run_until(FIRST_CLOCK + 7 * CLOCK_PERIOD + CLOCK_PERIOD / 2);
  std::vector<fake::Pulse> pulses = fake::pulses(B);
  EXPECT_EQ(pulses.size(), 4);  // the recycled bit is a 1 on half the steps
  for (const fake::Pulse &pulse : pulses) {
    EXPECT_AT_LEAST(pulse.end_us - pulse.start_us, 40 * fake::MS);
    EXPECT_LESS(pulse.end_us - pulse.start_us, 41 * fake::MS);
  }
}

TEST(a_clock_already_high_at_power_up_plays_its_step_with_the_knob_settings) {
  set_up(ALWAYS_FLIP, 8, 0, 1023, false);  // the full offset alone: half the DAC range
  board.clock_input = fake::clock_pulses(CLOCK_PERIOD, CLOCK_WIDTH, 0);  // the jack is high from power-up
  fake::boot();
  fake::run_until(3 * CLOCK_PERIOD + CLOCK_PERIOD / 2);
  EXPECT_EQ(fake::values_written_while_clock_high(A), std::vector<unsigned>(4, 2047));
}

TEST(with_the_toggle_left_every_cv_step_is_a_whole_number_of_semitones) {
  set_up(ALWAYS_FLIP, 4, 1023, 0, true);
  fake::boot();
  run_clocks(8);
  std::vector<unsigned> cv = fake::values_written_while_clock_high(A);
  EXPECT_EQ(cv.size(), 8);
  for (unsigned value : cv) EXPECT_EQ(value % BITSPERSEMITONE, 0);
  EXPECT_AT_LEAST(distinct(cv), 2);
}

}  // namespace
