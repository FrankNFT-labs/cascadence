In this folder you will find the firmwares available for Cascadence. If you wish to submit your own firmware please get in touch with me (fil@cctv.fm).

They are all written in Arduino and need no extra libraries: they drive the DAC by toggling its pins directly.

## Locking Sequencer - a dual 4 step sequencer

Clock In moves the sequencer to the next step

The 4 potentiometers adjust the pitch of each step.

Switching the toggle to the left or right enables editing of that sequencer, while the other remains unchanged

## ADSR - a dual analog modelling envelope
Simulates a capacitor charging and discharging at the attack, decay, and release stages by modelling a first-order filter

Clock in - trigger/gate to start both envelopes

Potentiometers from top to bottom - Attack, Decay, Sustain, Release

Toggle switch - changes active editing of envelope A or B.

Every control is explained in [ADSR/README.md](ADSR/README.md).

## Euclidean Sequencer
Generates mathematically derived rhythms from a set of input conditions. 32 step.

Clock in - Trigger input

Potentiometers from top to bottom:
1. Length - the total step length of the sequence from 0 to 32
2. Density - the number of pulses to distribute in the sequence, scales from 1 to Length
3. Offset - shifts the sequence right this number of steps. Scales from 1 to Length
4. Randomness - The likelyhood of a pulse state being inverted. At full counter-clockwise, 0 randomness is added. At full clockwise every pulse will be inverted (no pulse becomes pulse, pulse becomes no pulse). In between min and max, the sequence will change at every iteration. The 'coin' is flipped every time a pulse is about to be sent out.

Toggle Switch - changes active editing of sequence A or B.

## Turing Machine

Algorithmic Sequence generator with variable probability of mutation. Implemented here with a linked pulse output and optional quantizer. Each power-up starts a new random sequence.

Clock in - Trigger input

Potentiometers from top to bottom:
1. Randomness - this is the big knob on the original TM. Fully clockwise and your loop is repeated, fully counter clockwise, and your loop is inverted creating a mirrored loop twice as long as the 'length' setting. In the middle is a 50% chance of mutation at every iteration
2. Length - adjustable from 1 to 16. This is the number of bits in the shift register. The output is scaled to the bit length, and the output is taken from the available bits you have set here, for example if length is 3, you get a 3 bit output scaled to the full range of the output, limited by the 'Scale' setting.
3. Scale - the range from the lowest to the highest voltage outputed. Fully clockwise is the max range, fully counter clockwise you get no output.
4. Offset - Shifts the sequence up but preserving relationships. Where Scale and Offset together would go past the top of the output range, the CV stays at the top.

Toggle Switch - Engages a Chromatic Quantizer, in semitones of 1/12 V for 1 V per octave. Left is quantized, Right is unquantized.

A Output - The CV sequence output, updated on every clock

B Output - Sends a 40 ms pulse if the LSB in the shift register is '1', just after the CV on A has changed. You can use this in conjunction with the CV sequence to create interesting patterns.


## PolyCrossClock

A clock generator with polyrhythmic divisions that hard sync or drift when the number of divisions per beat is not a whole number. A cross-rhythm generator.

Clock in - Reset. While the input is high, both outputs restart, and the tempo counts on from the end of the input pulse.

Potentiometers from top to bottom:
1. Tempo - Clock tempo from 30 BPM fully counter-clockwise to 600 BPM fully clockwise
2. Divisions - Splits each beat into 1 to 16 divisions
3. Cross - B plays every Cross-th division, from 1 to 8. 1 plays every division, 2 every other, etc. With 4 divisions and a cross of 3, B pulses every three quarters of a beat: three against four.
4. Randomness - The chance that a pulse on B is skipped. At full counter-clockwise almost none are, about 1 in 1000. For now the reading wraps around, so the chance rises from there to about 1 in 4, four times over the knob's travel.

Toggle Switch - Engages a Division Quantizer. Left is quantized, Right is unquantized.

A Output - Clock trigger, one 40 ms pulse per beat

B Output - Clock division output, 40 ms pulses

## Template

A Simple template to get you started with writing your own Cascadence code. Includes the DAC functions and pin definitions so all the hard work is done for you.
