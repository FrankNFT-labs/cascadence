# ADSR firmware tests

Host-side tests for `ADSR.ino`. They compile the sketch as ordinary C++ against a fake Cascadence board and check what the firmware writes to the DAC, so they run on a PC without the module or the Arduino toolchain.

## Running

```bash
make -C firmware/ADSR/test
```

This needs `make` and a C++11 compiler with UndefinedBehaviorSanitizer, which recent clang and gcc both include, on a POSIX system (macOS, Linux or WSL: the runner forks one process per test). To build without the sanitizer, run `make clean` and then add `SANITIZERS=` to the command; flags given on the command line are not a build dependency.

## How it works

- `fake_arduino.h` declares the Arduino functions the sketch calls, and defines Arduino's `round()` macro so the sketch rounds the way it does on the module.
- `test_adsr.cpp` includes `../ADSR.ino` directly, then defines those functions as a fake board: the four pots, the A/B toggle, the gate input, inverted by the input transistor as on the real board, and the MCP48x2 DAC, which records every value written to outputs A and B and rejects frames that would mute the output, select the wrong gain, or use the wrong pins or bit order.
- Each test runs in its own process, so every test starts from a freshly booted sketch.
- A pass of `loop()` that reads the gate low more than twice is reported as running without a gate instead of hanging the run.
- The host does the sketch's arithmetic in 64-bit double where the ATtiny uses 4-byte floats, so the tests are robust to precision but not bit-exact with the module.

The Arduino build copies this folder but only compiles source files in the sketch folder itself and in `src/`, so the tests do not end up in the firmware.
