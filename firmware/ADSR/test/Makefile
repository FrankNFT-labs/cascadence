# Host-side tests for the ADSR firmware. Builds the sketch together with a fake
# board and runs every test. Needs make and a C++11 compiler, no Arduino toolchain.
#   make               build and run the tests
#   make SANITIZERS=   same, without UndefinedBehaviorSanitizer
#   make clean         remove the test binary

CXXFLAGS ?= -std=c++11 -g -O0 -Wall -Wextra
SANITIZERS ?= -fsanitize=undefined -fno-sanitize-recover=undefined

.PHONY: test clean

test: test_adsr
	./test_adsr

test_adsr: test_adsr.cpp fake_arduino.h ../ADSR.ino
	$(CXX) $(CXXFLAGS) $(SANITIZERS) -o $@ test_adsr.cpp

clean:
	rm -rf test_adsr test_adsr.dSYM
