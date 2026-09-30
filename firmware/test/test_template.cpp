// Host-side tests for the Template, the starting point for a new firmware: it
// boots with both outputs at zero, reads the four knobs into values[] on every
// pass, and offers SendPulse() for a 40 ms trigger.

#include "test_runner.h"
#include "fake_board.h"
#include "fake_arduino.h"

// The prototypes the Arduino builder generates for the sketch.
void updatevalues(void);
void SendPulse(boolean chan);
void setOutput(byte channel, byte gain, byte shutdown, unsigned int val);

#include "../Template/Template.ino"

namespace {

using fake::board;

TEST(boot_sets_both_outputs_to_zero) {
  fake::boot();
  EXPECT_EQ(board.dac[A].size(), 1);
  EXPECT_EQ(board.dac[A].back().value, 0);
  EXPECT_EQ(board.dac[B].size(), 1);
  EXPECT_EQ(board.dac[B].back().value, 0);
}

TEST(loop_reads_the_four_knobs_into_values) {
  board.pot[0] = 100;
  board.pot[1] = 200;
  board.pot[2] = 300;
  board.pot[3] = 400;
  fake::boot();
  fake::run_until(10 * fake::MS);
  EXPECT_EQ(values[0], 100);
  EXPECT_EQ(values[1], 200);
  EXPECT_EQ(values[2], 300);
  EXPECT_EQ(values[3], 400);
  EXPECT_EQ(board.sampled_adc_channels, (std::set<int>{0, 1, 2, 3}));
}

TEST(send_pulse_holds_one_output_at_full_scale_for_40_ms) {
  fake::boot();
  SendPulse(B);
  std::vector<fake::Pulse> pulses = fake::pulses(B);
  EXPECT_EQ(pulses.size(), 1);
  EXPECT_AT_LEAST(pulses[0].end_us - pulses[0].start_us, 40 * fake::MS);
  EXPECT_LESS(pulses[0].end_us - pulses[0].start_us, 41 * fake::MS);
  EXPECT_EQ(board.dac[B].back().value, 0);
  EXPECT_EQ(fake::pulses(A).size(), 0);
}

}  // namespace
