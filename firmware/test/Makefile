# Host-side tests for the Cascadence firmware. Each sketch is compiled as
# ordinary C++ together with the fake board, into one test binary per sketch.
#   make                 build and run every test binary
#   make test_adsr       build one binary; run it with ./test_adsr
#   make SANITIZERS=     build without UndefinedBehaviorSanitizer; run make clean
#                        first, flags given on the command line are not a dependency
#   make clean           remove the binaries
#
# The sketches are compiled with GCC, as the firmware is with avr-gcc: clang
# rejects code that avr-gcc only warns about, such as `updatevalues[A];` in the
# Euclidean and Turing Machine sketches. Without CXX=..., the newest g++-NN on
# the path is used, then g++, and only a real GCC is accepted. On macOS, install
# one with `brew install gcc`.

SKETCH_TESTS := test_adsr test_euclidean test_locking test_polycrossclock test_template test_turing
TESTS := $(foreach test,$(SKETCH_TESTS),$(if $(wildcard $(test).cpp),$(test)))

ifeq ($(origin CXX),default)
CXX := $(firstword $(foreach c,g++-16 g++-15 g++-14 g++-13 g++-12 g++,$(if $(shell $(c) --version 2>/dev/null | grep 'Free Software Foundation'),$(c))))
endif
ifeq ($(CXX),)
$(error No GCC found: install it (on macOS, brew install gcc) or pass CXX=<a g++>)
endif

CXXFLAGS ?= -g -O0 -Wall -Wextra
# The firmware build's dialect (avr-gcc 7.3 defaults to gnu++14), and a signed
# char, as on the ATtiny: some hosts make char unsigned.
override CXXFLAGS += -std=gnu++14 -fsigned-char

# UndefinedBehaviorSanitizer with its runtime where the compiler has one (GCC on
# Linux), which names each problem. Otherwise (GCC on macOS) trap mode, which
# stops the test at the first undefined behaviour without a report.
UBSAN_RUNTIME := $(shell echo 'int main() { return 0; }' | $(CXX) -x c++ -fsanitize=undefined -o /dev/null - >/dev/null 2>&1 && echo yes)
SANITIZERS ?= -fsanitize=undefined $(if $(UBSAN_RUNTIME),-fno-sanitize-recover=undefined,-fsanitize-trap=undefined)

SHARED := fake_board.cpp test_runner.cpp fake_arduino.h fake_board.h test_runner.h Makefile

.PHONY: test clean

test: $(TESTS)
	@status=0; for test in $(TESTS); do echo "== $$test"; ./$$test || status=1; echo; done; exit $$status

test_adsr: ../ADSR/ADSR.ino
test_euclidean: ../euclideansequencer/euclideansequencer.ino
test_locking: ../LockingSequencer/LockingSequencer.ino
test_polycrossclock: ../PolyCrossClock/PolyCrossClock.ino
test_template: ../Template/Template.ino
test_turing: ../TuringMachine/TuringMachine.ino

$(TESTS): test_%: test_%.cpp $(SHARED)
	$(CXX) $(CXXFLAGS) $(SANITIZERS) -o $@ $< fake_board.cpp test_runner.cpp

clean:
	rm -rf $(SKETCH_TESTS) *.dSYM
