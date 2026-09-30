// Host-side tests for the Locking Sequencer: two four-step sequences, A and B,
// that step on every clock, while the toggle chooses which one the four knobs
// edit (left edits A).

#include <algorithm>

#include "test_runner.h"
#include "fake_board.h"
#include "fake_arduino.h"

// The prototypes the Arduino builder generates for the sketch.
void setOutput(byte channel, byte gain, byte shutdown, unsigned int val);

#include "../LockingSequencer/LockingSequencer.ino"

namespace {

using fake::board;

const uint64_t FIRST_CLOCK = 50 * fake::MS;
const uint64_t CLOCK_PERIOD = 100 * fake::MS;
const uint64_t CLOCK_WIDTH = 5 * fake::MS;

// A step plays its knob's 10-bit reading shifted up to the DAC's 12 bits.
unsigned dac_value_for(int knob) { return knob << 2; }

void set_every_knob(int reading) {
  for (int pot = 0; pot < fake::POT_COUNT; ++pot) board.pot[pot] = reading;
}

// Clocks the sequencer `clocks` times. loop() never returns, so this is the
// test's only run: schedule knob and toggle changes before calling it.
void run_clocks(int clocks) {
  board.clock_input = fake::clock_pulses(CLOCK_PERIOD, CLOCK_WIDTH, FIRST_CLOCK);
  fake::run_until(FIRST_CLOCK + (clocks - 1) * CLOCK_PERIOD + CLOCK_PERIOD / 2);
}

std::vector<unsigned> sorted(std::vector<unsigned> values) {
  std::sort(values.begin(), values.end());
  return values;
}

TEST(four_clocks_play_each_stored_step_once_on_both_outputs) {
  board.pot[0] = 100;
  board.pot[1] = 200;
  board.pot[2] = 300;
  board.pot[3] = 400;
  fake::boot();  // setup() stores the four knobs as the steps of both sequences
  run_clocks(4);
  std::vector<unsigned> steps{dac_value_for(100), dac_value_for(200), dac_value_for(300), dac_value_for(400)};
  EXPECT_EQ(sorted(fake::values_written_while_clock_high(A)), steps);
  EXPECT_EQ(sorted(fake::values_written_while_clock_high(B)), steps);
}

// Turns every knob away from its boot position before the first clock, with the
// toggle on the side of `edited`, and checks that only that sequence changed.
void check_toggle_side_edits_only(int edited) {
  set_every_knob(100);
  board.toggle_left = edited == A;
  fake::boot();
  fake::at(FIRST_CLOCK / 2, [] { set_every_knob(900); });
  run_clocks(4);
  int kept = edited == A ? B : A;
  EXPECT_EQ(fake::values_written_while_clock_high(edited), std::vector<unsigned>(4, dac_value_for(900)));
  EXPECT_EQ(fake::values_written_while_clock_high(kept), std::vector<unsigned>(4, dac_value_for(100)));
}

TEST(knobs_edit_sequence_a_while_the_toggle_is_left) { check_toggle_side_edits_only(A); }

TEST(knobs_edit_sequence_b_while_the_toggle_is_right) { check_toggle_side_edits_only(B); }

}  // namespace
