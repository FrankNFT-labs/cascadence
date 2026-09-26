# ADSR firmware tests

Host-side tests for `ADSR.ino`. They compile the sketch as ordinary C++ against a fake Cascadence board and check what the firmware writes to the DAC, so they run on a PC without the module or the Arduino toolchain.

## Running

```bash
make -C firmware/ADSR/test
```

This needs `make` and a C++11 compiler with UndefinedBehaviorSanitizer, which recent clang and gcc both include. To build without the sanitizer, add `SANITIZERS=` to the command.

## How it works

- `fake_arduino.h` declares the Arduino functions the sketch calls.
- `test_adsr.cpp` includes `../ADSR.ino` directly, then defines those functions as a fake board: the four pots, the A/B toggle, the gate input, inverted by the input transistor as on the real board, and the MCP48x2 DAC, which records every value written to outputs A and B.
- Each test runs in its own process, so every test starts from a freshly booted sketch.
- A pass of `loop()` that reads the gate input more than 100,000 times is reported as stuck instead of hanging the run.

The Arduino build copies this folder but only compiles source files in the sketch folder itself and in `src/`, so the tests do not end up in the firmware.
