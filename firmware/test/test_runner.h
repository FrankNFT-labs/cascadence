// A small test runner for the host-side tests. TEST registers a test, the
// EXPECT macros check values, and test_runner.cpp runs each test in its own
// process. This header includes no POSIX headers, because a sketch's global
// names can collide with theirs: PolyCrossClock has a variable named sync.
#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <set>
#include <sstream>
#include <string>

#include "fake_board.h"

namespace fake {

void register_test(const char *name, void (*body)());

struct RegisterTest {
  RegisterTest(const char *name, void (*body)()) { register_test(name, body); }
};

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

inline std::string describe(const std::set<int> &values) {
  std::ostringstream text;
  text << "{";
  for (int value : values) text << (value == *values.begin() ? "" : ", ") << value;
  text << "}";
  return text.str();
}

inline void expect_equal(const std::set<int> &actual, const std::set<int> &expected, const char *expression,
                         int line) {
  if (actual == expected) return;
  throw TestFailure{"line " + std::to_string(line) + ": " + expression + " is " + describe(actual) + ", expected " +
                    describe(expected)};
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
