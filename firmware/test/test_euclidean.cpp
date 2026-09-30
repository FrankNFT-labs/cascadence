// Host-side tests for the Euclidean Sequencer: each output plays a Euclidean
// rhythm, a number of pulses spread as evenly as possible over a number of
// steps, one step per clock. The toggle chooses which output the knobs edit.

#include <algorithm>

#include "test_runner.h"
#include "fake_board.h"
#include "fake_arduino.h"

// The prototypes the Arduino builder generates for the sketch.
uint64_t euclid(int n, int k);
void updatevalues(boolean chan);
void SendPulse(boolean chan);
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
const uint64_t CLOCK_PERIOD = 100 * fake::MS;  // room for two blocking 40 ms pulses, A then B
const uint64_t CLOCK_WIDTH = 5 * fake::MS;

void set_rhythm(int pulses, int steps) {
  board.pot[LENGTH] = fake::knob_for(steps, 1, MAXSTEPLENGTH + 1);
  board.pot[DENSITY] = fake::knob_for(pulses, 1, steps);
  board.pot[OFFSET] = 0;
}

// A channel copies the knobs only while the toggle selects it, and setup()
// sets up neither channel (a phase 2 bug), so every test boots with the toggle
// right, which sets up B, and flips it left, which sets up A, before the first
// clock.
void boot_and_set_up_both_channels() {
  board.toggle_left = false;
  fake::boot();
  fake::at(FIRST_CLOCK / 2, [] { board.toggle_left = true; });
}

// With the randomness knob fully counter-clockwise, a draw of 0 from
// random(31) still inverts a step (a phase 2 bug). Drawing the highest value
// every time keeps the rhythm exact.
long highest_draw(long howbig) { return howbig - 1; }

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
  board.random_below = highest_draw;
  set_rhythm(3, 8);
  board.pot[RANDOMNESS] = 0;
  boot_and_set_up_both_channels();
  run_clocks(16);
  std::string a = steps_with_pulses(A, 16);
  EXPECT_EUCLIDEAN(a.substr(0, 8), 3);
  EXPECT_EQ(a.substr(8), a.substr(0, 8));
  EXPECT_EQ(steps_with_pulses(B, 16), a);  // same knobs, same rhythm
}

TEST(knobs_edit_only_the_output_the_toggle_selects) {
  board.random_below = highest_draw;
  set_rhythm(8, 8);  // B, set up first, pulses on every step
  board.pot[RANDOMNESS] = 0;
  boot_and_set_up_both_channels();
  fake::at(FIRST_CLOCK / 2, [] { set_rhythm(3, 8); });  // A, selected from here on
  run_clocks(8);
  EXPECT_EUCLIDEAN(steps_with_pulses(A, 8), 3);
  EXPECT_EQ(steps_with_pulses(B, 8), "xxxxxxxx");
}

TEST(both_outputs_play_their_rhythm_from_the_first_clock_without_a_toggle_flip) {
  board.random_below = highest_draw;
  set_rhythm(3, 8);
  board.pot[RANDOMNESS] = 0;
  fake::boot();  // toggle left, so loop() sets up only A: B depends on setup()
  run_clocks(8);
  EXPECT_EUCLIDEAN(steps_with_pulses(A, 8), 3);
  EXPECT_EUCLIDEAN(steps_with_pulses(B, 8), 3);
}

TEST(randomness_fully_clockwise_inverts_every_step) {
  set_rhythm(3, 8);
  board.pot[RANDOMNESS] = 1023;  // every draw from random(31) is at most the setting
  boot_and_set_up_both_channels();
  run_clocks(8);
  std::string a = steps_with_pulses(A, 8);
  EXPECT_EUCLIDEAN(inverted(a), 3);  // the three-over-eight rhythm with every step inverted
  EXPECT_EQ(steps_with_pulses(B, 8), a);
}

}  // namespace
