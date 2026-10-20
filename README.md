# Two-Channel Tilt Counter (Red vs Blue)

A cheap, battery-powered score counter for a two-sided see-saw game. Each see-saw has a ball tilt switch, and every change of position adds 1 to that side's count:

- tilting over adds 1
- coming back adds 1 more

A red LED and a blue LED blink out the two scores in time, like a metronome in 4/4. One button plays the final score and then clears it. After 10 minutes with no tilts the board goes into a very low-power sleep; the next tilt wakes it, and that tilt is counted.

- Sketch: [`tilt_counter/tilt_counter.ino`](tilt_counter/tilt_counter.ino)
- Perfboard layout: [`docs/wiring.html`](docs/wiring.html) (open it in a browser)
- Digi-Key shopping lists: [`bom/`](bom/)

## How the LEDs show the score

The score plays **once**, after each counted tilt. Then the LEDs stay dark until something happens.

- **Both LEDs blink in sync, one blink per beat in 4/4.** Blink number *k* lights every LED whose count is at least *k*, so **the side that's ahead keeps blinking after the other has stopped**. A side at 0 stays dark.
- Each blink is lit for an eighth note. **Beat 1 of every bar is accented**: it's lit for a dotted eighth, so you can count the score in bars of 4.
- **The tempo rises with the score**, so the blinks take about 6 s. It never goes below 100 bpm or above 200 bpm.

  | High score | Tempo   | Blinks take |
  | ---        | ---     | ---         |
  | up to 10   | 100 bpm | up to 6 s   |
  | 18         | 180 bpm | 6 s         |
  | 20         | 200 bpm | 6 s         |
  | 36         | 200 bpm | 10.8 s      |

- **The LEDs go dark the moment a switch starts to move.** Once the tilt has counted, at least 0.6 s later, the new score plays from the top. If the move was only rattle and didn't count, the LEDs stay dark.

```
100 bpm, score 6:   | 1 2 3 4 | 1 2
                      █ ▄ ▄ ▄   █ ▄
█ = accented beat 1 (dotted eighth)   ▄ = normal blink (eighth)
```

Score blinks are always in sync, so the patterns that aren't a score **alternate** between red and blue instead:

- **Power-on or a clear:** one bar of 8 alternating eighth notes (red, blue, red, blue…). Each is lit for the whole eighth, so the light passes straight from one LED to the other.
- **While both counts are 0:** one short 20 ms blip per bar, alternating red and blue, until the first point. It stops when the board sleeps.

```
power-on / clear:   | R B R B R B R B |
0–0 idle:           | r . . . | b . . . | r . . . | ...
```

## The button

The button is wired to the chip's RESET pin, so each press restarts the chip. The counts are kept through that restart.

- **Press once:** the final score plays twice at 100 bpm, however high it is, with 8 bars of rest between (about 20 s). Then both counts clear and the alternating pattern plays to confirm.
  - A tilt during that playback cancels the clear, because the game is still going.
  - At 0–0 there's nothing to play, so a press goes straight to the clear.
- **Press again within 2 s:** both counts clear straight away, with the same alternating confirmation.
  - Presses closer together than 0.25 s are treated as the button bouncing, so one press can't count as two.
- **Turning the battery pack off** also clears the counts.

The button works while the board is asleep, too.

This behaviour is for the ATtiny85. On a Pro Mini, the bootloader may clear the chip's reset flags; if so, every press clears the counts. On an ESP32, the EN button always clears them.

## Parts list (ATtiny85 build, about $5)

| Qty | Part                                       | Notes                                                          |
| --- | ---                                        | ---                                                            |
| 1   | ATtiny85-20PU (DIP-8) + 8-pin socket       | Recommended. A Pro Mini 5 V or an ESP32 also work (see below). |
| 2   | SW-520D ball tilt switch                   | ~$0.10 each, no mercury                                        |
| 1   | Red LED + 1 kΩ resistor                    |                                                                |
| 1   | Blue LED + 470 Ω resistor                  |                                                                |
| 1   | Tactile push button                        | play score / clear                                             |
| 1   | 10 kΩ resistor                             | RESET pull-up                                                  |
| 1   | 100 nF capacitor                           | across VCC and GND, close to the chip                          |
| 1   | 3×AA battery holder **with on/off switch** | 4.5 V. 2×AA is too low for a blue LED.                         |
| —   | Perfboard, wire                            | 10×10 holes are enough; see [`docs/wiring.html`](docs/wiring.html) |

You'll also need an ISP programmer for the ATtiny85. This build used a USBtiny-type programmer (it shows up as "FabISP"); a USBasp, or an Arduino Uno/Nano running the "ArduinoISP" example, also works.

## Wiring

```
                  ATtiny85
                +----u----+
       Button 1 |PB5   VCC| 8  +4.5 V (and 10 kΩ from RESET to +)
 Tilt SW B    2 |PB3   PB2| 7  Tilt SW A
 Blue LED     3 |PB4   PB1| 6  (unused)
          GND 4 |GND   PB0| 5  Red LED
                +---------+

 Red side (pins 5, 7) and blue side (pins 2, 3) are on opposite sides of the chip.

 PB0 ──[1 kΩ]──►|── GND      red LED   (long leg toward the resistor)
 PB4 ──[470 Ω]─►|── GND      blue LED
```

- **Tilt switches:** connect each one between its input pin and GND. The chip's internal pull-up does the rest, so no resistor is needed.
- **Button:** connect it between RESET and GND.

The perfboard layout in [`docs/wiring.html`](docs/wiring.html) has a parts-side view, a mirrored solder-side view and a step-by-step build order. To change it, edit `docs/gen_perf.py` and run `python3 gen_perf.py wiring.html perf_template.html` from inside `docs/`.

| Function              | ATtiny85      | Pro Mini | ESP32                         |
| ---                   | ---           | ---      | ---                           |
| Tilt switch A (red)   | PB2 (pin 7)   | D2       | GPIO 25                       |
| Tilt switch B (blue)  | PB3 (pin 2)   | D3       | GPIO 26                       |
| Red LED               | PB0 (pin 5)   | D5       | GPIO 18                       |
| Blue LED              | PB4 (pin 3)   | D6       | GPIO 19                       |
| Button (to GND)       | RESET (pin 1) | RST      | EN (or the board's EN button) |

## Power and battery life

- **3×AA (4.5 V)** powers the ATtiny85 or Pro Mini directly, with no regulator. Use a holder with a switch, or the board slowly drains the batteries over months even while asleep.
- **Pro Mini:** use the **5 V / 16 MHz** version, powered on **VCC** rather than RAW. For low sleep current, remove the power LED and the regulator.
- **ESP32 dev board:** run it from a USB power bank. Its USB chip and regulator draw 5–15 mA even in deep sleep, which drains AAs in a few days. Some power banks shut off when the current is low; a "low-current mode" bank avoids that.

Rough current draw (ATtiny85):

| State  | Current                                           | 3×AA (~2000 mAh) lasts      |
| ---    | ---                                               | ---                         |
| Asleep | ~1 µA (+~100 µA per tilt switch resting *closed*) | years, basically shelf life |
| Awake  | ~3–5 mA                                           | ~400+ hours of play         |

## Programming

### ATtiny85 with `arduino-cli`

One-time setup:

```sh
arduino-cli config add board_manager.additional_urls http://drazzy.com/package_drazzy.com_index.json
arduino-cli core update-index
arduino-cli core install ATTinyCore:avr
```

Put the chip in the programmer (notch matching the socket), then:

```sh
FQBN=ATTinyCore:avr:attinyx5:chip=85,clock=8internal,bod=disable

# Once per new chip: sets the fuses for the 8 MHz internal clock.
# A new chip runs at 1 MHz, so this uses the slow programmer setting.
arduino-cli burn-bootloader --fqbn $FQBN -P usbtinyisp

# Every time the sketch changes:
arduino-cli compile --fqbn $FQBN tilt_counter
arduino-cli upload  --fqbn $FQBN -P usbtinyisp2 tilt_counter
```

After burning, the fuses read lfuse `0xE2`, hfuse `0xD7`, efuse `0xFF`.

Always unplug the programmer before taking the chip out, and turn the battery pack off before putting it back in the board.

### Arduino IDE instead

- Add the ATTinyCore boards URL above in *Settings → Additional boards manager URLs*, then install **ATTinyCore** in the Boards Manager.
- Board: *ATtiny25/45/85 (No bootloader)*. Chip: ATtiny85. Clock: **8 MHz (internal)**. B.O.D.: **Disabled**.
- Programmer: *USBtinyISP* (or USBasp / Arduino as ISP).
- Run **Tools → Burn Bootloader** once per new chip, then upload with **Sketch → Upload Using Programmer**.

### Other boards

- **Pro Mini:** Board *Arduino Pro or Pro Mini*, Processor *ATmega328P (5 V, 16 MHz)*. Upload through a USB-serial adapter (FTDI/CH340).
- **ESP32:** install the *esp32* board package by Espressif. Board: *ESP32 Dev Module*. Upload over USB.

Only the ATtiny85 build has been compiled and tested on hardware.

## Mounting the tilt switches

The SW-520D is closed when its pins point down and opens when tilted past about 30–45°. Mount it on the see-saw at an angle so that one end-position is clearly closed and the other is clearly open. Both directions count, so it doesn't matter which position is "closed".

The firmware has two layers against false counts from impacts:

- **`DEBOUNCE_MS` (400 ms):** a change only counts after the switch has held its new position that long, and any rattle restarts the wait. A jolt that shakes the ball and lets it fall back adds nothing.
- **`FLIP_LOCKOUT_MS` (1 s):** after a count, that switch is ignored for a second. This covers a see-saw bouncing back off its stop far enough to flip the switch for a moment.

If you still see double counts, first check the mounting: each resting position should be well past the switch's trip angle. Then try raising `DEBOUNCE_MS` (for example to 600). Hot glue or a foam pad under the board also absorbs some shock.

## Settings

All settings are at the top of the sketch.

**Switches**

| Setting           | Default         | Purpose                                                    |
| ---               | ---             | ---                                                        |
| `DEBOUNCE_MS`     | 400             | switch must hold its new position this long to count       |
| `FLIP_LOCKOUT_MS` | 1000            | after a count, that switch can't count again for this long |
| `TILT_GAP_MS`     | 600             | dark time from a switch starting to move to the new score  |
| `SLEEP_AFTER_MS`  | 600000 (10 min) | time with no tilts before sleep                            |

**Display**

| Setting        | Default | Purpose                                                            |
| ---            | ---     | ---                                                                |
| `MIN_BPM`      | 100     | tempo for low scores, and after a button press                     |
| `MAX_BPM`      | 200     | fastest tempo, for high scores                                     |
| `TARGET_MS`    | 6000    | the tempo is set so the score's blinks take about this long        |
| `INTRO_BARS`   | 1       | bars of alternating eighth notes at power-on and after a clear     |
| `ZERO_BLIP_MS` | 20      | blip once a bar, alternating LEDs, while both counts are 0 (0 = off) |

**Button**

| Setting           | Default | Purpose                                                          |
| ---               | ---     | ---                                                              |
| `PRESS_PLAYS`     | 2       | times a single press plays the score before clearing             |
| `PRESS_REST_BARS` | 8       | bars of rest between those plays                                 |
| `PRESS_AGAIN_MS`  | 2000    | a second press within this clears the counts straight away       |
| `PRESS_BOUNCE_MS` | 250     | restarts closer together than this are button bounce, not a second press |

## Bench test

1. **Power-on:** switch the battery pack on. You should see one bar of red and blue alternating, then a short blip once a bar, red and blue in turn.
2. **First points:** tilt switch A and return it. Red should play 1 blink after the first tilt, then 2 blinks after the return, while blue stays dark.
3. **Leader keeps going:** tilt switch B 5 times. Both LEDs should blink together for 2 beats, then blue carries on alone for 3 more. Beats 1 and 5 should be longer.
4. **Interrupting:** tilt a switch in the middle of the score. The LEDs should go dark at once, then play the new score from the top.
5. **Single press:** the score should play twice at 100 bpm, 8 bars apart. Then the alternating pattern plays and the idle blips start.
6. **Tilt cancels the clear:** tilt a few times, press once, and tilt during the playback. The counts should stay.
7. **Double press:** tilt a few times, then press twice quickly. The alternating pattern should play straight away.
8. **Sleep:** set `SLEEP_AFTER_MS` to `15000`. The LEDs should go dark after 15 s with no tilts. One tilt should wake the board and add 1 to the previous count. Measure the sleep current with a multimeter in series with the battery.

## Notes

- **Counts are kept only in RAM:** they survive sleep and the button's restart, but not turning the power off.
- The perfboard layout page is generated, so edit `docs/gen_perf.py` or `docs/perf_template.html` rather than `docs/wiring.html`.
