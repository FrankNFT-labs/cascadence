// Host-side tests for the Cascadence ADSR firmware.
//
// The sketch runs on the shared fake board from fake_board.h: four pots, the
// A/B toggle, the gate input and the MCP48x2 DAC. Each test turns knobs, flips
// the toggle and holds the gate, then checks what the firmware wrote to DAC
// outputs A and B. Build and run with `make` in this folder.

#include "test_runner.h"
#include "fake_board.h"
#include "fake_arduino.h"

#include "../ADSR/ADSR.ino"

namespace {

using fake::TestFailure;
fake::Board &board = fake::board;

// Pots from top to bottom, on the ADC channels the sketch declares for them.
const int ATTACK_POT = POTS[0];
const int DECAY_POT = POTS[1];
const int SUSTAIN_POT = POTS[2];
const int RELEASE_POT = POTS[3];

// Every test boots with all four knobs at the midpoint.
const int KNOB_MIDPOINT = 512;

// The sketch switches from attack to decay once the envelope passes this.
const unsigned ATTACK_PEAK = 4000;

// Held gate steps for attack and decay to settle at the sustain level, and
// passes after a gate for the release to reach zero, both with generous margin.
const long GATE_READS_TO_SETTLE = 400;
const int PASSES_TO_RELEASE_FULLY = 2000;

// loop() reads the gate low at most twice per pass: once to leave the gate
// loop and once after it. A third low read in one pass means the gate loop
// kept running without a gate, which is how the old loop-mode bug hung the
// firmware.
const int MAX_LOW_GATE_READS_PER_PASS = 2;

// Passes that let the firmware's pot scan visit every pot at least once.
const int PASSES_TO_NOTICE_A_CONTROL = 8;

// The sustain level the sketch derives from a sustain knob reading.
int sustain_level_for(int knob) { return knob * 4; }

// The gate counts reads instead of time: the firmware takes one envelope step
// per gate read while the gate is held, so a count of high reads is a count of
// steps.
long gate_high_reads_left = 0;
int low_gate_reads_this_pass = 0;

bool gate_is_high(uint64_t) {
  bool high = gate_high_reads_left > 0;
  if (high)
    --gate_high_reads_left;
  else if (++low_gate_reads_this_pass > MAX_LOW_GATE_READS_PER_PASS)
    throw TestFailure{"loop() keeps running without a gate: it read the gate low more than " +
                      std::to_string(MAX_LOW_GATE_READS_PER_PASS) + " times in one pass"};
  return high;
}

// ---------------------------------------------------------------------------
// Driving the board and reading the outputs
// ---------------------------------------------------------------------------

void run_passes(int count) {
  for (int pass = 0; pass < count; ++pass) {
    low_gate_reads_this_pass = 0;
    loop();
  }
}

// Boots the sketch with every knob at KNOB_MIDPOINT, once per test.
void boot() {
  board.clock_input = gate_is_high;
  fake::boot();
  board.sampled_adc_channels.clear();  // setup() reads every pot; the scan test asks what loop() samples
}

void turn_knob(int pot, int position) {
  board.pot[pot] = position;
  run_passes(PASSES_TO_NOTICE_A_CONTROL);
}

void select_envelope(int envelope) {
  board.toggle_left = envelope == A;  // the left side of the toggle selects envelope A
  run_passes(PASSES_TO_NOTICE_A_CONTROL);
}

// Holds the gate high for `reads` reads of the gate input, then lets it fall.
// While the gate is held the firmware takes one envelope step per gate read,
// so this is also the number of steps taken with the gate high.
void hold_gate_for(long reads) {
  gate_high_reads_left = reads;
  run_passes(1);
}

// Plays one note and lets the release finish.
void play_and_fully_release() {
  hold_gate_for(GATE_READS_TO_SETTLE);
  run_passes(PASSES_TO_RELEASE_FULLY);
}

// Drops everything written to the DAC so far, so later checks see only what follows.
void forget_dac_history() {
  board.dac[A].clear();
  board.dac[B].clear();
}

unsigned peak_output(int channel) {
  unsigned peak = 0;
  for (const fake::DacWrite &write : board.dac[channel]) peak = std::max(peak, write.value);
  return peak;
}

unsigned output_at_end_of_gate(int channel) {
  for (auto write = board.dac[channel].rbegin(); write != board.dac[channel].rend(); ++write)
    if (write->input_high) return write->value;
  throw TestFailure{"nothing was written to the DAC while the gate was held"};
}

unsigned current_output(int channel) {
  if (board.dac[channel].empty()) throw TestFailure{"nothing was written to the DAC"};
  return board.dac[channel].back().value;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(held_gate_climbs_to_the_attack_peak_then_settles_at_the_sustain_level) {
  boot();
  hold_gate_for(GATE_READS_TO_SETTLE);
  EXPECT_AT_LEAST(peak_output(A), ATTACK_PEAK);
  EXPECT_AT_LEAST(peak_output(B), ATTACK_PEAK);
  EXPECT_EQ(output_at_end_of_gate(A), sustain_level_for(KNOB_MIDPOINT));
  EXPECT_EQ(output_at_end_of_gate(B), sustain_level_for(KNOB_MIDPOINT));
}

TEST(pot_scan_samples_only_the_four_pot_channels) {
  boot();
  run_passes(20);
  EXPECT_EQ(board.sampled_adc_channels, (std::set<int>{ATTACK_POT, DECAY_POT, SUSTAIN_POT, RELEASE_POT}));
}

TEST(released_gate_returns_both_outputs_to_zero) {
  boot();
  play_and_fully_release();
  EXPECT_EQ(current_output(A), 0);
  EXPECT_EQ(current_output(B), 0);
}

// Parks the sustain knob at `position` while editing envelope A, then flips the
// toggle to B without touching any knob. B was never edited, so it must keep
// the sustain level it booted with.
void check_toggle_flip_leaves_other_envelope_alone(int position) {
  boot();
  select_envelope(A);
  turn_knob(SUSTAIN_POT, position);
  select_envelope(B);
  hold_gate_for(GATE_READS_TO_SETTLE);
  EXPECT_EQ(output_at_end_of_gate(A), sustain_level_for(position));
  EXPECT_EQ(output_at_end_of_gate(B), sustain_level_for(KNOB_MIDPOINT));
}

TEST(toggle_flip_with_a_knob_at_zero_leaves_the_other_envelope_alone) {
  check_toggle_flip_leaves_other_envelope_alone(0);
}

// The highest reading where subtracting the sketch's dead band goes below zero.
TEST(toggle_flip_with_a_knob_just_below_the_threshold_leaves_the_other_envelope_alone) {
  check_toggle_flip_leaves_other_envelope_alone(THRESHOLD - 1);
}

TEST(knob_wiggle_inside_the_threshold_near_zero_is_ignored) {
  boot();
  select_envelope(A);
  turn_knob(SUSTAIN_POT, 0);
  turn_knob(SUSTAIN_POT, THRESHOLD - 2);  // inside the dead band that rejects ADC noise
  hold_gate_for(GATE_READS_TO_SETTLE);
  EXPECT_EQ(output_at_end_of_gate(A), 0);
}

TEST(knob_move_beyond_the_threshold_near_zero_edits_only_the_selected_envelope) {
  boot();
  select_envelope(B);
  turn_knob(SUSTAIN_POT, 0);
  turn_knob(SUSTAIN_POT, 2 * THRESHOLD);
  hold_gate_for(GATE_READS_TO_SETTLE);
  EXPECT_EQ(output_at_end_of_gate(B), sustain_level_for(2 * THRESHOLD));
  EXPECT_EQ(output_at_end_of_gate(A), sustain_level_for(KNOB_MIDPOINT));
}

// The two envelopes keep separate attack, decay and release settings. Each test
// below speeds up one stage on envelope A only and expects A to run ahead of B.

TEST(attack_knob_edits_only_the_selected_envelope) {
  boot();
  select_envelope(A);
  turn_knob(ATTACK_POT, 0);  // fastest attack on A, B keeps the midpoint attack
  hold_gate_for(5);          // a few steps into the attack
  EXPECT_LESS(output_at_end_of_gate(B), output_at_end_of_gate(A));
}

TEST(decay_knob_edits_only_the_selected_envelope) {
  boot();
  select_envelope(A);
  turn_knob(DECAY_POT, 0);  // fastest decay on A, B keeps the midpoint decay
  hold_gate_for(60);        // past the attack peak, while B is still decaying towards sustain
  EXPECT_LESS(output_at_end_of_gate(A), output_at_end_of_gate(B));
}

TEST(release_knob_edits_only_the_selected_envelope) {
  boot();
  select_envelope(A);
  turn_knob(RELEASE_POT, 0);  // fastest release on A, B keeps the midpoint release
  hold_gate_for(GATE_READS_TO_SETTLE);
  run_passes(20);  // A has released to zero, B is still on its way down
  EXPECT_LESS(current_output(A), current_output(B));
}

TEST(outputs_stay_at_zero_until_a_gate_arrives) {
  boot();
  run_passes(200);
  EXPECT_EQ(peak_output(A), 0);
  EXPECT_EQ(peak_output(B), 0);
}

TEST(second_gate_after_a_full_release_plays_a_new_envelope) {
  boot();
  play_and_fully_release();
  forget_dac_history();
  hold_gate_for(GATE_READS_TO_SETTLE);
  EXPECT_AT_LEAST(peak_output(A), ATTACK_PEAK);
  EXPECT_AT_LEAST(peak_output(B), ATTACK_PEAK);
  EXPECT_EQ(output_at_end_of_gate(A), sustain_level_for(KNOB_MIDPOINT));
  EXPECT_EQ(output_at_end_of_gate(B), sustain_level_for(KNOB_MIDPOINT));
}

}  // namespace
