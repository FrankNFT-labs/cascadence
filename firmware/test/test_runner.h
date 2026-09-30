// A small test runner for the host-side tests. TEST registers a test, the
// EXPECT macros check values, and test_runner.cpp runs each test in its own
// process. This header includes no POSIX headers, because a sketch's global
// names can collide with theirs: PolyCrossClock has a variable named sync.
#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "fake_board.h"

namespace fake {

void register_test(const char *name, void (*body)());

struct RegisterTest {
  RegisterTest(const char *name, void (*body)()) { register_test(name, body); }
};

// Runs `scenario` in a child process: one more power-up of the sketch, from the
// board as it is in this process. The fork is the reset, so a test that needs
// several power-ups runs each one this way. Returns what the scenario returns;
// a failure inside it fails the test.
std::string in_child_process(const std::function<std::string()> &scenario);

inline void expect_equal(long actual, long expected, const char *expression, int line) {
  if (actual == expected) return;
  std::ostringstream message;
  message << "line " << line << ": " << expression << " is " << actual << ", expected " << expected;
  throw TestFailure{message.str()};
}

inline void expect_at_least(long actual, long minimum, const char *expression, int line) {
  if (actual >= minimum) return;
  std::ostringstream message;
  message << "line " << line << ": " << expression << " is " << actual << ", expected at least " << minimum;
  throw TestFailure{message.str()};
}

inline void expect_less(long actual, long bound, const char *expression, int line) {
  if (actual < bound) return;
  std::ostringstream message;
  message << "line " << line << ": " << expression << " is " << actual << ", expected less than " << bound;
  throw TestFailure{message.str()};
}

template <typename Container>
std::string describe(const Container &values) {
  std::ostringstream text;
  text << "{";
  bool first = true;
  for (const auto &value : values) {
    text << (first ? "" : ", ") << value;
    first = false;
  }
  text << "}";
  return text.str();
}

inline void expect_equal(const std::set<int> &actual, const std::set<int> &expected, const char *expression,
                         int line) {
  if (actual == expected) return;
  throw TestFailure{"line " + std::to_string(line) + ": " + expression + " is " + describe(actual) + ", expected " +
                    describe(expected)};
}

inline void expect_equal(const std::vector<unsigned> &actual, const std::vector<unsigned> &expected,
                         const char *expression, int line) {
  if (actual == expected) return;
  throw TestFailure{"line " + std::to_string(line) + ": " + expression + " is " + describe(actual) + ", expected " +
                    describe(expected)};
}

inline void expect_equal(const std::string &actual, const std::string &expected, const char *expression, int line) {
  if (actual == expected) return;
  throw TestFailure{"line " + std::to_string(line) + ": " + expression + " is \"" + actual + "\", expected \"" +
                    expected + "\""};
}

}  // namespace fake

#define EXPECT_EQ(actual, expected) fake::expect_equal((actual), (expected), #actual, __LINE__)
#define EXPECT_AT_LEAST(actual, minimum) fake::expect_at_least((actual), (minimum), #actual, __LINE__)
#define EXPECT_LESS(actual, bound) fake::expect_less((actual), (bound), #actual, __LINE__)

#define TEST(name)                                 \
  void name();                                     \
  fake::RegisterTest register_##name(#name, name); \
  void name()

#endif
