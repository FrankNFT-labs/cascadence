# ADSR

Two envelope generators, A and B, for the Cascadence module. One gate input starts both, and each envelope keeps its own attack, decay, sustain and release settings, so a single gate can drive two different shapes: the volume and the filter of one voice, for example.

Each stage is an exponential curve, fast at first and slower as it nears its target, like the charge and discharge of a capacitor in an analog envelope.

## Panel

| Position | Control |
|---|---|
| Top | Toggle |
| Knob 1, right | Attack |
| Knob 2, left | Decay |
| Knob 3, right | Sustain |
| Knob 4, left | Release |
| Right, above the input | LED |
| Right | Gate input |
| Bottom left | Output A |
| Bottom right | Output B |
| Back of the module | Micro-USB |

The module has no push buttons: the toggle is its only switch.

## Controls

### Toggle

The toggle chooses which envelope the four knobs edit. Flip it left to edit envelope A and right to edit envelope B.

Flipping it changes nothing by itself. The envelope you switch to keeps its own settings until you turn a knob, and then only that knob's setting jumps to the knob's position. After setting up A, you can switch to B and change only B's release. B keeps its own attack, decay and sustain, even though the knobs now point at A's values.

A knob has to move by a few steps, about half a percent of its travel, before the firmware takes it as a change. This keeps noise in the knob readings from nudging a setting.

### Attack (knob 1)

The attack knob sets how quickly the envelope rises after a gate arrives: a few milliseconds fully counter-clockwise, several seconds fully clockwise.

The knob is far from linear. Its first three quarters cover short attacks, and the long, slow settings are all in the last quarter of the travel.

The attack aims at the top of the range and hands over to the decay once it passes about 98 percent of it.

### Decay (knob 2)

After the attack peaks, the envelope falls towards the sustain level while the gate is still held. The decay knob sets the speed of that fall, with the same range and the same uneven taper as the attack knob.

### Sustain (knob 3)

Sustain sets the level the envelope holds for as long as the gate stays high, once the decay has finished. Fully counter-clockwise it is 0 V, so the envelope decays all the way down and you get a percussive attack-decay shape. Fully clockwise it sits at the top, so the envelope rises and stays there until the gate ends. This knob is linear.

### Release (knob 4)

When the gate ends, the envelope falls back to 0 V, and the release knob sets how long that takes. Fully counter-clockwise it drops within milliseconds. The taper is uneven like the attack's, but it reaches much further: the last few degrees before fully clockwise stretch the release into minutes.

The release starts from wherever the envelope is when the gate ends, so a gate that ends during the attack or the decay goes straight into the release.

### Gate input and LED

Patch a gate or a clock into the jack above the outputs. Both envelopes start when the gate goes high and release when it goes low; the sustain lasts as long as the gate. Standard 5 V and 10 V Eurorack gates work.

The LED is wired to the input circuit, not to the firmware, so it shows the incoming gate even when no firmware is running.

The envelope only rises while the gate is high, so a short trigger gives a short blip rather than a full envelope. To get a full attack from triggers, stretch them into gates first, or use gates at least as long as the attack.

A new gate during the release starts a new attack from the envelope's current level instead of dropping to 0 V first, which avoids clicks.

### Outputs A and B

The bottom-left jack is envelope A and the bottom-right jack is envelope B. Both outputs are unipolar: they rest at 0 V between notes and peak at about 8 V.

### Micro-USB

The micro-USB connector on the back is for uploading firmware, with the module disconnected from rack power. The steps are under [Uploading the firmware](#uploading-the-firmware).

## When settings take effect

- While a gate is held, the firmware does not read the knobs. Turns made during a long held note take effect once the gate ends.
- After that, attack, decay and sustain changes apply from the next note. Release changes act at once, even on a release that is already running.
- Settings are not stored. At power-up both envelopes copy the current knob positions, so A and B start identical.

## Patch ideas

- For percussion, turn sustain and attack fully counter-clockwise. The decay knob then sets the length of the hit, and the release only matters for gates that end before the decay does.
- For one voice with two shapes, send A to the VCA and B to the filter cutoff. Give A a fast attack and B a slower attack and a lower sustain, so the brightness trails the volume.
- To declick a VCA, turn attack and release fully counter-clockwise and sustain fully clockwise. The output follows the gate with slightly rounded edges.

## Uploading the firmware

1. Set up the Arduino IDE with the Cascadence board package and the micronucleus uploader, as described in [software/README.md](../../software/README.md).
2. Open `ADSR.ino` from this folder and select the Cascadence board under Tools > Board.
3. Disconnect the module from Eurorack power. The software README warns that connecting USB and rack power at the same time can cause over-current warnings on some computers.
4. Click Upload. When the IDE asks for the device, plug the module's micro-USB into the computer. The uploader waits 60 seconds.

## How it works

Each envelope is a first-order low-pass filter, the digital version of a capacitor charging through a resistor. On every step the output moves a fixed fraction of the way towards a target: full scale during the attack, the sustain level during the decay, and 0 V during the release. The knobs set that fraction, which is why each stage is an exponential curve.

The step rate is how fast the firmware loops, so the times above are approximate and not calibrated.

## Credits

Based on [ADSRduino](https://github.com/m0xpd/ADSRduino) by m0xpd ([write-up](http://m0xpd.blogspot.co.uk/2017/02/signal-processing-on-arduino.html)). Cascadence was designed by [cctv.fm](https://www.cctv.fm/product-page/cascadence) with Modular Seattle for Velocity 2019.

For developers: the host-side tests for this firmware are in [test/](test/README.md), and planned changes are in [UPGRADE-PLAN.md](../../UPGRADE-PLAN.md).
