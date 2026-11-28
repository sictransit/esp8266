/*
 * DitBoard (CW HID)
 * A straight-key Morse (CW) decoder with a "dit train" keying coach.
 * Board: Waveshare RP2040-Zero (RP2040), Arduino-Pico core (Earle F. Philhower, III).
 * Libraries: U8g2 and Adafruit NeoPixel. The sketch folder and the .ino file must have the
 * same name (ditboard_rp2040zero) and must not contain any other .ino file.
 *
 * WHAT IT DOES
 *   - Reads a Morse key (GPIO, active low) and gives immediate feedback: a 600 Hz sidetone
 *     on a magnetic buzzer, and an optional key-down LED.
 *   - Decodes the keying into characters, printed on Serial (115200) and on an SH1106
 *     128x64 OLED (three text rows).
 *   - Judges HOW WELL you key against a steady reference beat called the "dit train":
 *       * the board's RGB LED (a WS2812, "NeoPixel", on GP16) blinks in the beat of that
 *         reference while online; green -> red is the smoothed phase error of your keying,
 *       * after each character the OLED draws a trace: your key (upper lane) with the dit
 *         train as a thin strip below it; a status row shows speed (WPM) and quality (Q %).
 *   - Types decoded text and sends navigation/editing commands as a USB HID keyboard.
 *     Serial and the OLED retain the decoded text and readable command labels.
 *   - Boots offline: the RGB LED blinks blue (1 s on / 1 s off), with no USB keystrokes.
 *     Three consecutive Vs in one word enable output; <SK> disables it. Neither
 *     control signal is typed. Local decoding, sidetone, speed and quality continue.
 *
 * HARDWARE (pads are labelled GPx on the board)
 *   As-built values and references: ditboard_rp2040zero_schematic.png.
 *   GP14  jack TIP: straight key (or dit paddle) to GND, internal pull-up, LOW = closed.
 *         R4 = 330 ohm in series with the pin, C3 = 10 nF from the contact to GND.
 *   GP13  jack RING: dah paddle, R5 = 330 ohm, C4 = 10 nF. NOT configured or read yet.
 *         A straight key on a mono plug grounds the ring (the plug body shorts ring to
 *         sleeve); with paddles it idles HIGH only once the pull-up is enabled.
 *   GP15  buzzer drive: tone() -> R3 = 1 kohm -> base of Q1 (BC337), with R2 = 100 kohm
 *         from the base to GND (keeps it off during reset). The magnetic
 *         buzzer (coil 15.2 ohm, rated 1.5-15 V) is in the collector circuit, supplied from
 *         the 5 V pad through R1 = 68 ohm, 1/2 W (about 57 mA; smaller = louder, keep it
 *         at 22 ohm or more). D1 (1N4148) sits across the coil, cathode to buzzer + after R1:
 *
 *             +5V --[R1 68]--+---------+
 *                            |         |
 *                       buzzer coil   D1 1N4148
 *                            |         |
 *                            +---------+--- collector
 *             GP15 --[R3 1k]--+--- base Q1 BC337   emitter --- GND
 *                            |
 *                         [R2 100k]
 *                            |
 *                           GND
 *
 *   GP4 / GP5  OLED SDA / SCL (I2C0); the module is powered from the 3V3 pad.
 *   GP16       the onboard WS2812 RGB LED (no wiring needed): the quality indicator.
 *   The board has no plain LED, so a key-down LED is optional (see LED_PIN).
 *
 * HOW IT WORKS - two cooperative loops, with single-owner peripherals and state
 *   Core 1 (setup1/loop1): key polling, sidetone, NeoPixel, decoder, speed and quality.
 *   Core 0 (setup/loop): Serial, USB keyboard and OLED, fed by a bounded snapshot queue.
 *   The timing core never calls USB, Serial or I2C, or waits for queue space.
 *
 * VOCABULARY AND UNITS (read this before changing anything)
 *   - All times are microseconds from micros(); unsigned differences handle normal
 *     intervals across its 71-minute wrap. Signed comparisons assume spans < 2^31 us.
 *   - edge  : a debounced key change. "down" edge = key pressed, "up" edge = released.
 *   - mark  : the time the key is down.  gap: the time it is up between marks.
 *   - dit   : estimated length of one dit in microseconds. WPM = 1,200,000 / dit.
 *   - tick  : one dit of the reference. The reference is a square wave with a period
 *             of 2 ticks. Ideal dits = 1 tick, dahs = 3 ticks, gaps = 1, 3 or 7 ticks:
 *             successive expected edges are an ODD number of ticks apart. Expected
 *             down edges land on even ticks, up edges on odd ticks.
 *   - phase error: how far an edge landed from its expected tick, in dits (+ = late).
 *
 * DESIGN DECISIONS
 *   - Key polling runs on a dedicated core, isolated from display and USB latency.
 *     Cross-core messages contain copies, not pointers to mutable decoder state.
 *   - Quality is measured against a free-running reference that follows you only
 *     partly (PHASE_GAIN), not against ideal element ratios one at a time. That is
 *     what lets it catch drifting, rushing and dragging.
 *   - Speed adapts only after consistent mixed dit/dah marks establish a candidate.
 *     Gaps supply no speed samples; long pauses clear pending confidence. Quality
 *     does not gate speed adaptation.
 *   - The trace uses per-edge timestamps, reference anchors and dit estimates from
 *     a ring buffer; it does not capture every live-reference update between edges.
 *   - The trace is deliberately compact (24 px) to leave room for three text rows: the
 *     key as a tall lane, the reference as a thin strip directly below it.
 *   - The buzzer is switched by a transistor because its coil (15 ohm) needs far more
 *     current than a GPIO pin can deliver; supplying it from the 5 V pad keeps its
 *     current pulses off the 3.3 V regulator that feeds the RP2040 and the OLED.
 *
 * KNOWN LIMITATIONS
 *   - Straight key only: the jack's ring input (paddles, iambic keying) is not read yet.
 *     First characters after boot assume ~15 WPM until the speed estimate has adapted.
 *   - Polling still has local processing jitter; hardware timing needs measurement.
 *   - Single-length marks cannot distinguish slow dits from faster dahs; speed
 *     tracking needs mixed marks. Initial decoding still uses the trusted speed.
 *   - Tuning constants (PHASE_GAIN, Q_GOOD, Q_BAD, timeouts) are first guesses.
 *   - USB keyboard output uses the US layout; the host layout must match for punctuation.
 *     Select Tools > USB Stack > Pico SDK (not Adafruit TinyUSB or No USB).
 *
 * THINGS TO CHECK
 *   - GP15 must idle LOW when the key is up, otherwise the buzzer coil stays energised
 *     (measure with a meter; if needed add digitalWrite(BUZZER, LOW) after noTone()).
 *   - If red and green look swapped on the RGB LED, change NEO_GRB to NEO_RGB below.
 *   - The OLED must be an SH1106; an SSD1306 needs a different U8g2 constructor.
 *   - Many SH1106 modules accept a 1 MHz I2C clock (setBusClock in setup()): ~1 ms/page.
 *
 * ARDUINO IDE GOTCHA
 *   Generated function prototypes can precede custom type declarations. Keep free-
 *   function signatures primitive unless explicit declarations handle that ordering.
 *   That is why edgeIdx() returns an int index instead of an Edge.
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>
#include <Keyboard.h>
#include <tusb.h>
#include <map>
#include <string>
#include <atomic>
#include <pico/util/queue.h>

bool core1_separate_stack = true;  // retain an 8 KB stack on each core
std::atomic<bool> outputReady{false};

// SH1106 128x64, I2C, "F" = full frame buffer in RAM (1 KB). Uses the hardware I2C
// object Wire (I2C0); its pins are set in setup() before the display is started.
// (An SSD1306 module needs a different constructor.)
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);

// ---- pins and fixed settings ----
const int KEY       = 14;    // GP14: jack tip, key (or dit paddle) to GND, internal pull-up (LOW = pressed)
const int KEY_RING  = 13;    // GP13: jack ring (dah paddle), wired but not read yet
const int BUZZER    = 15;    // GP15: tone() output into the base of the buzzer transistor
const int PIXEL_PIN = 16;    // GP16: the onboard WS2812 RGB LED = quality indicator
const int OLED_SDA  = 4;     // GP4 = I2C0 SDA
const int OLED_SCL  = 5;     // GP5 = I2C0 SCL
const int LED_PIN   = -1;    // no plain LED on the Zero: set a pin here for an external key-down LED
const uint16_t TONE_HZ = 600;                 // sidetone frequency
const unsigned long DEBOUNCE_US = 3000;       // lockout after an accepted edge (see loop1())

Adafruit_NeoPixel pixel(1, PIXEL_PIN, NEO_GRB + NEO_KHZ800);   // one pixel (if red and green look swapped: NEO_RGB)

// Key-down LED. The RP2040-Zero has no plain LED (its onboard LED is the WS2812 used as the
// quality indicator), so this does nothing unless LED_PIN names a pin with an external LED.
void led(bool on) { if (LED_PIN >= 0) digitalWrite(LED_PIN, on ? HIGH : LOW); }

// ---- Morse table: dots and dashes -> text ----
// Letters, digits and common punctuation. Prosigns are returned as readable text such
// as "<AR>". Three entries are special:
//   "-...-"    (BT)             -> "\n"   newline
//   "........" (8 dits, error)  -> "\b"   backspace
//   "-.-.-"    (CT / KA)        -> "<CMD>" enters command mode: the NEXT character
//                                           selects a command (see sendCommand()).
// <SK> (...-.-) always returns the keyboard offline, even in command mode.
// std::map is fine on the RP2040 (RAM is plentiful); it would not be on a small AVR.
const std::map<std::string, const char*> MORSE = {
  {".-","A"},{"-...","B"},{"-.-.","C"},{"-..","D"},{".","E"},{"..-.","F"},
  {"--.","G"},{"....","H"},{"..","I"},{".---","J"},{"-.-","K"},{".-..","L"},
  {"--","M"},{"-.","N"},{"---","O"},{".--.","P"},{"--.-","Q"},{".-.","R"},
  {"...","S"},{"-","T"},{"..-","U"},{"...-","V"},{".--","W"},{"-..-","X"},
  {"-.--","Y"},{"--..","Z"},
  {"-----","0"},{".----","1"},{"..---","2"},{"...--","3"},{"....-","4"},
  {".....","5"},{"-....","6"},{"--...","7"},{"---..","8"},{"----.","9"},
  {".-.-.-","."},{"--..--",","},{"..--..","?"},{"-..-.","/"},{"-....-","-"},
  {".-.-.","<AR>"},{"...-.-","<SK>"},{"-.--.","<KN>"},{".-...","<AS>"},
  {"-...-","\n"},{"........","\b"},
  {"-.-.-","<CMD>"},
};

// ---- decoder state (core 1 only) ----
bool last = HIGH;                  // last ACCEPTED key level, HIGH = key up
unsigned long lastChange = 0;      // time of the last accepted edge (us)
float dit = 80000;                 // us; speed estimate, starts at 15 WPM, adapts from marks
char sym[16];                      // dots/dashes of the character being keyed
int n = 0;                         // elements in sym[] (max 15, extra elements are dropped)
bool wordPending = false;          // a character was just decoded: a word space may follow
bool cmdMode = false;              // awaiting a command selector; no idle timeout
int activationVs = 0;
unsigned int keyboardEpoch = 0;    // core 1: odd = online, even = offline
std::atomic<unsigned int> keyboardState{0};
const unsigned long OFFLINE_BLINK_US = 1000000;
unsigned long offlineBlinkT = 0;
bool offlineBlueOn = true;

// ---- confidence-based speed tracking (core 1 only) ----
const int SPEED_WINDOW = 8, SPEED_INLIERS = 7, SPEED_CONFIRM = 3;
const float SPEED_MIN_DIT = 25000.0f, SPEED_MAX_DIT = 400000.0f;
const float SPEED_TOLERANCE = 0.15f, SPEED_CANDIDATE_TOLERANCE = 0.10f;
const float SPEED_GAIN = 0.20f, SPEED_MAX_STEP = 0.03f;
unsigned long speedMarks[SPEED_WINDOW];
int speedHead = 0, speedCount = 0, speedConfirm = 0, speedSinceConfirm = 0;
float speedCandidate = 80000.0f;

void resetSpeedEvidence() {
  speedHead = speedCount = speedConfirm = speedSinceConfirm = 0;
}

void adaptSpeed(unsigned long mark) {
  speedMarks[speedHead] = mark;
  speedHead = (speedHead + 1) % SPEED_WINDOW;
  if (speedCount < SPEED_WINDOW) speedCount++;
  if (speedCount < SPEED_WINDOW) return;
  if (speedSinceConfirm < SPEED_WINDOW) speedSinceConfirm++;

  int bestCount = 0;
  float bestError = 1e9f, candidate = dit;
  // Fit raw marks independently of the decoder's current dit/dah threshold.
  for (int i = 0; i < SPEED_WINDOW; i++) {
    for (int ticks = 1; ticks <= 3; ticks += 2) {
      float trial = (float)speedMarks[i] / ticks;
      if (trial < SPEED_MIN_DIT || trial > SPEED_MAX_DIT) continue;
      int count = 0, dits = 0, dahs = 0;
      float sum = 0, error = 0;
      for (int j = 0; j < SPEED_WINDOW; j++) {
        bool dah = speedMarks[j] > 2 * trial;
        float normalized = (float)speedMarks[j] / (dah ? 3 : 1);
        float residual = fabsf(normalized - trial) / trial;
        if (residual <= SPEED_TOLERANCE) {
          count++;
          if (dah) dahs++; else dits++;
          sum += normalized;
          error += residual;
        }
      }
      if (count < SPEED_INLIERS || dits < 2 || dahs < 2) continue;
      if (count > bestCount || (count == bestCount && error < bestError)) {
        bestCount = count;
        bestError = error;
        candidate = sum / count;
      }
    }
  }
  if (!bestCount) {
    speedConfirm = speedSinceConfirm = 0;
    return;
  }
  if (!speedConfirm ||
      fabsf(candidate - speedCandidate) > SPEED_CANDIDATE_TOLERANCE * speedCandidate) {
    speedConfirm = 1;
    speedSinceConfirm = 0;
    speedCandidate = candidate;
  } else if (speedConfirm < SPEED_CONFIRM && speedSinceConfirm >= SPEED_WINDOW) {
    // Each confirmation needs a fresh window, not the same seven reused marks.
    speedConfirm++;
    speedSinceConfirm = 0;
    speedCandidate = candidate;
  }
  if (speedConfirm < SPEED_CONFIRM) return;
  float normalized = (float)mark / (mark > 2 * candidate ? 3 : 1);
  if (fabsf(normalized - candidate) > SPEED_TOLERANCE * candidate) return;
  speedCandidate = candidate;

  float step = constrain(SPEED_GAIN * (candidate - dit),
                         -SPEED_MAX_STEP * dit, SPEED_MAX_STEP * dit);
  dit = constrain(dit + step, SPEED_MIN_DIT, SPEED_MAX_DIT);
}

// ---- keying quality: the "dit train" reference ----
// Picture a reference wave with a period of 2 dits. Key-down should happen when it
// goes negative, key-up when it goes positive: a dit ends half a period later (1 tick),
// a dah 1.5 periods later (3 ticks). Gaps are 1, 3 or 7 ticks, so two successive edges
// are always an odd number of ticks apart, and key-down edges land on even ticks,
// key-up edges on odd ticks.
//
// The reference is anchored to the previous edge and is nudged only part of the way
// (PHASE_GAIN) towards each new edge, so keying that wanders against the beat shows
// up as phase error. Speed follows only confident mark evidence.
// The first key-down after a long pause locks the phase.
//
// Online, the NeoPixel blinks in beat with this reference: lit while the wave is negative
// (when the key should be down), dark while it is positive. Its colour shows the
// keying quality. It switches off PAUSE_DITS dits after you stop keying.
// Offline, the same LED instead blinks blue independently of reference/quality state.
const float PHASE_GAIN = 0.25f;    // how far the reference follows each edge: 0 = rigid, 1 = follows fully
const float Q_GOOD = 0.10f;        // smoothed phase error (in dits) at or below: green / Q 100 %
const float Q_BAD  = 0.40f;        // at or above: red / Q 0 %
const float PAUSE_DITS = 10;       // idle: reference stops, online pixel off, next key-down relocks
unsigned long anchorT = 0;         // reference time of the latest edge (us), already nudged
bool anchorDown = false;           // true if the anchor tick is a key-down (wave going negative)
bool haveAnchor = false;           // the reference has been locked at least once
bool active = false;               // reference running; drives the LED only online
float qErr = Q_GOOD;               // smoothed |phase error| in dits (EMA 0.8/0.2), 0 = perfect
bool pixelOn = false;              // what the pixel currently shows (avoids redundant show())

// 0 = good keying, 1 = bad: supplied error mapped onto the Q_GOOD..Q_BAD range.
float qualityT(float error) {      // 0 = good, 1 = bad
  return constrain((error - Q_GOOD) / (Q_BAD - Q_GOOD), 0.0f, 1.0f);
}

void pixelOff() {
  pixel.clear();
  pixel.show();
  pixelOn = false;
}

// Online colour is a straight green->red blend (middle = dim olive/yellow).
// Brightness is capped in setup1() with setBrightness(40).
void pixelShow() {                 // quality colour online, blue offline
  if (keyboardEpoch & 1) {
    float t = qualityT(qErr);
    pixel.setPixelColor(0, pixel.Color((uint8_t)(255 * t), (uint8_t)(255 * (1 - t)), 0));
  } else {
    pixel.setPixelColor(0, pixel.Color(0, 0, 255));
  }
  pixel.show();
  pixelOn = true;
}

// The reference wave at time 'now': lit while "negative" (key should be down), dark
// while "positive". k counts whole ticks since the anchor; the anchor's own parity
// (anchorDown) says whether tick 0 is a down or an up half-period.
bool gridLit(unsigned long now) {
  long k = (long)floorf((float)(long)(now - anchorT) / dit);   // ticks since the anchor
  if (k < 0) k = 0;                // anchor nudged slightly ahead of now: still its first tick
  return (((k + (anchorDown ? 0 : 1)) & 1) == 0);
}

// Avoid redundant show() calls: the library can wait for the latch interval and
// masks core 1 interrupts while feeding PIO. Actual timing depends on the library.
void updatePixel(unsigned long now, bool force) {
  bool on;
  if (keyboardEpoch & 1) {
    on = active && gridLit(now);
  } else {
    unsigned long elapsed = now - offlineBlinkT;
    unsigned long steps = elapsed / OFFLINE_BLINK_US;
    if (steps) {
      offlineBlinkT += steps * OFFLINE_BLINK_US;
      if (steps & 1) offlineBlueOn = !offlineBlueOn;
    }
    on = offlineBlueOn;
  }
  if (!on) { if (pixelOn) pixelOff(); }
  else if (!pixelOn || force) pixelShow();
}

void setKeyboardOnline(bool online) {
  activationVs = 0;
  cmdMode = false;
  if (online == ((keyboardEpoch & 1) != 0)) return;
  // Publish mode changes independently of the bounded output queue. An epoch also
  // invalidates old events when offline -> online happens before core 0 catches up.
  keyboardEpoch++;
  keyboardState.store(keyboardEpoch, std::memory_order_release);
  if (!online) {
    offlineBlinkT = micros();
    offlineBlueOn = true;
  }
  updatePixel(micros(), true);
}

// ---- edge history for the trace: where the key really went vs. the reference ----
// One record per accepted edge; reference values are captured before mark-speed
// adaptation. The trace evaluates these snapshots, not every live-reference update.
struct Edge {
  unsigned long t;                 // accepted edge's polling timestamp (us)
  unsigned long anchor;            // reference anchor after this edge (us)
  float dit;                       // dit estimate recorded at this edge, before adaptation
  bool down;                       // true = key-down edge (reference starts "negative")
};
const int EDGE_N = 32;             // more than the ~21 ideally spaced edges in the 21-dit window
Edge edges[EDGE_N];
int edgeHead = 0, edgeCount = 0;   // next write slot / number of valid records

// Record an edge at time t, using the CURRENT anchor/dit/anchorDown (so call it after
// the anchor has been updated).
void pushEdge(unsigned long t) {
  edges[edgeHead] = { t, anchorT, dit, anchorDown };
  edgeHead = (edgeHead + 1) % EDGE_N;
  if (edgeCount < EDGE_N) edgeCount++;
}

// Index into edges[] of the i-th stored edge, 0 = oldest. Returns an int, not an
// Edge: the Arduino IDE puts function prototypes above the struct definition.
int edgeIdx(int i) {
  return (edgeHead + EDGE_N - edgeCount + i) % EDGE_N;
}

// ---- bounded core 1 -> core 0 output, including immutable trace snapshots ----
struct OutputEvent {
  char text[8];
  bool command;
  bool trace;
  bool usb;                        // eligible when decoded; offline/control events never qualify
  unsigned int keyboardEpoch;
  float dit, error;
  unsigned long t;
  int edgeCount;
  Edge edges[EDGE_N];
};
const int OUTPUT_N = 8;
queue_t outputQueue;
std::atomic<unsigned int> outputDropped{0};
unsigned int reportedOutputDropped = 0; // core 0 only
unsigned int usbEpoch = 0;         // core 0's last observed keyboard session
float displayDit = 80000.0f, displayError = Q_GOOD;
Edge displayEdges[EDGE_N];
int displayEdgeCount = 0;

void postOutput(const char* text, bool command, bool trace, bool usb) {
  OutputEvent event{};
  snprintf(event.text, sizeof event.text, "%s", text);
  event.command = command;
  event.trace = trace;
  event.usb = usb && (keyboardEpoch & 1);
  event.keyboardEpoch = keyboardEpoch;
  event.dit = dit;
  event.error = qErr;
  event.t = micros();
  if (trace) {
    event.edgeCount = edgeCount;
    for (int i = 0; i < edgeCount; i++) event.edges[i] = edges[edgeIdx(i)];
  }
  if (!queue_try_add(&outputQueue, &event)) {
    // Single writer: load/store avoids a read-modify-write on Cortex-M0+.
    outputDropped.store(outputDropped.load(std::memory_order_relaxed) + 1,
                        std::memory_order_release);
  }
}

// Classify the gap before a key press as 1 (inside a character), 3 (between
// characters) or 7 (between words) ticks. The thresholds are the midpoints.
int gapTicks(unsigned long g) {    // classify a gap as 1, 3 or 7 ticks
  return (g < 2 * dit) ? 1 : (g < 5 * dit) ? 3 : 7;
}

// Score an edge at time t that is expected 'ticks' dits after the previous edge:
//   1. expected time = anchor + ticks * dit
//   2. phase error e = (t - expected) / dit, clipped to +-1 dit
//   3. quality: qErr = exponential moving average of |e|
//   4. phase loop: move the anchor by PHASE_GAIN of the clipped error
//   5. record the edge for the trace and refresh the appropriate LED state
// Call BEFORE adapting dit, so the edge is judged against the speed that applied to it.
void scoreEdge(unsigned long t, int ticks) {
  unsigned long tick = anchorT + (unsigned long)(long)(ticks * dit);   // expected time
  float e = (float)(long)(t - tick) / dit;                              // phase error in dits, + = late
  e = constrain(e, -1.0f, 1.0f);
  qErr = 0.8f * qErr + 0.2f * fabsf(e);
  anchorT = tick + (unsigned long)(long)(PHASE_GAIN * e * dit);
  pushEdge(t);
  updatePixel(t, (keyboardEpoch & 1) != 0);
}

// ---- display ----
// 128x64 SH1106, full buffer. Layout (pages are 8 px tall):
//   y  0..9    status row
//   y 10..39   three text rows         (status + text cover pages 0..4)
//   y 40..63   trace = pages 5..7      (redrawn after each decoded character)
// Drawing only fills the buffer and marks pages dirty. serviceDisplay() sends ONE
// page per call on core 0; I2C never blocks core 1's key polling.
// Invariant: the text area (pages 0..4) and the trace (pages 5..7) never share a
// page, so redrawing one never forces a resend of the other (TRACE_Y is a multiple of 8).
#define DISPLAY_FONT u8g2_font_6x10_tf
const int CHAR_W = 6, ROW_H = 10;  // font cell; change together with DISPLAY_FONT
const int COLS = 128 / CHAR_W;     // 21 characters per text row
const int ROWS = 3;                // text rows below the status row
const int TRACE_Y = 40;            // first pixel row of the trace area (a multiple of 8)

char lines[ROWS][COLS + 1];        // text shown on the OLED (+1 for the terminating 0)
int curLine = 0, col = 0;          // cursor: current text row and column
uint8_t dirty = 0;                 // bit n set = page n needs sending

// Mark every 8-pixel page that touches pixel rows y0..y1 as dirty.
void markPages(int y0, int y1) {
  for (int p = y0 / 8; p <= y1 / 8 && p < 8; p++) dirty |= (1 << p);
}

void markRows(int firstRow, int lastRow) {          // display rows, 0 = status row
  markPages(firstRow * ROW_H, lastRow * ROW_H + ROW_H - 1);
}

// Send the lowest dirty page (128 bytes, ~3 ms at 400 kHz). updateDisplayArea() takes
// tile coordinates: x tile 0, y tile p, 16 tiles wide, 1 tile high.
void serviceDisplay() {            // send one dirty page
  if (!dirty) return;
  int p = 0;
  while (!(dirty & (1 << p))) p++;
  dirty &= ~(1 << p);
  u8g2.updateDisplayArea(0, p, 16, 1);
}

// Status row uses core 0's copied speed/quality snapshot, not live core 1 state.
// WPM = 1,200,000 / displayDit; Q is 100 % at Q_GOOD and 0 % at Q_BAD.
void drawStatus() {
  char buf[26];
  int q = (int)(100 * (1.0f - qualityT(displayError)) + 0.5f);
  snprintf(buf, sizeof buf, "%s %2d WPM  Q %3d%%", (usbEpoch & 1) ? "CW" : "OFF",
           (int)(1200000.0 / displayDit + 0.5), q);
  u8g2.setDrawColor(0); u8g2.drawBox(0, 0, 128, ROW_H); u8g2.setDrawColor(1);
  u8g2.drawStr(0, 0, buf);
  markRows(0, 0);
}

// Redraw text row i (0-based; display row i+1, because row 0 is the status row).
void drawRow(int i) {
  u8g2.setDrawColor(0); u8g2.drawBox(0, (i + 1) * ROW_H, 128, ROW_H); u8g2.setDrawColor(1);
  u8g2.drawStr(0, (i + 1) * ROW_H, lines[i]);
}

// Move to the next text row; when the bottom is reached, scroll everything up.
void newLine(bool &scrolled) {
  if (curLine < ROWS - 1) curLine++;
  else {
    memmove(lines[0], lines[1], (ROWS - 1) * (COLS + 1));
    scrolled = true;
  }
  lines[curLine][0] = 0;
  col = 0;
}

// Append text to the on-screen lines. '\n' starts a new row, '\b' deletes the previous
// character on the CURRENT row only (it does not jump back a row). Only the rows that
// changed are redrawn (all of them if the text scrolled).
void showText(const char* s) {
  bool scrolled = false;
  int from = curLine;
  for (; *s; s++) {
    char c = *s;
    if (c == '\n') newLine(scrolled);
    else if (c == '\b') { if (col > 0) lines[curLine][--col] = 0; }
    else {
      if (col >= COLS) newLine(scrolled);
      lines[curLine][col++] = c;
      lines[curLine][col] = 0;
    }
  }
  if (scrolled) {
    for (int i = 0; i < ROWS; i++) drawRow(i);
    markRows(1, ROWS);
  } else {
    for (int i = from; i <= curLine; i++) drawRow(i);
    markRows(from + 1, curLine + 1);
  }
}

// ---- trace: your key with the dit train as a thin strip below it, last ~21 dits ----
// Upper lane (tall)  = the key:       a bar wherever it really was down.
// Lower strip (thin) = the reference: a bar wherever the key SHOULD be down (the dit train).
// Keyed perfectly, every key bar sits exactly above reference bars (a dit above one bar,
// a dah above bar-gap-bar); early, late, short or long elements show as misalignment.
// Layout inside the 24 px trace area (y 40..63): key bars y 41..54, key baseline y 55,
// reference bars y 57..60, reference baseline y 61.
const int PX_PER_DIT = 6;          // horizontal scale: 128 px = ~21 dits at any speed
const int KEY_Y = 41, KEY_H = 14;  // key bars; baseline just below
const int REF_Y = 57, REF_H = 4;   // reference strip; baseline just below

// Draw the trace into the frame buffer (and mark its pages dirty). The picture is a
// window of 128 px ending at time tEnd. For every pixel column the corresponding time
// is looked up in the edge history:
//   - reference lane: find the latest reference segment (anchor <= T) and evaluate its
//     square wave with ITS OWN dit and parity; segments older than PAUSE_DITS are blank
//   - key lane: the key is down if the latest edge before T was a down edge
// ri/ki only move forward because T increases with x.
void renderTrace(unsigned long tEnd) {            // tEnd = right edge of the picture
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, TRACE_Y, 128, 64 - TRACE_Y);
  u8g2.setDrawColor(1);
  u8g2.drawHLine(0, REF_Y + REF_H, 128);
  u8g2.drawHLine(0, KEY_Y + KEY_H, 128);

  if (displayEdgeCount > 0) {
    float pxDur = displayDit / PX_PER_DIT;        // us per pixel
    unsigned long tStart = tEnd - (unsigned long)(128 * pxDur);
    int ri = -1, ki = -1;                         // current reference segment / key edge
    for (int x = 0; x < 128; x++) {
      unsigned long T = tStart + (unsigned long)(x * pxDur);
      while (ri + 1 < displayEdgeCount && (long)(T - displayEdges[ri + 1].anchor) >= 0) ri++;
      while (ki + 1 < displayEdgeCount && (long)(T - displayEdges[ki + 1].t) >= 0) ki++;

      if (ri >= 0) {
        const Edge& r = displayEdges[ri];
        float dt = (float)(long)(T - r.anchor);
        if (dt <= PAUSE_DITS * r.dit) {
          long k = (long)(dt / r.dit);
          if (((k + (r.down ? 0 : 1)) & 1) == 0) u8g2.drawVLine(x, REF_Y, REF_H);
        }
      }
      if (ki >= 0 && displayEdges[ki].down) u8g2.drawVLine(x, KEY_Y, KEY_H);
    }
  }
  markPages(TRACE_Y, 63);
}

// Local feedback only: command labels must not be typed into the host application.
void emit(const char* s) { Serial.print(s); showText(s); }

// ---- USB keyboard: one press/release report per pass, no Keyboard.write() delays ----
const int USB_KEY_N = 32;
uint8_t usbKeys[USB_KEY_N];
int usbKeyHead = 0, usbKeyCount = 0;
bool usbKeyDown = false;

void syncKeyboardState() {
  unsigned int epoch = keyboardState.load(std::memory_order_acquire);
  if (epoch == usbEpoch) return;
  usbKeyHead = usbKeyCount = 0;
  usbEpoch = epoch;
  Serial.println((epoch & 1) ? "\nKeyboard ONLINE" : "\nKeyboard OFFLINE");
  drawStatus();
}

void queueKey(uint8_t key) {
  if (!(usbEpoch & 1) || keyboardState.load(std::memory_order_acquire) != usbEpoch) return;
  if (!tud_mounted()) {
    Serial.println("\nUSB keyboard: not connected; key dropped");
    return;
  }
  if (usbKeyCount == USB_KEY_N) {
    Serial.println("\nUSB keyboard: queue full; key dropped");
    return;
  }
  usbKeys[(usbKeyHead + usbKeyCount) % USB_KEY_N] = key;
  usbKeyCount++;
}

void typeText(const char* s, bool usb) {
  emit(s);
  if (!usb) return;
  for (; *s; s++) {
    uint8_t key = (uint8_t)*s;
    if (key == '\n') key = KEY_RETURN;
    else if (key == '\b') key = KEY_BACKSPACE;
    queueKey(key);
  }
}

void serviceKeyboard() {
  syncKeyboardState();
  if (!tud_mounted()) {
    if (usbKeyCount || usbKeyDown) {
      Serial.println("\nUSB keyboard: disconnected; pending keys dropped");
      usbKeyHead = usbKeyCount = 0;
      Keyboard.releaseAll();             // clear local pressed state; cross-core events are separate
      usbKeyDown = false;
    }
    return;
  }
  // Preflight readiness so the core's HIDReady() does not wait for the host.
  if (!tud_hid_ready()) return;
  if (usbKeyDown) {
    Keyboard.releaseAll();
    usbKeyDown = false;
  } else if ((usbEpoch & 1) && usbKeyCount &&
             keyboardState.load(std::memory_order_acquire) == usbEpoch) {
    if (Keyboard.press(usbKeys[usbKeyHead])) usbKeyDown = true;
    else Serial.println("\nUSB keyboard: unsupported key dropped");
    usbKeyHead = (usbKeyHead + 1) % USB_KEY_N;
    usbKeyCount--;
  }
}

void sendKey(uint8_t key, const char* label, bool usb) {
  emit(label);
  if (usb) queueKey(key);
}

// ---- commands (arrow keys etc.) ----
// Keyed as <CMD> (-.-.-) followed by one character. Send a USB key only when eligible,
// and keep the readable name on Serial/OLED. Unknown commands send no USB key.
// Note: out[0] of the decoded item is used, so prosigns such as "<AR>" map to "<?>".
void sendCommand(char c, bool usb) {
  switch (c) {
    case 'U': sendKey(KEY_UP_ARROW, "<UP>", usb);      break;
    case 'D': sendKey(KEY_DOWN_ARROW, "<DOWN>", usb);  break;
    case 'L': sendKey(KEY_LEFT_ARROW, "<LEFT>", usb);  break;
    case 'R': sendKey(KEY_RIGHT_ARROW, "<RIGHT>", usb); break;
    case 'H': sendKey(KEY_HOME, "<HOME>", usb);        break;
    case 'E': sendKey(KEY_END, "<END>", usb);          break;
    case 'X': sendKey(KEY_DELETE, "<DEL>", usb);       break;
    case 'T': sendKey(KEY_TAB, "<TAB>", usb);          break;
    case 'N': sendKey(KEY_RETURN, "<ENTER>", usb);     break;
    default:  emit("<?>");     break;
  }
}

// Core 1 decodes and publishes a snapshot; core 0 owns all output operations.
void flushChar() {
  sym[n] = 0;
  auto it = MORSE.find(sym);
  const char* out = (it != MORSE.end()) ? it->second : "?";
  bool special = false;

  if (!strcmp(out, "<SK>")) {
    setKeyboardOnline(false);
    special = true;
    postOutput(out, false, true, false);
  } else {
    if (!(keyboardEpoch & 1)) {
      if (!strcmp(out, "V")) activationVs++;
      else activationVs = 0;
      if (activationVs == 3) {
        setKeyboardOnline(true);
        special = true;
      }
    }
    if (special) {
      postOutput(out, false, true, false);
    } else if (!strcmp(out, "<CMD>")) {
      cmdMode = true;
      special = true;
      postOutput("", false, true, false);
    } else if (cmdMode) {
      cmdMode = false;
      special = true;
      postOutput(out, true, true, true);
    } else {
      postOutput(out, false, true, true);
    }
  }

  n = 0;
  wordPending = !special;          // commands and mode controls have no trailing USB word space
}

void setup() {
  queue_init(&outputQueue, sizeof(OutputEvent), OUTPUT_N);
  Serial.begin(115200);
  Keyboard.begin();
  Serial.println("Keyboard OFFLINE; key VVV to enable");

  Wire.setSDA(OLED_SDA);                  // I2C pins must be chosen before the first use of Wire
  Wire.setSCL(OLED_SCL);
  u8g2.begin();
  u8g2.setBusClock(400000);               // I2C fast mode (a page = ~3 ms). Many SH1106 modules also
                                          // accept 1000000 (~1 ms per page): try it if yours does.
  u8g2.setFont(DISPLAY_FONT);
  u8g2.setFontPosTop();                   // y coordinate of drawStr() = top of the glyphs
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  drawStatus();
  renderTrace(micros());                  // empty trace: just the baselines
  while (dirty) serviceDisplay();         // first paint, nobody is keying yet
  outputReady.store(true, std::memory_order_release);
}

void loop() {
  syncKeyboardState();
  unsigned int dropped = outputDropped.load(std::memory_order_acquire);
  if (dropped != reportedOutputDropped) {
    Serial.print("\nOutput queue full; dropped events: ");
    Serial.println(dropped - reportedOutputDropped);
    reportedOutputDropped = dropped;
    showText("<OVF>");
  }
  OutputEvent event;
  if (queue_try_remove(&outputQueue, &event)) {
    displayDit = event.dit;
    displayError = event.error;
    bool usb = event.usb && (usbEpoch & 1) && event.keyboardEpoch == usbEpoch &&
               keyboardState.load(std::memory_order_acquire) == usbEpoch;
    if (event.command) sendCommand(event.text[0], usb);
    else if (event.text[0]) typeText(event.text, usb);
    drawStatus();
    if (event.trace) {
      displayEdgeCount = event.edgeCount;
      for (int i = 0; i < event.edgeCount; i++) displayEdges[i] = event.edges[i];
      renderTrace(event.t);
    }
  }
  serviceKeyboard();
  serviceDisplay();
}

void setup1() {
  while (!outputReady.load(std::memory_order_acquire)) tight_loop_contents();
  pinMode(KEY, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  if (LED_PIN >= 0) pinMode(LED_PIN, OUTPUT);
  led(false);
  pixel.begin();
  pixel.setBrightness(40);
  pixelOff();
  lastChange = micros();
  offlineBlinkT = lastChange;
  updatePixel(lastChange, true);
}

void loop1() {
  bool s = digitalRead(KEY);              // LOW = key down
  unsigned long now = micros();           // timestamp used for any edge found this pass
  unsigned long d = now - lastChange;     // time since the last accepted edge

  // ---- a key edge ----
  // Debounce = lockout: after an accepted edge, changes are ignored for DEBOUNCE_US.
  // Outside the lockout, edges are timestamped when polled with no extra settling
  // wait. During the lockout, short pulses can be missed and persistent changes deferred.
  // Raise DEBOUNCE_US if a bouncy key produces stray dits.
  if (s != last && d > DEBOUNCE_US) {
    if (s == LOW) {                       // pressed: d = gap since release
      tone(BUZZER, TONE_HZ);              // sound first, slow stuff after
      anchorDown = true;
      if (d > 5 * dit) activationVs = 0;
      if (!haveAnchor || d > PAUSE_DITS * SPEED_MAX_DIT) resetSpeedEvidence();
      if (!haveAnchor || d > PAUSE_DITS * dit) {   // new phrase: lock the phase here
        anchorT = now;
        haveAnchor = true;
        active = true;
        edgeCount = 0;                    // history of the previous phrase is irrelevant
        pushEdge(now);
        updatePixel(now, (keyboardEpoch & 1) != 0);
      } else {
        scoreEdge(now, gapTicks(d));      // judge the gap just ended
      }
    } else {                              // released: d = length of the mark
      noTone(BUZZER);
      anchorDown = false;
      bool dah = d > 2 * dit;             // dit/dah threshold: midpoint of 1 and 3
      scoreEdge(now, dah ? 3 : 1);        // score before the speed adapts
      if (n < 15) sym[n++] = dah ? '-' : '.';
      adaptSpeed(d);
    }
    led(s == LOW);                        // optional external key-down LED
    last = s;
    lastChange = now;
    return;                               // keep the loop short right after an edge
  }

  updatePixel(now, false);               // online beat or offline blue blink, after key edges

  // ---- key up: decoder timeouts only ----
  if (last == HIGH) {                     // key up: gap timeouts
    if (n > 0 && d > 2.5 * dit) flushChar();            // end of character
    else if (wordPending && d > 5 * dit) {              // end of word
      postOutput(" ", false, false, true);
      wordPending = false;
      cmdMode = false;                    // normal word ended; pending commands suppress wordPending
    }
    if (d > 5 * dit) activationVs = 0;
    if (active && d > PAUSE_DITS * dit) { // reference stops; offline blue blink continues
      active = false;
      updatePixel(now, false);
      qErr = Q_GOOD;
    }
  }
}
