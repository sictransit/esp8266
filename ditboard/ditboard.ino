/*
 * DitBoard (CW HID) - straight-key Morse (CW) decoder with a "dit train" keying coach
 * (file: cw_decoder.ino). HID = the planned USB-keyboard output; today commands only echo as text.
 * Board: LOLIN(WEMOS) D1 mini (ESP8266), Arduino core.  Libraries: U8g2, Adafruit NeoPixel.
 * More background, rationale and tuning notes: see README.md in the same folder.
 *
 * WHAT IT DOES
 *   - Reads a straight Morse key (GPIO, active low) and gives immediate feedback:
 *     a 600 Hz sidetone on a passive piezo and the onboard LED while the key is down.
 *   - Decodes the keying into characters: printed on Serial (115200) and on an
 *     SH1106 128x64 OLED (two text rows).
 *   - Judges HOW WELL you key, against a reference beat called the "dit train":
 *       * the NeoPixel blinks in the beat of that reference; its colour (green ->
 *         red) is the smoothed phase error of your keying,
 *       * the OLED shows a trace of your key under the reference after each
 *         character, and a status row with speed (WPM) and quality (Q %).
 *
 * HARDWARE (D1 mini pin -> GPIO)
 *   D7 (13) key to GND (internal pull-up)    D5 (14) passive piezo
 *   D6 (12) NeoPixel DIN                     D1 (5) SCL / D2 (4) SDA  -> OLED (I2C)
 *   D4 (2)  onboard LED (active low, no wiring)
 *
 * HOW IT WORKS - one cooperative loop(), no interrupts, nothing blocks for long
 *   1. read the key and take a microsecond timestamp
 *   2. if the reference is running, blink the NeoPixel in its beat
 *   3. on a debounced key edge: sidetone, score the edge against the reference,
 *      classify the mark as dit/dah and adapt the speed estimate
 *   4. while the key is up: character / word / idle timeouts, and ship at most ONE
 *      8-pixel page of the display buffer to the OLED per pass
 *
 * VOCABULARY AND UNITS (read this before changing anything)
 *   - All times are microseconds from micros(); differences use unsigned arithmetic
 *     so the 71-minute wrap is harmless (signed casts are only used on short spans).
 *   - edge  : a debounced key change. "down" edge = key pressed, "up" edge = released.
 *   - mark  : the time the key is down.  gap: the time it is up between marks.
 *   - dit   : estimated length of one dit in microseconds. WPM = 1,200,000 / dit.
 *   - tick  : one dit of the reference. The reference is a square wave with a period
 *             of 2 ticks. dit = 1 tick, dah = 3 ticks, gaps = 1, 3 or 7 ticks, so two
 *             successive edges are always an ODD number of ticks apart. Down edges
 *             therefore land on even ticks, up edges on odd ticks.
 *   - phase error: how far an edge landed from its expected tick, in dits (+ = late).
 *
 * DESIGN DECISIONS (details and alternatives in README.md)
 *   - Polling loop instead of interrupts: simple, and the cost is bounded. The only
 *     slow operation (an I2C page, ~3 ms) is done one page per loop pass and only
 *     while the key is up, so releases are never delayed.
 *   - Quality is measured against a free-running reference that follows you only
 *     partly (PHASE_GAIN), not against ideal element ratios one at a time. That is
 *     what lets it catch drifting, rushing and dragging.
 *   - Speed adapts from marks only, so unusual spacing shows up as phase error
 *     instead of corrupting the speed estimate.
 *   - Everything the trace needs is recorded per edge (ring buffer), so the picture
 *     shows what the decoder actually used, not a reconstruction.
 *
 * KNOWN LIMITATIONS
 *   - Straight key only (no paddles / iambic keyer). First characters after boot
 *     assume ~15 WPM until the speed estimate has adapted.
 *   - A display page can delay the detection of a key PRESS by up to ~3 ms (the
 *     timestamp is taken after the page is sent). Fine at 15-20 WPM, noticeable
 *     above ~30 WPM. A pin-change interrupt would remove this.
 *   - Tuning constants (PHASE_GAIN, Q_GOOD, Q_BAD, timeouts) are first guesses.
 *   - Commands (see sendCommand) only echo text; real USB HID is future work.
 *
 * ARDUINO IDE GOTCHA
 *   The IDE hoists function prototypes above all other code, so no function may have
 *   a user-defined type (e.g. struct Edge) in its signature. That is why edgeIdx()
 *   returns an int index instead of an Edge.
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>
#include <map>
#include <string>

// SH1106 128x64, I2C, "F" = full frame buffer in RAM (1 KB). Hardware I2C pins on the
// D1 mini: D1 = SCL, D2 = SDA. (An SSD1306 module needs a different constructor.)
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);   // D1 = SCL, D2 = SDA

// ---- pins and fixed settings ----
const int KEY       = D7;    // key to GND, internal pull-up (LOW = pressed). GPIO13: not a boot pin.
const int BUZZER    = D5;    // passive piezo, driven with tone()
const int PIXEL_PIN = D6;    // NeoPixel DIN
const uint16_t TONE_HZ = 600;                 // sidetone frequency
const unsigned long DEBOUNCE_US = 3000;       // lockout after an accepted edge (see loop())

Adafruit_NeoPixel pixel(1, PIXEL_PIN, NEO_GRB + NEO_KHZ800);   // one pixel

// ---- Morse table: dots and dashes -> text ----
// Letters, digits and common punctuation. Prosigns are returned as readable text such
// as "<AR>". Three entries are special:
//   "-...-"    (BT)             -> "\n"   newline
//   "........" (8 dits, error)  -> "\b"   backspace
//   "-.-.-"    (CT / KA)        -> "<CMD>" enters command mode: the NEXT character
//                                           selects a command (see sendCommand()).
// std::map is fine on the ESP8266 (heap is plentiful); it would not be on a small AVR.
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

// ---- decoder state ----
bool last = HIGH;                  // last ACCEPTED key level, HIGH = key up
unsigned long lastChange = 0;      // time of the last accepted edge (us)
float dit = 80000;                 // us; speed estimate, starts at 15 WPM, adapts from marks
char sym[16];                      // dots/dashes of the character being keyed
int n = 0;                         // elements in sym[] (max 15, extra elements are dropped)
bool wordPending = false;          // a character was just decoded: a word space may follow
bool cmdMode = false;              // the previous character was <CMD>

// ---- keying quality: the "dit train" reference ----
// Picture a reference wave with a period of 2 dits. Key-down should happen when it
// goes negative, key-up when it goes positive: a dit ends half a period later (1 tick),
// a dah 1.5 periods later (3 ticks). Gaps are 1, 3 or 7 ticks, so two successive edges
// are always an odd number of ticks apart, and key-down edges land on even ticks,
// key-up edges on odd ticks.
//
// The reference is anchored to the previous edge and is nudged only part of the way
// (PHASE_GAIN) towards each new edge, so keying that wanders against the beat shows
// up as phase error. Speed (dit) adapts to the marks as before.
// The first key-down after a long pause locks the phase.
//
// The NeoPixel blinks in beat with this reference: lit while the wave is negative
// (when the key should be down), dark while it is positive. Its colour shows the
// keying quality. It switches off PAUSE_DITS dits after you stop keying.
const float PHASE_GAIN = 0.25f;    // how far the reference follows each edge: 0 = rigid, 1 = follows fully
const float Q_GOOD = 0.10f;        // smoothed phase error (in dits) at or below: green / Q 100 %
const float Q_BAD  = 0.40f;        // at or above: red / Q 0 %
const float PAUSE_DITS = 10;       // idle this long: reference stops, pixel off, next key-down relocks
unsigned long anchorT = 0;         // reference time of the latest edge (us), already nudged
bool anchorDown = false;           // true if the anchor tick is a key-down (wave going negative)
bool haveAnchor = false;           // the reference has been locked at least once
bool active = false;               // reference running, pixel blinks in its beat
float qErr = Q_GOOD;               // smoothed |phase error| in dits (EMA 0.8/0.2), 0 = perfect
bool pixelOn = false;              // what the pixel currently shows (avoids redundant show())

// 0 = good keying, 1 = bad: qErr mapped onto the Q_GOOD..Q_BAD range.
float qualityT() {                 // 0 = good, 1 = bad
  return constrain((qErr - Q_GOOD) / (Q_BAD - Q_GOOD), 0.0f, 1.0f);
}

void pixelOff() {
  pixel.clear();
  pixel.show();
  pixelOn = false;
}

// Colour is a straight green->red blend (so the middle is a dim olive/yellow).
// Brightness is capped in setup() with setBrightness(40).
void pixelShow() {                 // lit, in the current quality colour
  float t = qualityT();
  pixel.setPixelColor(0, pixel.Color((uint8_t)(255 * t), (uint8_t)(255 * (1 - t)), 0));
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

// Only touches the pixel when its state changes (or force: colour may have changed).
// pixel.show() disables interrupts for a few tens of microseconds, so redundant calls
// are avoided.
void updatePixel(unsigned long now, bool force) {
  bool on = active && gridLit(now);
  if (!on) { if (pixelOn) pixelOff(); }
  else if (!pixelOn || force) pixelShow();
}

// ---- edge history for the trace: where the key really went vs. the reference ----
// One record per edge, kept in a ring buffer. The trace is drawn from these records,
// so it shows what the decoder actually used (including its phase corrections).
struct Edge {
  unsigned long t;                 // when the edge really happened (us)
  unsigned long anchor;            // reference anchor after this edge (us)
  float dit;                       // dit length used by the reference from this anchor
  bool down;                       // true = key-down edge (reference starts "negative")
};
const int EDGE_N = 32;             // more than the ~21 edges that can fit in the 21-dit window
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

// Classify the gap before a key press as 1 (inside a character), 3 (between
// characters) or 7 (between words) ticks. The thresholds are the midpoints.
int gapTicks(unsigned long g) {    // classify a gap as 1, 3 or 7 ticks
  return (g < 2 * dit) ? 1 : (g < 5 * dit) ? 3 : 7;
}

// Score an edge at time t that is expected 'ticks' dits after the previous edge:
//   1. expected time = anchor + ticks * dit
//   2. phase error e = (t - expected) / dit, clipped to +-1 dit
//   3. quality: qErr = exponential moving average of |e|
//   4. phase loop: move the anchor PHASE_GAIN of the way to where the edge really was
//   5. record the edge for the trace and refresh the pixel colour
// Call BEFORE adapting dit, so the edge is judged against the speed that applied to it.
void scoreEdge(unsigned long t, int ticks) {
  unsigned long tick = anchorT + (unsigned long)(long)(ticks * dit);   // expected time
  float e = (float)(long)(t - tick) / dit;                              // phase error in dits, + = late
  e = constrain(e, -1.0f, 1.0f);
  qErr = 0.8f * qErr + 0.2f * fabsf(e);
  anchorT = tick + (unsigned long)(long)(PHASE_GAIN * e * dit);
  pushEdge(t);
  updatePixel(t, true);
}

// ---- display ----
// 128x64 SH1106, full buffer. Layout (pages are 8 px tall):
//   y  0..9    status row
//   y 10..29   two text rows           (these cover pages 0..3)
//   y 32..63   trace = pages 4..7      (redrawn after each decoded character)
// Drawing only fills the buffer and marks pages dirty. serviceDisplay() sends ONE
// page per call, and only while the key is up, so the key waits at most one page
// (~3 ms) and its release is never delayed.
// Invariant: the text area (pages 0..3) and the trace (pages 4..7) never share a
// page, so redrawing one never forces a resend of the other (TRACE_Y is a multiple of 8).
#define DISPLAY_FONT u8g2_font_6x10_tf
const int CHAR_W = 6, ROW_H = 10;  // font cell; change together with DISPLAY_FONT
const int COLS = 128 / CHAR_W;     // 21 characters per text row
const int ROWS = 2;                // text rows below the status row
const int TRACE_Y = 32;            // first pixel row of the trace area

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

// Status row: speed from the dit estimate (1,200,000 / dit_us = WPM) and quality Q %
// (100 % at qErr <= Q_GOOD, 0 % at qErr >= Q_BAD).
void drawStatus() {
  char buf[26];
  int q = (int)(100 * (1.0f - qualityT()) + 0.5f);
  snprintf(buf, sizeof buf, "CW %2d WPM  Q %3d%%", (int)(1200000.0 / dit + 0.5), q);
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

// ---- trace: the keyed wave under the dit train, last ~21 dits ----
// Top lane    = reference: a bar wherever the key SHOULD be down (the dit train).
// Bottom lane = the key:   a bar wherever it really was down.
// Keyed perfectly, every key bar sits exactly under reference bars.
const int PX_PER_DIT = 6;          // horizontal scale: 128 px = ~21 dits at any speed
const int REF_Y = 35, REF_H = 8;   // reference bars; baseline just below
const int KEY_Y = 48, KEY_H = 12;  // key bars; baseline just below

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

  if (edgeCount > 0) {
    float pxDur = dit / PX_PER_DIT;               // us per pixel
    unsigned long tStart = tEnd - (unsigned long)(128 * pxDur);
    int ri = -1, ki = -1;                         // current reference segment / key edge
    for (int x = 0; x < 128; x++) {
      unsigned long T = tStart + (unsigned long)(x * pxDur);
      while (ri + 1 < edgeCount && (long)(T - edges[edgeIdx(ri + 1)].anchor) >= 0) ri++;
      while (ki + 1 < edgeCount && (long)(T - edges[edgeIdx(ki + 1)].t) >= 0) ki++;

      if (ri >= 0) {
        const Edge& r = edges[edgeIdx(ri)];
        float dt = (float)(long)(T - r.anchor);
        if (dt <= PAUSE_DITS * r.dit) {
          long k = (long)(dt / r.dit);
          if (((k + (r.down ? 0 : 1)) & 1) == 0) u8g2.drawVLine(x, REF_Y, REF_H);
        }
      }
      if (ki >= 0 && edges[edgeIdx(ki)].down) u8g2.drawVLine(x, KEY_Y, KEY_H);
    }
  }
  markPages(TRACE_Y, 63);
}

// Output one decoded item: Serial gets the full stream, the OLED the last two rows.
void emit(const char* s) { Serial.print(s); showText(s); }

// ---- commands (arrow keys etc.) ----
// Keyed as <CMD> (-.-.-) followed by one character. PLACEHOLDER: for now this only
// echoes the command name as text. The plan is to replace each emit() with a USB HID
// key press (e.g. Keyboard.write(KEY_UP_ARROW)) on a board with native USB (RP2040).
// Note: out[0] of the decoded item is used, so prosigns such as "<AR>" map to "<?>".
void sendCommand(char c) {
  switch (c) {
    case 'U': emit("<UP>");    break;   // later: Keyboard.write(KEY_UP_ARROW)
    case 'D': emit("<DOWN>");  break;
    case 'L': emit("<LEFT>");  break;
    case 'R': emit("<RIGHT>"); break;
    case 'H': emit("<HOME>");  break;
    case 'E': emit("<END>");   break;
    case 'X': emit("<DEL>");   break;
    case 'T': emit("<TAB>");   break;
    case 'N': emit("<ENTER>"); break;
    default:  emit("<?>");     break;
  }
}

// The gap after a character has lasted 2.5 dits: look the dots/dashes up, emit the
// result (or run a command), then refresh the status row and draw the trace snapshot
// of the character just keyed. An unknown pattern, or one longer than 15 elements,
// shows as '?'.
void flushChar() {
  sym[n] = 0;
  auto it = MORSE.find(sym);
  const char* out = (it != MORSE.end()) ? it->second : "?";
  bool special = false;

  if (!strcmp(out, "<CMD>")) { cmdMode = true; special = true; }
  else if (cmdMode)          { cmdMode = false; sendCommand(out[0]); special = true; }
  else                         emit(out);

  n = 0;
  wordPending = !special;          // commands are not followed by a word space
  drawStatus();
  renderTrace(micros());           // snapshot of the character just keyed
}

void setup() {
  Serial.begin(115200);
  pinMode(KEY, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);        // active low: HIGH = off

  pixel.begin();
  pixel.setBrightness(40);                // 0-255; low on purpose (USB power, eyes)
  pixelOff();

  u8g2.begin();
  u8g2.setBusClock(400000);               // I2C fast mode: shorter pages, less key delay
  u8g2.setFont(DISPLAY_FONT);
  u8g2.setFontPosTop();                   // y coordinate of drawStr() = top of the glyphs
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  drawStatus();
  renderTrace(micros());                  // empty trace: just the baselines
  while (dirty) serviceDisplay();         // first paint, nobody is keying yet
}

void loop() {
  bool s = digitalRead(KEY);              // LOW = key down
  unsigned long now = micros();           // timestamp used for any edge found this pass
  unsigned long d = now - lastChange;     // time since the last accepted edge

  if (active) updatePixel(now, false);    // blink in the beat of the reference

  // ---- a key edge ----
  // Debounce = lockout: after an accepted edge, changes are ignored for DEBOUNCE_US.
  // Edges are timestamped when found (not delayed), so there is no added latency.
  // Raise DEBOUNCE_US if a bouncy key produces stray dits.
  if (s != last && d > DEBOUNCE_US) {
    if (s == LOW) {                       // pressed: d = gap since release
      tone(BUZZER, TONE_HZ);              // sound first, slow stuff after
      anchorDown = true;
      if (!haveAnchor || d > PAUSE_DITS * dit) {   // new phrase: lock the phase here
        anchorT = now;
        haveAnchor = true;
        active = true;
        edgeCount = 0;                    // history of the previous phrase is irrelevant
        pushEdge(now);
        updatePixel(now, true);
      } else {
        scoreEdge(now, gapTicks(d));      // judge the gap just ended
      }
    } else {                              // released: d = length of the mark
      noTone(BUZZER);
      anchorDown = false;
      bool dah = d > 2 * dit;             // dit/dah threshold: midpoint of 1 and 3
      scoreEdge(now, dah ? 3 : 1);        // score before the speed adapts
      if (n < 15) sym[n++] = dah ? '-' : '.';
      // Speed follows the marks only (a dah counts as 3 dits), smoothed 0.8/0.2 and
      // limited to 3..48 WPM (400..25 ms per dit). Gaps are deliberately not used.
      dit = 0.8 * dit + 0.2 * (dah ? d / 3.0 : d);
      dit = constrain(dit, 25000.0, 400000.0);
    }
    digitalWrite(LED_BUILTIN, s);         // onboard LED on while key is down (active low)
    last = s;
    lastChange = now;
    return;                               // keep the loop short right after an edge
  }

  // ---- key up: timeouts and display work ----
  if (last == HIGH) {                     // key up: gap timeouts and display work
    if (n > 0 && d > 2.5 * dit) flushChar();            // end of character
    else if (wordPending && d > 5 * dit) {              // end of word
      emit(" ");
      drawStatus();
      wordPending = false;
      cmdMode = false;                    // a command must follow its <CMD> within the word
    }
    if (active && d > PAUSE_DITS * dit) { // stopped keying: reference stops, pixel off
      active = false;
      updatePixel(now, false);
      qErr = Q_GOOD;
    }
    serviceDisplay();                     // at most one 8 px page per pass
  }
}
