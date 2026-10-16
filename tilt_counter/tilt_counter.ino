/*
  Two-channel tilt counter (RED and BLUE)

  Each SW-520D ball tilt switch adds 1 to its channel's count on every
  change of position: tilting counts 1 and returning counts 1 more.

  One red LED and one blue LED blink in sync at 100 bpm. Blink number k
  lights every LED whose count is at least k, so the side that's ahead keeps
  blinking after the other stops. After a pause the cycle repeats. A new tilt
  shows a 500 ms solid "acknowledge" on the side that moved, then both sides
  restart together.

  After SLEEP_AFTER_MS with no tilts, the board goes into its lowest-power
  sleep. The next tilt wakes it, and that tilt is counted.
  The reset button is wired to the chip's RESET/EN pin; a restart clears both
  counts.

  Supported boards: ATtiny85 (ATTinyCore, 8 MHz internal), Arduino Pro Mini
  (ATmega328P), ESP32 (original ESP32). See README.md for wiring.
*/

#include <Arduino.h>

// ============================== Settings ==============================

const uint16_t DEBOUNCE_MS    = 50;     // switch must hold its new position this long
const uint16_t TILT_ACK_MS    = 500;    // solid "acknowledge" shown right after a tilt
const uint16_t ACK_GAP_MS     = 600;    // dark gap between the acknowledge and the count
const uint16_t BLINK_ON_MS    = 300;    // 300 on + 300 off = 100 blinks per minute
const uint16_t BLINK_OFF_MS   = 300;
const uint16_t PAUSE_MS       = 1500;   // dark gap between repeats of the count
const uint16_t ZERO_BLIP_MS   = 20;     // count 0: quick "alive" blip (0 = stay dark)
const uint32_t SLEEP_AFTER_MS = 10UL * 60UL * 1000UL;  // 10 minutes with no tilts

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
  uint16_t count;
};

Channel  ch[2];
uint32_t lastActivity;

#if IS_ESP32
// Kept through deep sleep
RTC_DATA_ATTR uint16_t rtcCount[2];
RTC_DATA_ATTR bool     rtcStable[2];
#endif

static inline bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

// ============================== Blink display ==============================
// Both LEDs share one timeline. Blink number `slot` lights each LED whose
// count is >= slot, so the side that's ahead keeps blinking after the other stops.

enum : uint8_t { PH_ACK, PH_GAP, PH_ON, PH_OFF, PH_PAUSE };

uint8_t  phase = PH_PAUSE;
uint16_t slot;           // current blink, from 1
uint16_t slots;          // blinks this cycle = the larger count
uint32_t until;          // when the current phase ends
uint32_t blipUntil;      // when a zero count's "alive" blip ends
uint8_t  ackMask;        // sides that tilted during the current acknowledge

void setLed(uint8_t i, bool on) { digitalWrite(PIN_LED[i], on ? HIGH : LOW); }

void ledsOff() {
  setLed(0, false);
  setLed(1, false);
}

void startBlink(uint32_t now) {
  for (uint8_t i = 0; i < 2; i++) {
    uint16_t n = ch[i].count;
    setLed(i, n >= slot || (n == 0 && slot == 1 && ZERO_BLIP_MS > 0));
  }
  blipUntil = now + ZERO_BLIP_MS;
  phase = PH_ON;
  until = now + BLINK_ON_MS;
}

void startCycle(uint32_t now) {
  slots = ch[0].count > ch[1].count ? ch[0].count : ch[1].count;
  if (slots == 0) slots = 1;   // one slot so a zero count can still blip
  slot = 1;
  startBlink(now);
}

void updateDisplay(uint32_t now) {
  if (phase == PH_ON && reached(now, blipUntil)) {
    for (uint8_t i = 0; i < 2; i++) if (ch[i].count == 0) setLed(i, false);
  }
  if (!reached(now, until)) return;
  switch (phase) {
    case PH_ACK:
      ledsOff();
      phase = PH_GAP;
      until = now + ACK_GAP_MS;
      break;
    case PH_ON:
      ledsOff();
      if (slot < slots) {
        phase = PH_OFF;
        until = now + BLINK_OFF_MS;
      } else {
        phase = PH_PAUSE;
        until = now + PAUSE_MS;
      }
      break;
    case PH_OFF:
      slot++;
      startBlink(now);
      break;
    default:  // PH_GAP, PH_PAUSE
      startCycle(now);
      break;
  }
}

// Stop the cycle and light the side(s) that just moved. A tilt during the
// acknowledge restarts it. Both sides then begin a fresh cycle together.
void onCount(uint8_t i, uint32_t now) {
  if (phase != PH_ACK) ackMask = 0;
  ackMask |= 1 << i;
  setLed(0, ackMask & 1);
  setLed(1, ackMask & 2);
  phase = PH_ACK;
  until = now + TILT_ACK_MS;
}

// ============================== Switches ==============================

void pollSwitch(uint8_t i, uint32_t now) {
  Channel &c = ch[i];
  bool raw = digitalRead(PIN_SW[i]);
  if (raw != c.lastRaw) {
    c.lastRaw  = raw;
    c.rawSince = now;
  } else if (raw != c.stable && now - c.rawSince >= DEBOUNCE_MS) {
    c.stable = raw;                  // tilt or untilt: both count
    if (c.count < 0xFFFF) c.count++;
    lastActivity = now;
    onCount(i, now);
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
  phase = PH_PAUSE;       // fresh cycle; the waking tilt is counted by pollSwitch()
  until = now;
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
#if IS_AVR
  ADCSRA &= ~_BV(ADEN);
  power_adc_disable();
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
#if IS_ESP32
    if (woke) {
      // Start from the saved position, so the tilt that woke the board is counted
      c.count  = rtcCount[i];
      c.stable = rtcStable[i];
    }
#endif
    c.lastRaw  = c.stable;
    c.rawSince = now;
  }

  until = now;
  lastActivity = now;
}

void loop() {
  uint32_t now = millis();
  pollSwitch(0, now);
  pollSwitch(1, now);
  updateDisplay(now);
  if (now - lastActivity >= SLEEP_AFTER_MS) goToSleep();
  idle();
}
