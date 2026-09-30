// Runs every registered test in its own process, so each one starts from a
// freshly booted sketch, and a crash or a sanitizer stop fails only that test.
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <vector>

#include "test_runner.h"

namespace {

struct TestCase {
  const char *name;
  void (*body)();
};

std::vector<TestCase> &registered_tests() {
  static std::vector<TestCase> tests;
  return tests;
}

}  // namespace

void fake::register_test(const char *name, void (*body)()) { registered_tests().push_back(TestCase{name, body}); }

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
      } catch (const fake::TestFailure &failure) {
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
    else if (WIFSIGNALED(status) && (WTERMSIG(status) == SIGTRAP || WTERMSIG(status) == SIGILL))
      printf("FAIL  %s\n      undefined behaviour, stopped by the sanitizer in trap mode; a build with the "
             "sanitizer runtime (GCC on Linux) names it\n",
             test.name);
    else if (WIFSIGNALED(status))
      printf("FAIL  %s\n      crashed: %s\n", test.name, strsignal(WTERMSIG(status)));
    else
      printf("FAIL  %s\n      exited with status %d, see the message on stderr above\n", test.name,
             WEXITSTATUS(status));
  }
  printf("\n%d of %zu tests failed\n", failed, registered_tests().size());
  return failed == 0 ? 0 : 1;
}
