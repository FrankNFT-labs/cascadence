# Firmware tests

Host-side tests for the Cascadence sketches. Each test binary compiles one sketch as ordinary C++ against a fake Cascadence board and checks what the firmware writes to the DAC, so the tests run on a PC without the module or the Arduino toolchain.

## Running

```bash
make -C firmware/test
```

This builds and runs every test binary. `make -C firmware/test test_adsr` builds a single one, which you then run as `firmware/test/test_adsr`.

You need `make` and GCC on a POSIX system: macOS, Linux or WSL, because the runner forks one process per test. The Makefile insists on GCC even where the system compiler is clang. The firmware itself is compiled with GCC (avr-gcc), and clang rejects code that avr-gcc only warns about, such as `updatevalues[A];` in the Euclidean and Turing Machine sketches. It uses the newest `g++-NN` on the path, then `g++`, and accepts only a real GCC. On macOS, install one with `brew install gcc`, or name a compiler with `CXX=`.

UndefinedBehaviorSanitizer is on by default. With GCC on Linux it names each problem it finds. GCC on macOS has no sanitizer runtime, so there it runs in trap mode: the test stops at the first undefined behaviour, and the runner reports that without details. To build without the sanitizer, run `make clean` and then add `SANITIZERS=`; flags given on the command line are not a build dependency.

## How it works

- `fake_arduino.h` declares the Arduino functions the sketches call, and defines Arduino's `round()`, `bitRead()` and `bitSet()` macros, so the sketches compute the way they do on the module.
- `fake_board.h` and `fake_board.cpp` implement those functions as the Cascadence board: the four pots, the A/B toggle, the clock or gate input, inverted by the input transistor as on the real board, and the MCP48x2 DAC. The DAC records every value written to outputs A and B, and rejects frames that would mute the output, select the wrong gain, or use the wrong pins or bit order. `random()` is avr-libc's generator, so a seed draws the sequence the module draws, and `avr/eeprom.h` stands in for avr-libc's EEPROM calls, on a 512-byte EEPROM that starts erased.
- Time is virtual. Each Arduino call moves it on by an estimate of what the call takes on the ATtiny84 at 8 MHz, and `delay()` by the delay. A test schedules knob turns and toggle flips with `fake::at()`, drives the clock input with `fake::clock_pulses()`, and stops a sketch whose `loop()` never returns with `fake::run_until()`.
- Each `test_<sketch>.cpp` declares the function prototypes that the Arduino builder would generate, then includes its sketch.
- `test_runner.h` holds the `TEST` and `EXPECT` macros, and `test_runner.cpp` runs every test in its own process, so each test starts from a freshly booted sketch and a crash fails only that test. A test that powers the sketch up more than once runs each power-up with `fake::in_child_process()`. The runner's POSIX headers stay out of the files that include a sketch, because some sketch globals share their names: PolyCrossClock has a variable called `sync`.

## What the tests cover

| File | Sketch | Checks |
|---|---|---|
| `test_adsr.cpp` | ADSR | Envelope shape, knob edits per envelope, the pot scan, the dead band near zero, release, retrigger |
| `test_euclidean.cpp` | Euclidean Sequencer | Three pulses spread evenly over eight steps and repeating, knob edits per output, full randomness inverting every step |
| `test_locking.cpp` | Locking Sequencer | Four clocks play the four stored steps, and the toggle side decides which sequence the knobs edit |
| `test_polycrossclock.cpp` | PolyCrossClock | 30 and 600 BPM, whole divisions, cross, and a reset from the clock input |
| `test_template.cpp` | Template | Outputs at zero after boot, knob reads, the 40 ms `SendPulse()` |
| `test_turing.cpp` | Turing Machine | Inverted and locked loops, the offset, the CV held at full scale, the knobs read at power-up, the CV on time with a non-blocking pulse, semitones of 1/12 V, a new sequence on every power-up |

The tests describe what each sketch does right today; the known bugs get their failing tests in phase 2 of `UPGRADE-PLAN.md`. Until then the Euclidean tests work around two of them, and should drop the workarounds when the fixes land. Every test flips the toggle once before the first clock, because `setup()` leaves both outputs unset. The tests that expect an exact rhythm also draw no zeros from `random()`, because a draw of zero inverts a step even with the randomness knob fully counter-clockwise.

## Limits

The host is not an ATtiny84:

- `int` has 32 bits instead of 16, and `long` 64 instead of 32, so overflow and wrap-around bugs, such as `micros()` rolling over, do not show up here.
- `double` has 64 bits instead of 32, so float results can differ from the module's in the last bits, which the tests allow for.
- The time each Arduino call takes is an estimate, not a measurement.

This folder is not a sketch, so the Arduino IDE ignores it and nothing in it ends up in the firmware.
