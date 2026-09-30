// Host-side tests for the Euclidean Sequencer: each output plays a Euclidean
// rhythm, a number of pulses spread as evenly as possible over a number of
// steps, one step per clock. The toggle chooses which output the knobs edit.

#include <algorithm>
#include <cstdlib>

#include "test_runner.h"
#include "fake_board.h"
#include "fake_arduino.h"

// The prototypes the Arduino builder generates for the sketch.
uint64_t euclid(int n, int k);
void updatevalues(boolean chan);
void setpattern(boolean chan);
void StartPulse(boolean chan);
void EndPulses();
void setOutput(byte channel, byte gain, byte shutdown, unsigned int val);
long int ConcatBin(uint64_t bina, uint64_t binb);
int findlength(long int bnry);

#include "../euclideansequencer/euclideansequencer.ino"

namespace {

using fake::board;

// Knobs from top to bottom.
const int LENGTH = POTS[0];
const int DENSITY = POTS[1];
const int OFFSET = POTS[2];
const int RANDOMNESS = POTS[3];

const uint64_t FIRST_CLOCK = 50 * fake::MS;
const uint64_t CLOCK_PERIOD = 100 * fake::MS;  // longer than a 40 ms pulse
const uint64_t CLOCK_WIDTH = 5 * fake::MS;

// The sketch scales each knob with map(reading, 0, 1024, lowest, highest + 1),
// which gives each value from lowest to highest an equal share of the travel.
void set_rhythm(int pulses, int steps) {
  board.pot[LENGTH] = fake::knob_for(steps, 1, MAXSTEPLENGTH + 2, 1024);  // 1 to 32 steps
  board.pot[DENSITY] = fake::knob_for(pulses, 1, steps + 1, 1024);
  board.pot[OFFSET] = 0;
}

// 31 counts below the end of the travel: inside the top value's share on
// every knob, because no knob has more than 32 values.
const int NEAR_THE_TOP = 1023 - 31;

// Clocks the sketch `clocks` times. loop() never returns, so this is the
// test's only run: schedule knob and toggle changes before calling it.
void run_clocks(int clocks) {
  board.clock_input = fake::clock_pulses(CLOCK_PERIOD, CLOCK_WIDTH, FIRST_CLOCK);
  fake::run_until(FIRST_CLOCK + (clocks - 1) * CLOCK_PERIOD + CLOCK_PERIOD / 2);
}

std::string steps_with_pulses(int channel, int clocks) {
  return fake::steps_with_pulses(channel, FIRST_CLOCK, CLOCK_PERIOD, clocks);
}

std::string inverted(std::string steps) {
  for (char &step : steps) step = step == 'x' ? '.' : 'x';
  return steps;
}

// Whether `steps` holds `pulses` pulses spread as evenly as possible: the gaps
// from one pulse to the next, wrapping around, differ by one step at most. Any
// rotation of a Euclidean rhythm passes, so the tests do not depend on which
// step the sketch starts the pattern on.
bool evenly_spread(const std::string &steps, int pulses) {
  std::vector<int> at;
  for (int step = 0; step < static_cast<int>(steps.size()); ++step)
    if (steps[step] == 'x') at.push_back(step);
  if (static_cast<int>(at.size()) != pulses) return false;
  int length = steps.size(), shortest = length, longest = 0;
  for (size_t i = 0; i < at.size(); ++i) {
    int gap = (at[(i + 1) % at.size()] - at[i] + length) % length;
    if (gap == 0) gap = length;  // a single pulse
    shortest = std::min(shortest, gap);
    longest = std::max(longest, gap);
  }
  return longest - shortest <= 1;
}

void expect_euclidean(const std::string &steps, int pulses, int line) {
  if (evenly_spread(steps, pulses)) return;
  throw fake::TestFailure{"line " + std::to_string(line) + ": \"" + steps + "\" is not " + std::to_string(pulses) +
                          " pulses spread evenly over " + std::to_string(steps.size()) + " steps"};
}

#define EXPECT_EUCLIDEAN(steps, pulses) expect_euclidean((steps), (pulses), __LINE__)

TEST(three_pulses_spread_evenly_over_eight_steps_repeat_every_eight_clocks) {
  set_rhythm(3, 8);
  board.pot[RANDOMNESS] = 0;
  fake::boot();
  run_clocks(16);
  std::string a = steps_with_pulses(A, 16);
  EXPECT_EUCLIDEAN(a.substr(0, 8), 3);
  EXPECT_EQ(a.substr(8), a.substr(0, 8));
  EXPECT_EQ(steps_with_pulses(B, 16), a);  // same knobs, same rhythm
}

TEST(knobs_edit_only_the_output_the_toggle_selects) {
  set_rhythm(8, 8);  // both outputs at power-up: a pulse on every step
  board.pot[RANDOMNESS] = 0;
  board.toggle_left = false;
  fake::boot();
  fake::at(FIRST_CLOCK / 2, [] {  // flip the toggle left, to A, and turn the knobs
    board.toggle_left = true;
    set_rhythm(3, 8);
  });
  run_clocks(8);
  EXPECT_EUCLIDEAN(steps_with_pulses(A, 8), 3);
  EXPECT_EQ(steps_with_pulses(B, 8), "xxxxxxxx");
}

TEST(flipping_the_toggle_changes_neither_output) {
  set_rhythm(3, 8);
  board.pot[RANDOMNESS] = 0;
  fake::boot();  // toggle left: both outputs start at three pulses over eight steps
  fake::at(FIRST_CLOCK / 2, [] { set_rhythm(5, 8); });  // A takes five over eight
  fake::at(FIRST_CLOCK * 3 / 4, [] { board.toggle_left = false; });  // flip to B, knobs untouched
  run_clocks(8);
  EXPECT_EQ(steps_with_pulses(A, 8), "x.xx.xx.");
  EXPECT_EQ(steps_with_pulses(B, 8), "x..x..x.");
}

TEST(a_knob_turned_after_a_flip_changes_only_its_own_setting_of_the_selected_output) {
  set_rhythm(3, 8);
  board.pot[RANDOMNESS] = 0;
  fake::boot();  // toggle left: both outputs start at three pulses over eight steps
  fake::at(FIRST_CLOCK / 4, [] { set_rhythm(5, 16); });  // A takes five over sixteen
  fake::at(FIRST_CLOCK / 2, [] { board.toggle_left = false; });  // flip to B
  fake::at(FIRST_CLOCK * 3 / 4, [] { board.pot[DENSITY] = 0; });  // one pulse, over B's own eight steps
  run_clocks(16);
  EXPECT_EQ(steps_with_pulses(A, 16), "x..x..x..x..x...");
  EXPECT_EQ(steps_with_pulses(B, 16), "x.......x.......");
}

TEST(both_outputs_play_their_rhythm_from_the_first_clock_without_a_toggle_flip) {
  set_rhythm(3, 8);
  board.pot[RANDOMNESS] = 0;
  fake::boot();  // toggle left, so loop() sets up only A: B depends on setup()
  run_clocks(8);
  EXPECT_EUCLIDEAN(steps_with_pulses(A, 8), 3);
  EXPECT_EUCLIDEAN(steps_with_pulses(B, 8), 3);
}

TEST(at_offset_zero_each_cycle_starts_on_a_pulse) {
  set_rhythm(3, 8);
  board.pot[RANDOMNESS] = 0;
  board.toggle_left = false;
  fake::boot();
  fake::at(FIRST_CLOCK / 2, [] { set_rhythm(5, 8); });  // B only: the toggle is right
  run_clocks(16);
  EXPECT_EQ(steps_with_pulses(A, 16), "x..x..x.x..x..x.");  // the tresillo
  EXPECT_EQ(steps_with_pulses(B, 16), "x.xx.xx.x.xx.xx.");  // the cinquillo
}

TEST(a_five_step_rhythm_keeps_its_cycle_past_the_thirty_second_clock) {
  set_rhythm(2, 5);
  board.pot[RANDOMNESS] = 0;
  fake::boot();
  run_clocks(64);
  std::string cycles;
  while (cycles.size() < 64) cycles += "x.x..";
  cycles.resize(64);
  EXPECT_EQ(steps_with_pulses(A, 64), cycles);
  EXPECT_EQ(steps_with_pulses(B, 64), cycles);
}

TEST(the_length_knob_reaches_thirty_two_steps_before_the_end_of_its_travel) {
  board.pot[LENGTH] = NEAR_THE_TOP;
  board.pot[DENSITY] = 0;  // one pulse
  board.pot[OFFSET] = 0;
  board.pot[RANDOMNESS] = 0;
  fake::boot();
  run_clocks(64);
  std::string one_pulse_in_32 = "x" + std::string(31, '.');
  EXPECT_EQ(steps_with_pulses(A, 64), one_pulse_in_32 + one_pulse_in_32);
}

TEST(the_density_knob_reaches_a_pulse_on_every_step_before_the_end_of_its_travel) {
  set_rhythm(1, 8);
  board.pot[DENSITY] = NEAR_THE_TOP;
  board.pot[RANDOMNESS] = 0;
  fake::boot();
  run_clocks(8);
  EXPECT_EQ(steps_with_pulses(A, 8), "xxxxxxxx");
}

TEST(the_offset_knob_fully_clockwise_plays_each_pulse_one_step_later) {
  set_rhythm(3, 8);
  board.pot[OFFSET] = 1023;  // offset 7 of 8: seven steps earlier is one step later
  board.pot[RANDOMNESS] = 0;
  fake::boot();
  run_clocks(8);
  EXPECT_EQ(steps_with_pulses(A, 8), ".x..x..x");
}

TEST(the_randomness_knob_reaches_every_step_inverted_before_the_end_of_its_travel) {
  board.random_below = [](long howbig) { return howbig - 1; };  // the draw least likely to invert a step
  set_rhythm(3, 8);
  board.pot[RANDOMNESS] = NEAR_THE_TOP;
  fake::boot();
  run_clocks(8);
  EXPECT_EQ(steps_with_pulses(A, 8), inverted("x..x..x."));
}

TEST(a_knob_jittering_by_one_count_across_a_step_boundary_keeps_its_value) {
  board.pot[LENGTH] = 127;  // the highest reading for 4 steps; 128 gives 5
  board.pot[DENSITY] = 0;   // one pulse
  board.pot[OFFSET] = 0;
  board.pot[RANDOMNESS] = 0;
  fake::boot();
  for (int clock = 0; clock < 16; clock += 2) {  // one count up after every other clock, back down after the next
    fake::at(FIRST_CLOCK + clock * CLOCK_PERIOD + CLOCK_PERIOD / 2, [] { board.pot[LENGTH] = 128; });
    fake::at(FIRST_CLOCK + (clock + 1) * CLOCK_PERIOD + CLOCK_PERIOD / 2, [] { board.pot[LENGTH] = 127; });
  }
  run_clocks(16);
  EXPECT_EQ(steps_with_pulses(A, 16), "x...x...x...x...");
}

TEST(with_every_knob_fully_counter_clockwise_at_power_up_every_step_pulses) {
  for (int &knob : board.pot) knob = 0;  // one step with one pulse, well inside the dead band of a reading of 0
  fake::boot();
  run_clocks(8);
  EXPECT_EQ(steps_with_pulses(A, 8), "xxxxxxxx");
  EXPECT_EQ(steps_with_pulses(B, 8), "xxxxxxxx");
}

TEST(a_knob_turned_just_past_the_dead_band_takes_effect) {
  board.pot[LENGTH] = 127;  // 4 steps
  board.pot[DENSITY] = 0;
  board.pot[OFFSET] = 0;
  board.pot[RANDOMNESS] = 0;
  fake::boot();
  fake::at(FIRST_CLOCK / 2, [] { board.pot[LENGTH] = 127 + 6; });  // 5 steps, 6 counts on: just past the 5-count dead band
  run_clocks(10);
  EXPECT_EQ(steps_with_pulses(A, 10), "x....x....");
}

TEST(both_outputs_fire_together_on_a_shared_step) {
  set_rhythm(8, 8);
  board.pot[RANDOMNESS] = 0;
  fake::boot();
  run_clocks(4);
  std::vector<fake::Pulse> a = fake::pulses(A), b = fake::pulses(B);
  EXPECT_EQ(a.size(), 4);
  EXPECT_EQ(b.size(), 4);
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
    EXPECT_LESS(std::labs(static_cast<long>(b[i].start_us) - static_cast<long>(a[i].start_us)), fake::MS);
    EXPECT_AT_LEAST(b[i].end_us - b[i].start_us, 40 * fake::MS);
    EXPECT_LESS(b[i].end_us - b[i].start_us, 41 * fake::MS);
  }
}

TEST(a_clock_too_fast_for_two_pulses_back_to_back_still_plays_every_step) {
  set_rhythm(8, 8);
  board.pot[RANDOMNESS] = 0;
  fake::boot();
  const uint64_t period = 60 * fake::MS;  // one 40 ms pulse fits, two in a row do not
  board.clock_input = fake::clock_pulses(period, CLOCK_WIDTH, FIRST_CLOCK);
  fake::run_until(FIRST_CLOCK + 7 * period + period / 2);
  EXPECT_EQ(fake::steps_with_pulses(A, FIRST_CLOCK, period, 8), "xxxxxxxx");
  EXPECT_EQ(fake::steps_with_pulses(B, FIRST_CLOCK, period, 8), "xxxxxxxx");
}

TEST(a_pulse_lasts_40_ms_while_the_clock_stays_high_for_longer) {
  set_rhythm(8, 8);
  board.pot[RANDOMNESS] = 0;
  fake::boot();
  board.clock_input = fake::clock_pulses(CLOCK_PERIOD, CLOCK_PERIOD / 2, FIRST_CLOCK);  // high for 50 ms
  fake::run_until(FIRST_CLOCK + 3 * CLOCK_PERIOD + CLOCK_PERIOD / 2);
  std::vector<fake::Pulse> a = fake::pulses(A);
  EXPECT_EQ(a.size(), 4);
  for (const fake::Pulse &pulse : a) {
    EXPECT_AT_LEAST(pulse.end_us - pulse.start_us, 40 * fake::MS);
    EXPECT_LESS(pulse.end_us - pulse.start_us, 41 * fake::MS);
  }
}

TEST(randomness_fully_counter_clockwise_inverts_no_step) {
  board.random_below = [](long) { return 0L; };  // the draw most likely to invert a step
  set_rhythm(3, 8);
  board.pot[RANDOMNESS] = 0;
  fake::boot();
  run_clocks(8);
  EXPECT_EUCLIDEAN(steps_with_pulses(A, 8), 3);
  EXPECT_EUCLIDEAN(steps_with_pulses(B, 8), 3);
}

TEST(randomness_fully_clockwise_inverts_every_step) {
  set_rhythm(3, 8);
  board.pot[RANDOMNESS] = 1023;  // every draw from random(31) is below the setting
  fake::boot();
  run_clocks(8);
  std::string a = steps_with_pulses(A, 8);
  EXPECT_EUCLIDEAN(inverted(a), 3);  // the three-over-eight rhythm with every step inverted
  EXPECT_EQ(steps_with_pulses(B, 8), a);
}

}  // namespace
