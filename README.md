# Two-Channel Tilt Counter (Red vs Blue)

A cheap, battery-powered counter for two see-saw–style parts. Each part has a ball tilt switch. Every change of position adds 1 to that side's count:

- tilting adds 1
- returning adds 1 more

A red LED and a blue LED blink out the two scores. A reset button clears both counts. After 10 minutes with no tilts the board goes into a deep, very low-power sleep. The next tilt wakes it, and that tilt is counted.

Sketch: [`tilt_counter/tilt_counter.ino`](tilt_counter/tilt_counter.ino)

## How the LEDs show the count

- Both LEDs **blink in sync** at 100 bpm (300 ms on, 300 ms off). Blink number *k* lights every LED whose count is at least *k*.
- The **side that's ahead keeps blinking after the other has stopped**, so you can see who's winning at a glance.
- After the last blink there's a 1.5 s pause, then the cycle repeats.
- A tilt cuts in right away. The side that moved lights solid for 500 ms, both go dark for 0.6 s, then both start a fresh cycle together.
- A count of 0 shows a tiny "alive" blip on the first beat.

## Parts list (ATtiny85 build, about $5)

| Qty | Part | Notes |
|---|---|---|
| 1 | ATtiny85 (DIP-8) + 8-pin socket | Recommended. A Pro Mini 5 V or an ESP32 also work (see below). |
| 2 | SW-520D ball tilt switch | ~$0.10 each, no mercury |
| 1 | Red LED + 1 kΩ resistor | |
| 1 | Blue LED + 470 Ω resistor | |
| 1 | Tactile push button | reset |
| 1 | 10 kΩ resistor | RESET pull-up |
| 1 | 100 nF capacitor | across VCC and GND, close to the chip |
| 1 | 3×AA battery holder **with on/off switch** | 4.5 V. 2×AA is too low for a blue LED. |
| — | Perfboard, wire | |

You'll also need a programmer for the ATtiny85: a USBasp (~$5), or any Arduino Uno/Nano running the "ArduinoISP" example.

## Wiring

```
                  ATtiny85
                +----u----+
 RESET button 1 |PB5   VCC| 8  +4.5 V (and 10 kΩ from RESET to +)
 Tilt SW B    2 |PB3   PB2| 7  Tilt SW A
 Blue LED     3 |PB4   PB1| 6  (unused)
          GND 4 |GND   PB0| 5  Red LED
                +---------+

 Red side (pins 5, 7) and blue side (pins 2, 3) are on opposite sides of the chip.

 PB0 ──[1 kΩ]──►|── GND      red LED   (long leg toward the resistor)
 PB4 ──[470 Ω]─►|── GND      blue LED
```

- **Tilt switches**: connect each one between its input pin and GND. The chip's internal pull-up does the rest, so no resistor is needed.
- **Reset button**: connect it between RESET and GND.

| Function | ATtiny85 | Pro Mini | ESP32 |
|---|---|---|---|
| Tilt switch A (red) | PB2 (pin 7) | D2 | GPIO 25 |
| Tilt switch B (blue) | PB3 (pin 2) | D3 | GPIO 26 |
| Red LED | PB0 (pin 5) | D5 | GPIO 18 |
| Blue LED | PB4 (pin 3) | D6 | GPIO 19 |
| Reset button (to GND) | RESET (pin 1) | RST | EN (or the board's EN button) |

## Power and battery life

- **3×AA (4.5 V)** powers the ATtiny85 or Pro Mini directly, with no regulator. Use a holder with a switch, or the board slowly drains the batteries over months even while asleep.
- **Pro Mini**: use the **5 V / 16 MHz** version, powered on **VCC** rather than RAW. For low sleep current, remove the power LED and the regulator.
- **ESP32 dev board**: run it from a USB power bank. Its USB chip and regulator draw 5–15 mA even in deep sleep, which drains AAs in a few days. Some power banks shut off when the current is low; a "low-current mode" bank avoids that.

Rough current draw (ATtiny85):

| State | Current | 3×AA (~2000 mAh) lasts |
|---|---|---|
| Asleep | ~1 µA (+~100 µA per tilt switch resting *closed*) | years, basically shelf life |
| Awake | ~3–5 mA | ~400+ hours of play |

## Programming

1. Install the **Arduino IDE** (or `arduino-cli`). No extra libraries are needed.
2. Board setup:
   - **ATtiny85**:
     - Add the **ATTinyCore** boards URL (`http://drazzy.com/package_drazzy.com_index.json`), then install ATTinyCore.
     - Board: *ATtiny25/45/85 (No bootloader)*. Clock: **8 MHz (internal)**. B.O.D.: **Disabled**. millis()/micros(): Enabled.
     - Programmer: USBasp, or "Arduino as ISP".
     - Run **Tools → Burn Bootloader** once. This sets the clock fuses.
     - Upload with **Sketch → Upload Using Programmer**.
   - **Pro Mini**: Board *Arduino Pro or Pro Mini*, Processor *ATmega328P (5 V, 16 MHz)*. Upload through a USB-serial adapter (FTDI/CH340).
   - **ESP32**: install the *esp32* board package by Espressif. Board: *ESP32 Dev Module*. Upload over USB.

## Mounting the tilt switches

The SW-520D is closed when its pins point down and opens when tilted past about 30–45°. Mount it on the see-saw at an angle so that one end-position is clearly closed and the other is clearly open.

Both directions count, so it doesn't matter which position is "closed". Each full rock (over and back) adds 2.

If you see double counts from the ball rattling when the see-saw hits its stop, raise `DEBOUNCE_MS` (for example to 100).

## Settings

All settings are at the top of the sketch:

| Setting | Default | Purpose |
|---|---|---|
| `DEBOUNCE_MS` | 50 | ignore switch chatter |
| `TILT_ACK_MS` | 500 | solid acknowledge after a tilt |
| `ACK_GAP_MS` | 600 | dark gap after the acknowledge |
| `BLINK_ON_MS` / `BLINK_OFF_MS` | 300 / 300 | 100 bpm blink rate |
| `PAUSE_MS` | 1500 | gap before the count repeats |
| `ZERO_BLIP_MS` | 20 | "alive" blip at count 0 (0 = off) |
| `SLEEP_AFTER_MS` | 600000 (10 min) | idle time before sleep |

## Bench test

1. Tilt switch A and return it. Red should show the 500 ms acknowledge, then blink 2 times, while blue gives only a tiny blip.
2. Tilt switch B 5 times. The blue LED should acknowledge, then both should blink together for 2 beats, and blue should carry on alone for 3 more.
3. Tilt a switch in the middle of a cycle. The cycle should stop right away and restart, with both LEDs in sync.
4. Press reset. Both counts should go back to 0.
5. Set `SLEEP_AFTER_MS` to `15000` for a sleep test. Both LEDs should go dark after 15 s. One tilt should wake the board and add 1 to the previous count. Measure the sleep current with a multimeter in series with the battery.

## Notes

- **Counts are kept only in RAM**: they survive sleep, but not a battery change or a reset.
