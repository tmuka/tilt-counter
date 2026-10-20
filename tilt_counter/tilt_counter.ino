/*
  Two-channel tilt counter (RED and BLUE)

  Each SW-520D ball tilt switch adds 1 to its channel's count on every
  change of position: tilting counts 1 and returning counts 1 more.

  One red LED and one blue LED blink in sync, one blink per beat in 4/4.
  Blink number k lights every LED whose count is at least k, so the side
  that's ahead keeps blinking after the other stops. Beat 1 of each bar is a
  longer (accented) blink, so the score can be counted in bars of 4. The score
  plays once after each counted tilt, then the LEDs stay dark. The LEDs go
  dark as soon as a switch starts to move, and the new score plays once the
  tilt has counted.

  Patterns that aren't a score alternate between the LEDs: power-on and a
  clear play INTRO_BARS bars of alternating eighth notes, then while both
  counts are 0 there's one short blip per bar, alternating LEDs.

  The tempo rises with the score so the blinks take about TARGET_MS, between
  MIN_BPM and MAX_BPM. After a button press the score always plays at MIN_BPM.

  The button is wired to the chip's RESET pin. On the AVR boards the counts
  survive that restart. One press plays the score PRESS_PLAYS times at
  MIN_BPM, with PRESS_REST_BARS bars of rest between, then clears both
  counts; a tilt during that playback cancels the clear. A second press
  within PRESS_AGAIN_MS clears them at once.
  Turning the power off also clears them. On the ESP32 the EN button always
  clears them.

  After SLEEP_AFTER_MS with no tilts, the board goes into its lowest-power
  sleep. The next tilt wakes it, and that tilt is counted.

  Supported boards: ATtiny85 (ATTinyCore, 8 MHz internal), Arduino Pro Mini
  (ATmega328P), ESP32 (original ESP32). See README.md for wiring.
*/

#include <Arduino.h>

// ============================== Settings ==============================

const uint16_t DEBOUNCE_MS    = 400;    // switch must hold its new position this long; any rattle restarts the wait
const uint16_t FLIP_LOCKOUT_MS = 1000;  // after a count, that switch can't count again for this long
const uint16_t TILT_GAP_MS    = 600;    // dark time from a switch starting to move to the new score
const uint16_t MIN_BPM        = 100;    // tempo for low scores, and after a button press
const uint16_t MAX_BPM        = 200;    // fastest tempo, for high scores
const uint16_t TARGET_MS      = 6000;   // aim for the blinks to take about this long
const uint8_t  INTRO_BARS     = 1;      // power-on / clear: bars of alternating eighth notes
const uint16_t ZERO_BLIP_MS   = 20;     // both counts 0: short blip once a bar, alternating LEDs (0 = stay dark)
const uint8_t  PRESS_PLAYS    = 2;      // a single button press plays the score this many times,
const uint8_t  PRESS_REST_BARS = 8;     // with this many bars of rest between, then clears
const uint16_t PRESS_AGAIN_MS = 2000;   // a second button press within this clears the counts
const uint16_t PRESS_BOUNCE_MS = 250;   // restarts sooner than this are button bounce, not a second press
const uint32_t SLEEP_AFTER_MS = 10UL * 60UL * 1000UL;  // 10 minutes with no tilts

const uint8_t  BEATS_PER_BAR  = 4;
const uint16_t SLOW_BEAT_MS   = 60000UL / MIN_BPM;  // quarter note at MIN_BPM
const uint16_t FAST_BEAT_MS   = 60000UL / MAX_BPM;  // quarter note at MAX_BPM

// ============================== Pin maps ==============================

#if defined(__AVR_ATtiny85__)
  #define IS_AVR 1
  // Chosen so each side's switch and LED sit on the same side of the chip:
  // red = PB0 (pin 5) + PB2 (pin 7), blue = PB4 (pin 3) + PB3 (pin 2).
  const uint8_t PIN_SW[2]  = {2, 3};    // PB2, PB3 (pin-change wake below assumes these)
  const uint8_t PIN_LED[2] = {0, 4};    // PB0 red, PB4 blue
#elif defined(__AVR_ATmega328P__)
  #define IS_AVR 1
  const uint8_t PIN_SW[2]  = {2, 3};    // PD2, PD3 (pin-change wake below assumes these)
  const uint8_t PIN_LED[2] = {5, 6};
#elif defined(ARDUINO_ARCH_ESP32)
  #if !defined(CONFIG_IDF_TARGET_ESP32)
    #error "Only the original ESP32 is supported (it needs ext0 + ext1 wake on GPIO 25/26)."
  #endif
  #define IS_ESP32 1
  const uint8_t PIN_SW[2]  = {25, 26};  // must be RTC GPIOs so they can wake from deep sleep
  const uint8_t PIN_LED[2] = {18, 19};
#else
  #error "Unsupported board: add a pin map above."
#endif

#if IS_AVR
  #include <avr/sleep.h>
  #include <avr/power.h>
  #include <avr/interrupt.h>

  // Kept through a RESET-button restart (.noinit RAM isn't zeroed at start-up).
  // `check` tells real saved counts from the random RAM left by power-on.
  struct Saved {
    uint16_t count[2];
    uint8_t  armed;      // 1 = a press right now clears the counts
    uint16_t check;
  };
  Saved   saved __attribute__((section(".noinit")));
  uint8_t bootFlags __attribute__((section(".noinit")));

  // Runs before main(): keep why the chip restarted, then clear the flags.
  void readResetFlags() __attribute__((naked, used, section(".init3")));
  void readResetFlags() {
    bootFlags = MCUSR;
    MCUSR = 0;
  }

  uint16_t savedCheck() {
    return 0x5A3C ^ saved.count[0] ^ (saved.count[1] << 1) ^ saved.armed;
  }
#endif
#if IS_ESP32
  #include <esp_system.h>
  #include <esp_sleep.h>
  #include <driver/rtc_io.h>
#endif

// ============================== State ==============================

struct Channel {
  bool     stable;       // debounced switch level
  bool     lastRaw;      // last raw reading
  uint32_t rawSince;     // when lastRaw last changed
  uint32_t lockUntil;    // no new count before this time
  uint16_t count;
};

Channel  ch[2];
uint32_t lastActivity;
uint32_t armFrom;        // start of the press-again window (AVR)
uint32_t armedUntil;     // end of the press-again window
bool     armSoon;        // open the window at armFrom

#if IS_ESP32
// Kept through deep sleep
RTC_DATA_ATTR uint16_t rtcCount[2];
RTC_DATA_ATTR bool     rtcStable[2];
#endif

static inline bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

// ============================== Blink display ==============================
// Both LEDs share one timeline on a beat grid. Blink number `slot` lights each
// LED whose count is >= slot, so the side that's ahead keeps blinking after the
// other stops. Score blinks are always in sync, so the patterns that aren't a
// score alternate between the LEDs instead:
//   PH_INTRO  power-on or a clear: alternating eighth notes for INTRO_BARS bars
//   PH_IDLE   while both counts are 0: one short blip per bar, alternating LEDs
// Each phase ends at a time computed from the previous one, not from millis(),
// so the tempo doesn't drift. After a score the display rests in PH_DONE until
// the next tilt or press.

enum : uint8_t { PH_HOLD, PH_ON, PH_OFF, PH_REST, PH_INTRO, PH_IDLE, PH_DONE };  // PH_REST: within a press's playback

uint8_t  phase = PH_DONE;
uint16_t slot;           // current blink (from 1), or step of the intro / idle pattern
uint16_t slots;          // blinks this cycle = the larger count
bool     scoreDue;       // a tilt counted while the display was held dark
uint8_t  plays;          // times the score has played since the button press
bool     pressScore;     // started by a button press: play at MIN_BPM, then clear
bool     switchMoving;   // a switch has left its counted position and hasn't settled
uint32_t holdStart;      // when the display went dark for a tilt
uint16_t beatMs;         // this cycle's quarter note
uint32_t beatStart;      // when the current blink's beat began
uint32_t until;          // when the current phase ends
uint32_t litUntil;       // when an intro flash or idle blip ends

void setLed(uint8_t i, bool on) { digitalWrite(PIN_LED[i], on ? HIGH : LOW); }

void ledsOff() {
  setLed(0, false);
  setLed(1, false);
}

bool bothZero() { return ch[0].count == 0 && ch[1].count == 0; }

// One step of an alternating pattern: light LED `slot & 1` for `litMs`
void flashStep(uint32_t now, uint16_t litMs) {
  setLed(0, !(slot & 1));
  setLed(1, slot & 1);
  litUntil = now + litMs;
}

// While both counts are 0: a short blip once a bar, alternating LEDs
void startIdle(uint32_t now) {
  beatMs = SLOW_BEAT_MS;
  phase = PH_IDLE;
  // Start on blue: the intro ends on blue, so the first blip runs straight on
  // from it instead of showing as an extra red blink
  slot = 1;
  if (ZERO_BLIP_MS > 0) flashStep(now, ZERO_BLIP_MS);
  until = now + (uint32_t)beatMs * BEATS_PER_BAR;
}

// Power-on or a clear: alternating eighth notes, each lit for the whole eighth
void startIntro(uint32_t now) {
  beatMs = SLOW_BEAT_MS;
  phase = PH_INTRO;
  slot = 0;
  flashStep(now, beatMs / 2);
  until = now + beatMs / 2;
}

// Zero both counts and play the intro to confirm
void clearCounts(uint32_t now) {
  ch[0].count = ch[1].count = 0;
#if IS_AVR
  saved.count[0] = saved.count[1] = 0;
  saved.check = savedCheck();
#endif
  pressScore = false;
  startIntro(now);
}

void startBlink(uint32_t now) {
  for (uint8_t i = 0; i < 2; i++) setLed(i, ch[i].count >= slot);
  bool downbeat = (slot - 1) % BEATS_PER_BAR == 0;
  beatStart = now;
  phase = PH_ON;
  // Lit for an eighth; beat 1 of each bar for a dotted eighth
  until = now + (downbeat ? beatMs * 3 / 4 : beatMs / 2);
}

void playScore(uint32_t now) {
  slots = ch[0].count > ch[1].count ? ch[0].count : ch[1].count;
  // Pick a tempo so the blinks take about TARGET_MS, within MIN_BPM..MAX_BPM
  uint16_t b = TARGET_MS / slots;
  if (pressScore || b > SLOW_BEAT_MS) b = SLOW_BEAT_MS;
  if (b < FAST_BEAT_MS) b = FAST_BEAT_MS;
  beatMs = b;
  slot = 1;
  startBlink(now);
}

// Play the score from the top: once after a tilt, or PRESS_PLAYS times
// (slowly, then clear) after a button press. With no score yet, a press just
// clears (playing the intro) and anything else goes to the idle blips.
void startScore(uint32_t now, bool press = false) {
  pressScore = press;
  scoreDue = false;
  plays = 0;
  if (bothZero()) {
    if (press) clearCounts(now);
    else startIdle(now);
    return;
  }
  playScore(now);
}

void updateDisplay(uint32_t now) {
  if ((phase == PH_INTRO || phase == PH_IDLE) && reached(now, litUntil)) ledsOff();
  if (phase == PH_HOLD) {
    // Dark while a switch moves. Once it has settled: play the new score if
    // the move counted, pick up a button press's playback, go back to the
    // idle blips at 0-0, or stay dark.
    if (!switchMoving && now - holdStart >= TILT_GAP_MS) {
      if (scoreDue || pressScore || bothZero()) startScore(now, pressScore);
      else phase = PH_DONE;
    }
    return;
  }
  if (phase == PH_DONE || !reached(now, until)) return;
  switch (phase) {
    case PH_ON:
      ledsOff();
      if (slot < slots) {
        phase = PH_OFF;
        until = beatStart + beatMs;
      } else if (pressScore && ++plays < PRESS_PLAYS) {
        // Rest to the end of this bar, then PRESS_REST_BARS more; play again on the downbeat
        uint16_t toBarEnd = (BEATS_PER_BAR - slots % BEATS_PER_BAR) % BEATS_PER_BAR;
        phase = PH_REST;
        until = beatStart + (uint32_t)beatMs * (1 + toBarEnd + PRESS_REST_BARS * BEATS_PER_BAR);
      } else if (pressScore) {
        phase = PH_REST;             // one dark beat, then the clear
        until = beatStart + 2 * beatMs;
      } else {
        phase = PH_DONE;
      }
      break;
    case PH_OFF:
      slot++;
      startBlink(until);
      break;
    case PH_REST:
      if (plays < PRESS_PLAYS) playScore(until);
      else clearCounts(until);
      break;
    case PH_INTRO:
      if (++slot < INTRO_BARS * BEATS_PER_BAR * 2) {
        flashStep(until, beatMs / 2);
        until += beatMs / 2;
      } else {
        startIdle(until);
      }
      break;
    case PH_IDLE:
      slot++;
      if (ZERO_BLIP_MS > 0) flashStep(until, ZERO_BLIP_MS);
      until += (uint32_t)beatMs * BEATS_PER_BAR;
      break;
  }
}

// Go dark as soon as a switch starts to move, so a blink of the old score
// can't run into the new one. If the move turns out to be rattle that doesn't
// count, the score just starts again from the top once the switch settles.
void holdDisplay(uint32_t now) {
  if (phase == PH_HOLD) return;
  ledsOff();
  phase = PH_HOLD;
  holdStart = now;
}

// A tilt cancels a button press's pending clear: the game is still going.
void onCount(uint32_t now) {
  pressScore = false;
  holdDisplay(now);   // usually already dark since the switch started moving
  scoreDue = true;
}

// ============================== Switches ==============================

void pollSwitch(uint8_t i, uint32_t now) {
  Channel &c = ch[i];
  bool raw = digitalRead(PIN_SW[i]);
  if (raw != c.lastRaw) {
    c.lastRaw  = raw;
    c.rawSince = now;
  } else if (raw != c.stable && now - c.rawSince >= DEBOUNCE_MS && reached(now, c.lockUntil)) {
    c.stable = raw;                  // tilt or untilt: both count
    if (c.count < 0xFFFF) c.count++;
    // A see-saw bouncing back off its stop can hold the switch the wrong way
    // for longer than DEBOUNCE_MS; ignore this switch until it has settled.
    c.lockUntil = now + FLIP_LOCKOUT_MS;
#if IS_AVR
    saved.count[i] = c.count;
    saved.check    = savedCheck();
#endif
    lastActivity = now;
    onCount(now);
  }
}

// ============================== Sleep ==============================

#if IS_AVR
// Pin-change interrupts only wake the chip; no work is needed in them.
#if defined(__AVR_ATtiny85__)
EMPTY_INTERRUPT(PCINT0_vect);
#else
EMPTY_INTERRUPT(PCINT2_vect);   // Pro Mini D2/D3
#endif

void setWakeInterrupts(bool on) {
#if defined(__AVR_ATtiny85__)
  GIFR  = _BV(PCIF);
  PCMSK = on ? (_BV(PCINT2) | _BV(PCINT3)) : 0;
  if (on) GIMSK |= _BV(PCIE); else GIMSK &= ~_BV(PCIE);
#else
  PCIFR  = _BV(PCIF2);
  PCMSK2 = on ? (_BV(PCINT18) | _BV(PCINT19)) : 0;
  if (on) PCICR |= _BV(PCIE2); else PCICR &= ~_BV(PCIE2);
#endif
}

void goToSleep() {
  ledsOff();
  cli();
  // If a switch already moved, don't sleep; the loop counts it right away.
  if (digitalRead(PIN_SW[0]) == ch[0].stable && digitalRead(PIN_SW[1]) == ch[1].stable) {
    setWakeInterrupts(true);
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
  #ifdef sleep_bod_disable
    sleep_bod_disable();
  #endif
    sei();
    sleep_cpu();          // wakes on any switch change; millis() pauses meanwhile
    sleep_disable();
    setWakeInterrupts(false);
  }
  sei();
  uint32_t now = millis();
  lastActivity = now;
  phase = PH_DONE;        // the waking tilt is counted by pollSwitch(), which starts the blinks
}

void idle() {
  set_sleep_mode(SLEEP_MODE_IDLE);   // nap until the next timer tick
  sleep_mode();
}
#endif

#if IS_ESP32
void goToSleep() {
  ledsOff();
  for (uint8_t i = 0; i < 2; i++) {
    rtcCount[i]  = ch[i].count;
    rtcStable[i] = ch[i].stable;
  }
  // Wake when either switch leaves its last debounced position.
  // ext1 mode 0 is "all low" (ESP_EXT1_WAKEUP_ALL_LOW / ANY_LOW); with one pin, either means "this pin is low".
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_SW[0], ch[0].stable ? 0 : 1);
  esp_sleep_enable_ext1_wakeup(1ULL << PIN_SW[1],
      ch[1].stable ? (esp_sleep_ext1_wakeup_mode_t)0 : ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);  // keep pull-ups alive
  for (uint8_t i = 0; i < 2; i++) {
    rtc_gpio_pullup_en((gpio_num_t)PIN_SW[i]);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_SW[i]);
  }
  esp_deep_sleep_start();   // waking restarts from setup()
}

void idle() { delay(1); }
#endif

// ============================== Main ==============================

void setup() {
  bool pressed = false;   // restarted by a single button press
#if IS_AVR
  ADCSRA &= ~_BV(ADEN);
  power_adc_disable();

  // A RESET-button restart keeps the counts; a second press inside the
  // press-again window clears them. Power-on (or unreadable RAM) starts at 0.
  // The window opens PRESS_BOUNCE_MS after the restart, so a bouncing button
  // contact (several restarts within a few ms) isn't taken for a second press.
  pressed = (bootFlags & _BV(EXTRF)) && !(bootFlags & _BV(PORF)) && saved.check == savedCheck();
  bool cleared = pressed && saved.armed;
  if (!pressed || cleared) saved.count[0] = saved.count[1] = 0;
  saved.armed = 0;
  saved.check = savedCheck();
  armSoon = pressed && !cleared;
#endif
#if IS_ESP32
  setCpuFrequencyMhz(80);   // Wi-Fi and Bluetooth are never started
  bool woke = esp_reset_reason() == ESP_RST_DEEPSLEEP;
  for (uint8_t i = 0; i < 2; i++) rtc_gpio_deinit((gpio_num_t)PIN_SW[i]);
#endif

  for (uint8_t i = 0; i < 2; i++) {
    pinMode(PIN_SW[i], INPUT_PULLUP);
    pinMode(PIN_LED[i], OUTPUT);
  }
  delay(5);   // let the pull-ups settle

  uint32_t now = millis();
  for (uint8_t i = 0; i < 2; i++) {
    Channel &c = ch[i];
    c.stable = digitalRead(PIN_SW[i]);
    c.count  = 0;
#if IS_AVR
    c.count  = saved.count[i];
#endif
#if IS_ESP32
    if (woke) {
      // Start from the saved position, so the tilt that woke the board is counted
      c.count  = rtcCount[i];
      c.stable = rtcStable[i];
    }
#endif
    c.lastRaw  = c.stable;
    c.rawSince  = now;
    c.lockUntil = now;
  }

  lastActivity = now;
  armFrom      = now + PRESS_BOUNCE_MS;
  armedUntil   = now + PRESS_AGAIN_MS;
#if IS_AVR
  if (cleared) {
    clearCounts(now);
    return;
  }
#endif
#if IS_ESP32
  if (woke) return;   // the waking tilt starts the blinks once it's counted
#endif
  if (pressed) startScore(now, true);   // play the score slowly, then clear
  else startIntro(now);                 // power-on
}

void loop() {
  uint32_t now = millis();
  pollSwitch(0, now);
  pollSwitch(1, now);
  // A switch is moving if it has left its counted position (outside its lockout)
  switchMoving = false;
  for (uint8_t i = 0; i < 2; i++)
    if (ch[i].lastRaw != ch[i].stable && reached(now, ch[i].lockUntil)) switchMoving = true;
  if (switchMoving) holdDisplay(now);
  updateDisplay(now);
#if IS_AVR
  if (armSoon && reached(now, armFrom)) {
    armSoon = false;
    saved.armed = 1;   // a press from now until armedUntil clears the counts
    saved.check = savedCheck();
  }
  if (saved.armed && reached(now, armedUntil)) {
    saved.armed = 0;   // too late for a second press: the next press only shows the score
    saved.check = savedCheck();
  }
#endif
  if (now - lastActivity >= SLEEP_AFTER_MS) goToSleep();
  idle();
}
