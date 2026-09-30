// Host-side tests for PolyCrossClock: output A is a clock at the tempo the top
// knob sets, and output B divides each beat, playing every Cross-th division.
// A pulse on the clock input restarts both.

#include "test_runner.h"
#include "fake_board.h"
#include "fake_arduino.h"

// The prototypes the Arduino builder generates for the sketch.
void updatevalues(void);
void setOutput(byte channel, byte gain, byte shutdown, unsigned int val);

#include "../PolyCrossClock/PolyCrossClock.ino"

namespace {

using fake::board;

// Knobs from top to bottom.
const int TEMPO = POTS[0];
const int DIVISIONS = POTS[1];
const int CROSS = POTS[2];
const int RANDOMNESS = POTS[3];

const int SLOWEST = 0;           // 30 BPM, one beat every 2 s
const int FASTEST = 1023;        // 600 BPM, one beat every 100 ms
const int FOUR_DIVISIONS = 240;  // reads as 4.5 divisions, which the toggle's left side rounds down to 4

const uint64_t PULSE = 40 * fake::MS;
const uint64_t TOLERANCE = 2 * fake::MS;  // a few passes of loop()

// B skips a division pulse when random(1024) is at most the randomness
// setting. Drawing the highest value every time means no skips.
long highest_draw(long howbig) { return howbig - 1; }

void boot_with(int tempo, int divisions, int cross) {
  board.random_below = highest_draw;
  board.pot[TEMPO] = tempo;
  board.pot[DIVISIONS] = divisions;
  board.pot[CROSS] = fake::knob_for(cross, 1, 8);
  board.pot[RANDOMNESS] = 0;
  board.toggle_left = true;  // whole divisions
  fake::boot();
}

// Checks for `count` pulses that start `period_us` apart and last the 40 ms trigger.
void expect_steady_pulses(int channel, uint64_t period_us, size_t count) {
  std::vector<fake::Pulse> pulses = fake::pulses(channel);
  EXPECT_EQ(pulses.size(), count);
  for (size_t i = 1; i < pulses.size(); ++i) {
    EXPECT_AT_LEAST(pulses[i].start_us - pulses[i - 1].start_us, period_us - TOLERANCE);
    EXPECT_LESS(pulses[i].start_us - pulses[i - 1].start_us, period_us + TOLERANCE);
  }
  for (const fake::Pulse &pulse : pulses) {
    if (pulse.end_us == 0) continue;  // still high when the run ended
    EXPECT_AT_LEAST(pulse.end_us - pulse.start_us, PULSE);
    EXPECT_LESS(pulse.end_us - pulse.start_us, PULSE + TOLERANCE);
  }
}

long pulses_starting_between(int channel, uint64_t from_us, uint64_t to_us) {
  long count = 0;
  for (const fake::Pulse &pulse : fake::pulses(channel))
    if (pulse.start_us >= from_us && pulse.start_us < to_us) ++count;
  return count;
}

TEST(tempo_knob_fully_counter_clockwise_clocks_a_at_30_bpm) {
  boot_with(SLOWEST, FOUR_DIVISIONS, 1);
  fake::run_until(6500 * fake::MS);
  expect_steady_pulses(A, 2000 * fake::MS, 4);
}

TEST(tempo_knob_fully_clockwise_clocks_a_at_600_bpm) {
  boot_with(FASTEST, FOUR_DIVISIONS, 1);
  fake::run_until(1050 * fake::MS);
  expect_steady_pulses(A, 100 * fake::MS, 11);
}

TEST(with_the_toggle_left_b_plays_whole_divisions_of_the_beat) {
  boot_with(SLOWEST, FOUR_DIVISIONS, 1);
  fake::run_until(4100 * fake::MS);
  expect_steady_pulses(B, 500 * fake::MS, 9);
}

TEST(cross_three_over_four_divisions_pulses_b_every_three_quarters_of_a_beat) {
  boot_with(SLOWEST, FOUR_DIVISIONS, 3);
  fake::run_until(6100 * fake::MS);
  expect_steady_pulses(B, 1500 * fake::MS, 5);
}

TEST(a_pulse_on_the_clock_input_restarts_both_outputs) {
  boot_with(SLOWEST, FOUR_DIVISIONS, 1);
  board.clock_input = [](uint64_t now_us) { return now_us >= 700 * fake::MS && now_us < 705 * fake::MS; };
  fake::run_until(1000 * fake::MS);
  EXPECT_EQ(fake::pulses(A).size(), 2);  // the first beat at boot, then the restart
  EXPECT_EQ(pulses_starting_between(A, 700 * fake::MS, 701 * fake::MS), 1);
  EXPECT_EQ(pulses_starting_between(B, 700 * fake::MS, 701 * fake::MS), 1);
}

}  // namespace
