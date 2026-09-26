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

// Pots from top to bottom, one ADC channel each.
const int ATTACK_POT = 0;
const int DECAY_POT = 1;
const int SUSTAIN_POT = 2;
const int RELEASE_POT = 3;

// A single pass of loop() that reads the gate more often than this is stuck.
const long MAX_GATE_READS_PER_PASS = 100000;

struct TestFailure {
  std::string message;
};

struct DacWrite {
  unsigned value;  // 12-bit data field of the frame (the MCP4812 uses its top 10 bits)
  bool gate_held;  // whether the firmware's most recent gate read saw the gate high
};

struct Board {
  int pot[4] = {512, 512, 512, 512};  // ADC readings, 0..1023, pots from top to bottom
  bool toggle_on_b = false;
  long gate_high_reads_left = 0;
  bool gate_seen_high = false;
  long gate_reads_this_pass = 0;
  std::vector<int> sampled_adc_channels;
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
    unsigned channel = (word >> 15) & 1;
    board.dac[channel].push_back(DacWrite{word & 0x0FFF, board.gate_seen_high});
  }
  board.dac_selected = false;
}

void shiftOut(uint8_t, uint8_t, uint8_t, uint8_t value) {
  if (board.dac_selected) board.frame.push_back(value);
}

int digitalRead(uint8_t pin) {
  if (pin == gatePin) {
    if (++board.gate_reads_this_pass > MAX_GATE_READS_PER_PASS)
      throw TestFailure{"loop() did not return: one pass read the gate input more than " +
                        std::to_string(MAX_GATE_READS_PER_PASS) + " times"};
    board.gate_seen_high = board.gate_high_reads_left > 0;
    if (board.gate_seen_high) --board.gate_high_reads_left;
    return board.gate_seen_high ? LOW : HIGH;  // the input transistor inverts the jack
  }
  if (pin == SW) return board.toggle_on_b ? LOW : HIGH;  // HIGH selects envelope A
  return LOW;
}

int analogRead(uint8_t channel) {
  board.sampled_adc_channels.push_back(channel);
  return channel < 4 ? board.pot[channel] : 0;  // ADC4 is the DAC clock line, which idles low
}

namespace {

// ---------------------------------------------------------------------------
// Driving the board and reading the outputs
// ---------------------------------------------------------------------------

void run_passes(int count) {
  for (int pass = 0; pass < count; ++pass) {
    board.gate_reads_this_pass = 0;
    loop();
  }
}

void boot_with_all_knobs_at(int position) {
  for (int &reading : board.pot) reading = position;
  setup();
}

// Holds the gate high for `reads` reads of the gate input, then lets it fall.
// While the gate is held the firmware takes one envelope step per gate read,
// so this is also the number of steps taken with the gate high.
void hold_gate_for(long reads) {
  board.gate_high_reads_left = reads;
  run_passes(1);
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

std::set<int> adc_channels_sampled() {
  return std::set<int>(board.sampled_adc_channels.begin(), board.sampled_adc_channels.end());
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
  boot_with_all_knobs_at(512);  // sustain knob at 512: sustain level 512 * 4 = 2048
  hold_gate_for(400);
  EXPECT_AT_LEAST(peak_output(A), 4000);
  EXPECT_AT_LEAST(peak_output(B), 4000);
  EXPECT_EQ(output_at_end_of_gate(A), 2048);
  EXPECT_EQ(output_at_end_of_gate(B), 2048);
}

TEST(pot_scan_samples_only_the_four_pot_channels) {
  boot_with_all_knobs_at(512);
  run_passes(20);
  EXPECT_EQ(adc_channels_sampled(), (std::set<int>{ATTACK_POT, DECAY_POT, SUSTAIN_POT, RELEASE_POT}));
}

TEST(released_gate_returns_both_outputs_to_zero) {
  boot_with_all_knobs_at(512);
  hold_gate_for(400);
  run_passes(2000);
  EXPECT_EQ(current_output(A), 0);
  EXPECT_EQ(current_output(B), 0);
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
    else if (WIFSIGNALED(status))
      printf("FAIL  %s\n      crashed: %s\n", test.name, strsignal(WTERMSIG(status)));
    else
      printf("FAIL  %s\n      exited with status %d, see the sanitizer message above\n", test.name, WEXITSTATUS(status));
  }
  printf("\n%d of %zu tests failed\n", failed, registered_tests().size());
  return failed == 0 ? 0 : 1;
}
