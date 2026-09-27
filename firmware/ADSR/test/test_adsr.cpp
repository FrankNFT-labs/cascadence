// Host-side tests for the Cascadence ADSR firmware.
//
// The sketch is compiled as ordinary C++ against a fake board: four pots, the
// A/B toggle, the gate input and the MCP48x2 DAC. Each test turns knobs, flips
// the toggle and holds the gate, then checks what the firmware wrote to DAC
// outputs A and B. Build and run with `make` in this folder.

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "fake_arduino.h"

// The Arduino build generates a prototype for every function in a sketch.
// Plain C++ needs this one spelled out, because loop() calls it before its definition.
void checkforchange(int scan);

#include "../ADSR.ino"

// ---------------------------------------------------------------------------
// Fake board
// ---------------------------------------------------------------------------

namespace {

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

struct TestFailure {
  std::string message;
};

struct DacWrite {
  unsigned value;  // 12-bit data field of the frame (the MCP4812 uses its top 10 bits)
  bool gate_held;  // whether the firmware's most recent gate read saw the gate high
};

struct Board {
  int pot[4] = {KNOB_MIDPOINT, KNOB_MIDPOINT, KNOB_MIDPOINT, KNOB_MIDPOINT};  // ADC readings, 0..1023
  bool toggle_on_b = false;
  long gate_high_reads_left = 0;
  bool gate_seen_high = false;
  int low_gate_reads_this_pass = 0;
  std::set<int> sampled_adc_channels;
  std::vector<DacWrite> dac[2];
  bool dac_selected = false;
  std::vector<uint8_t> frame;
};

Board board;

}  // namespace

void pinMode(uint8_t, uint8_t) {}

void digitalWrite(uint8_t pin, uint8_t value) {
  if (pin != PIN_CS) return;
  if (value == LOW) {  // chip select falls: a new 16-bit frame starts
    board.dac_selected = true;
    board.frame.clear();
    return;
  }
  if (board.dac_selected) {  // chip select rises: the DAC latches the frame
    if (board.frame.size() != 2)
      throw TestFailure{"DAC frame had " + std::to_string(board.frame.size()) + " bytes, expected 2"};
    unsigned word = (board.frame[0] << 8) | board.frame[1];
    if (((word >> 12) & 1) != NO_SHTDWN)
      throw TestFailure{"DAC frame has the shutdown bit clear: the output would be muted"};
    if (((word >> 13) & 1) != GAIN_2)
      throw TestFailure{"DAC frame selects 1x gain: the module drives its outputs at 2x"};
    unsigned channel = (word >> 15) & 1;
    board.dac[channel].push_back(DacWrite{word & 0x0FFF, board.gate_seen_high});
  }
  board.dac_selected = false;
}

void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t value) {
  if (dataPin != MOSI || clockPin != SCK)
    throw TestFailure{"shiftOut on the wrong pins: the DAC data line is MOSI and its clock is SCK"};
  if (bitOrder != MSBFIRST) throw TestFailure{"shiftOut LSB first: the MCP48x2 expects MSB first"};
  if (board.dac_selected) board.frame.push_back(value);
}

int digitalRead(uint8_t pin) {
  if (pin == gatePin) {
    board.gate_seen_high = board.gate_high_reads_left > 0;
    if (board.gate_seen_high)
      --board.gate_high_reads_left;
    else if (++board.low_gate_reads_this_pass > MAX_LOW_GATE_READS_PER_PASS)
      throw TestFailure{"loop() keeps running without a gate: it read the gate low more than " +
                        std::to_string(MAX_LOW_GATE_READS_PER_PASS) + " times in one pass"};
    return board.gate_seen_high ? LOW : HIGH;  // the input transistor inverts the jack
  }
  if (pin == SW) return board.toggle_on_b ? LOW : HIGH;  // HIGH selects envelope A
  return LOW;
}

int analogRead(uint8_t channel) {
  board.sampled_adc_channels.insert(channel);
  return channel < 4 ? board.pot[channel] : 0;  // ADC4 is the DAC clock line, which idles low
}

namespace {

// ---------------------------------------------------------------------------
// Driving the board and reading the outputs
// ---------------------------------------------------------------------------

void run_passes(int count) {
  for (int pass = 0; pass < count; ++pass) {
    board.low_gate_reads_this_pass = 0;
    loop();
  }
}

// Boots the sketch with every knob at KNOB_MIDPOINT. Each test runs in its own
// process and the fork is the reset, so a test boots exactly once: setup() does
// not reset the sketch's globals, and a second boot would run on stale state.
void boot() {
  static bool booted = false;
  if (booted) throw TestFailure{"boot() called twice: the fork is the reset, boot once per test"};
  booted = true;
  setup();
  board.sampled_adc_channels.clear();  // setup() reads every pot; the scan test asks what loop() samples
}

void turn_knob(int pot, int position) {
  board.pot[pot] = position;
  run_passes(PASSES_TO_NOTICE_A_CONTROL);
}

void select_envelope(int envelope) {
  board.toggle_on_b = envelope == B;
  run_passes(PASSES_TO_NOTICE_A_CONTROL);
}

// Holds the gate high for `reads` reads of the gate input, then lets it fall.
// While the gate is held the firmware takes one envelope step per gate read,
// so this is also the number of steps taken with the gate high.
void hold_gate_for(long reads) {
  board.gate_high_reads_left = reads;
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
  for (const DacWrite &write : board.dac[channel]) peak = std::max(peak, write.value);
  return peak;
}

unsigned output_at_end_of_gate(int channel) {
  for (auto write = board.dac[channel].rbegin(); write != board.dac[channel].rend(); ++write)
    if (write->gate_held) return write->value;
  throw TestFailure{"nothing was written to the DAC while the gate was held"};
}

unsigned current_output(int channel) {
  if (board.dac[channel].empty()) throw TestFailure{"nothing was written to the DAC"};
  return board.dac[channel].back().value;
}

// ---------------------------------------------------------------------------
// Checks and test registry
// ---------------------------------------------------------------------------

void expect_equal(long actual, long expected, const char *expression, int line) {
  if (actual == expected) return;
  std::ostringstream message;
  message << "line " << line << ": " << expression << " is " << actual << ", expected " << expected;
  throw TestFailure{message.str()};
}

void expect_at_least(long actual, long minimum, const char *expression, int line) {
  if (actual >= minimum) return;
  std::ostringstream message;
  message << "line " << line << ": " << expression << " is " << actual << ", expected at least " << minimum;
  throw TestFailure{message.str()};
}

void expect_less(long actual, long bound, const char *expression, int line) {
  if (actual < bound) return;
  std::ostringstream message;
  message << "line " << line << ": " << expression << " is " << actual << ", expected less than " << bound;
  throw TestFailure{message.str()};
}

std::string describe(const std::set<int> &values) {
  std::ostringstream text;
  text << "{";
  for (int value : values) text << (value == *values.begin() ? "" : ", ") << value;
  text << "}";
  return text.str();
}

void expect_equal(const std::set<int> &actual, const std::set<int> &expected, const char *expression, int line) {
  if (actual == expected) return;
  throw TestFailure{"line " + std::to_string(line) + ": " + expression + " is " + describe(actual) + ", expected " +
                    describe(expected)};
}

#define EXPECT_EQ(actual, expected) expect_equal((actual), (expected), #actual, __LINE__)
#define EXPECT_AT_LEAST(actual, minimum) expect_at_least((actual), (minimum), #actual, __LINE__)
#define EXPECT_LESS(actual, bound) expect_less((actual), (bound), #actual, __LINE__)

struct TestCase {
  const char *name;
  void (*body)();
};

std::vector<TestCase> &registered_tests() {
  static std::vector<TestCase> tests;
  return tests;
}

struct RegisterTest {
  RegisterTest(const char *name, void (*body)()) { registered_tests().push_back(TestCase{name, body}); }
};

#define TEST(name)                           \
  void name();                               \
  RegisterTest register_##name(#name, name); \
  void name()

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

// Runs every test in its own process, so each one starts from a freshly booted
// sketch, and a crash or sanitizer abort fails only that test.
int main() {
  int failed = 0;
  for (const TestCase &test : registered_tests()) {
    fflush(stdout);
    pid_t child = fork();
    if (child < 0) {
      perror("fork");
      return 2;
    }
    if (child == 0) {
      alarm(30);
      try {
        test.body();
      } catch (const TestFailure &failure) {
        printf("FAIL  %s\n      %s\n", test.name, failure.message.c_str());
        fflush(stdout);
        _exit(3);
      }
      printf("PASS  %s\n", test.name);
      fflush(stdout);
      _exit(0);
    }
    int status = 0;
    waitpid(child, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) continue;
    ++failed;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 3) continue;  // already reported by the child
    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM)
      printf("FAIL  %s\n      timed out\n", test.name);
    else if (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)
      printf("FAIL  %s\n      aborted (sanitizer or assert), see the message on stderr above\n", test.name);
    else if (WIFSIGNALED(status))
      printf("FAIL  %s\n      crashed: %s\n", test.name, strsignal(WTERMSIG(status)));
    else
      printf("FAIL  %s\n      exited with status %d, see the message on stderr above\n", test.name,
             WEXITSTATUS(status));
  }
  printf("\n%d of %zu tests failed\n", failed, registered_tests().size());
  return failed == 0 ? 0 : 1;
}
