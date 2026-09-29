# AGENTS.md

This file provides guidance to AI coding agents, such as Claude Code and Codex, when working with code in this repository.

## What this repository is

Cascadence is a 4 HP Eurorack module built around an ATtiny84, made by cctv.fm with Modular Seattle for Velocity 2019. Each firmware is a standalone Arduino sketch in `firmware/<Name>/<Name>.ino`. `software/CCTV/` is the Arduino board package, `hardware/` holds the Eagle schematic, board and panel files, and `firmware/bootloader/` the micronucleus USB bootloader, which comes preinstalled on assembled modules.

`origin` is the fork FrankNFT-labs/cascadence; the original cctvfm/cascadence has been dormant since 2019, so PRs target the fork's `master`. The ADSR is the firmware in active use.

`UPGRADE-PLAN.md` at the root is the roadmap. It holds a review of every sketch with line references, ADRs, the phased plan with hardware checklists, and the decisions made so far. Read it before changing any firmware.

## Commands

Build with the repo's own board package, which is the real target. The package is not installed system-wide, so point arduino-cli at a scratch sketchbook:

```bash
mkdir -p /tmp/cascadence-sketchbook/hardware && cp -R software/CCTV /tmp/cascadence-sketchbook/hardware/
ARDUINO_DIRECTORIES_USER=/tmp/cascadence-sketchbook arduino-cli compile --fqbn CCTV:avr:CCTV firmware/ADSR
```

- The package borrows the `arduino:avr` core (`build.core=arduino:arduino`), so that core must be installed.
- A sketch must stay at or under 6012 bytes of flash: 8 KB minus the bootloader. RAM is 512 bytes.
- The package compiles with `-w`, so this build never shows warnings.
- Every sketch except ADSR has `#include <tinySPI.h>`, which it never uses. Install the library into the same sketchbook (`ARDUINO_DIRECTORIES_USER=... arduino-cli lib install tinySPI`) or build a copy without that line.

For warnings, build with ATTinyCore as a stand-in. The sketches declare `const int MOSI` and `const int SCK`, which ATTinyCore defines as macros, so rename them in a copy first:

```bash
mkdir -p /tmp/lint/ADSR && perl -pe 's/\bMOSI\b/DAC_MOSI/g; s/\bSCK\b/DAC_SCK/g' firmware/ADSR/ADSR.ino > /tmp/lint/ADSR/ADSR.ino
arduino-cli compile --fqbn "ATTinyCore:avr:attinyx4:chip=84,clock=8internal,pinmapping=old" --warnings all /tmp/lint/ADSR
```

`pinmapping=old` matches the pin numbering of the repo's `tiny14` variant. The ADSR is warning-free under `-Wall -Wextra`.

Host tests exist for the ADSR only:

```bash
make -C firmware/ADSR/test                                                    # build with UBSan, run all tests
make -C firmware/ADSR/test clean && make -C firmware/ADSR/test SANITIZERS=   # without the sanitizer
```

There is no single-test filter: `main()` runs every `TEST` in its own forked process and prints PASS or FAIL per test, and the whole suite takes well under a second. Flags given on the make command line are not a build dependency, which is why the second line cleans first. The runner needs a POSIX system.

To upload, build with `--output-dir` and write the hex over the 6-pin ISP header with a USBasp. This is the route for the owner's module, whose USB bootloader never worked: its reset vector skipped micronucleus, and its fuses disable self-programming.

```bash
ARDUINO_DIRECTORIES_USER=/tmp/cascadence-sketchbook arduino-cli compile --fqbn CCTV:avr:CCTV --output-dir /tmp/cascadence-build firmware/ADSR
avrdude -c usbasp-clone -p t84 -U flash:w:/tmp/cascadence-build/ADSR.ino.hex:i
```

- Use `usbasp-clone`. The owner's clone reports its maker as "XWOPEN." instead of "www.fischl.de", so `-c usbasp` fails with "cannot find USB device". The board package defines no programmers, so the Arduino IDE cannot flash over ISP at all.
- avrdude erases the whole chip first, bootloader included. Leave the fuses alone: lfuse 0xE2, hfuse 0xDF, efuse 0xFF, which gives the 8 MHz internal clock the build assumes.
- If the chip may hold firmware that is not in git, back it up first: `avrdude -c usbasp-clone -p t84 -U flash:r:backup.hex:i`.
- The clone's old firmware prints "cannot set sck period" and "USB access errors detected". Both are harmless; success is "bytes of flash verified".

Modules with a working bootloader can also use the micro-USB route in `software/README.md`: click Upload in the Arduino IDE, then plug in the module's USB within 60 seconds. Power the module from one source only, because USB, the programmer's 5 V and the Eurorack regulator all feed the same +5 V rail.

## Hardware facts the code depends on

- ATtiny84 at 8 MHz: `int` is 16 bits, `double` is the same 4-byte type as `float`, and there is no hardware multiplier.
- Pots: ADC channels 0 to 3, top to bottom.
- Toggle: pin 7, wired SPDT between +5 V and ground. Which lever side reads HIGH is not verified on hardware: `firmware/ADSR/README.md` says left, derived from the PCB, while the Turing Machine entry in `firmware/README.MD` implies right.
- Clock or gate input: pin 8, behind an NPN inverter, so it reads LOW while the jack is high. The LED sits in the transistor's collector and is not firmware-driven. The ADSR names this pin both `CLK_IN` and `gatePin`.
- DAC: MCP4812, 10-bit (code comments say MCP4802 or MCP4822), bit-banged with `shiftOut` on data pin 6, clock pin 4 and chip select pin 5. The ATtiny's USI cannot drive it, because the data line sits on the USI input pin and chip select on its output pin, so `tinySPI` can never work here.
- DAC frame built by `setOutput`: bit 15 channel (A=0, B=1), bit 13 gain (`GAIN_2` = 0 = 2x), bit 12 shutdown (`NO_SHTDWN` = 1 = output on), bits 11 to 0 data. A value of 4096 or more spills into the control bits; `setOutput` does not clamp.
- Output stage: TL072 at gain 2, about 8.2 V full scale. Output A is the bottom-left jack and B the bottom-right one.
- Programming header: a 6-pin AVR ISP header (`AVRISP` in the schematic) on the back, just above the micro-USB connector. Its VCC is the +5 V rail. Its SCK, MOSI and MISO lines are the DAC's clock, data and chip select (pins 4, 6 and 5), so under rack power the outputs can jump while a programmer talks to the chip.

## Code architecture

Every sketch is self-contained and carries its own copy of the board boilerplate: pin constants, DAC constants, `setOutput`, often `updatevalues` and `SendPulse`. A fix in one sketch never reaches the others. `firmware/Template/` is the starting point for a new firmware.

Timing is mostly blocking. The Locking Sequencer, Euclidean and Turing Machine busy-wait on the clock input, and the latter two send pulses with a blocking `delay(40)`, as the Template's `SendPulse` teaches; only PolyCrossClock schedules from `micros()`. The ADSR advances its envelopes one step per pass of `loop()`, so envelope times depend on how long a pass takes, which is dominated by the two bit-banged DAC writes. Adding work to the loop retunes every envelope.

### ADSR (`firmware/ADSR/ADSR.ino`)

- Two envelopes, A and B, run the first-order filter step `envelope = (1 - alpha) * drive + alpha * envelope`. The attack drives towards 4096 and hands over to decay once the envelope passes 4000. Decay drives towards `sustain_Level`, which is the sustain pot reading shifted left by 2. The release runs outside the gate loop and reads `alpha3` directly, which keeps the release knob live mid-release.
- Each pass of `loop()` reads the gate, then `checkforchange` samples one pot. A reading that moves more than `THRESHOLD` (5) from `lastanalogread` is applied through `update_params` to the envelope the toggle selects; the other envelope keeps its values. Then `while(gate)` runs attack and decay, and the knobs are not read while the gate is held.
- The pot-to-alpha mapping `sqrt(k * cos((1023 - reading) / 795))` is strongly non-linear: long times live only in the last quarter of a knob's travel.
- Keep the explicit prototypes near the top of the sketch. The Arduino builder would generate them, but the host harness compiles the `.ino` as plain C++.

### Host test harness (`firmware/ADSR/test/`)

- `test_adsr.cpp` includes `fake_arduino.h` (types, API declarations, and Arduino's float `round()` macro), then `#include "../ADSR.ino"`, then defines the fake board with the sketch's own pin constants.
- The fake DAC decodes every frame, records it per channel, and fails a test on a wrong shutdown or gain bit, wrong pins, or LSB-first bit order. The gate is a countdown of high reads, inverted like the real input. A pass that reads the gate low more than twice fails as "running without a gate".
- Every test runs in a forked child, and the fork is the reset: `setup()` does not reinitialise the sketch's globals, so `boot()` throws if a test calls it twice.
- The Arduino builder compiles only the sketch folder's root and `src/`, so `test/` never reaches the firmware.

## Verification before calling a firmware change done

1. The build with the repo board package succeeds and stays within 6012 bytes.
2. The ATTinyCore warnings build is clean.
3. `make -C firmware/ADSR/test` passes when the ADSR changed.
4. Anything touching timing, voltages or panel behaviour gets the hardware checklist of its phase in `UPGRADE-PLAN.md`. Host tests model the board but not its timing or analog stages.

## Documentation that tracks the code

- `firmware/README.MD` is the overview of every firmware, one entry each.
- `firmware/ADSR/README.md` is the user manual for every ADSR control. Update it when ADSR behaviour changes; for example, the planned live-parameter change alters what it says about knobs during a held gate.
- `UPGRADE-PLAN.md` records decisions with dates in DD-MM-YYYY. On 27-09-2026: ADSR parameters stay live while a gate is held, and ADSR loop mode was removed.
