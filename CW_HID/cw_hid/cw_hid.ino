// CW decoder: key -> sidetone + onboard LED
//   + decoded text and a keyed-wave-vs-dit-train snapshot on an SH1106 OLED
//   + NeoPixel that blinks in the beat of the dit train and shows keying quality
// Board: LOLIN(WEMOS) D1 mini (ESP8266)

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>
#include <map>
#include <string>

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);   // D1 = SCL, D2 = SDA

const int KEY       = D7;    // key to GND, internal pull-up
const int BUZZER    = D5;    // passive piezo
const int PIXEL_PIN = D6;    // NeoPixel DIN
const uint16_t TONE_HZ = 600;
const unsigned long DEBOUNCE_US = 3000;

Adafruit_NeoPixel pixel(1, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

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
bool last = HIGH;                  // HIGH = key up
unsigned long lastChange = 0;
float dit = 80000;                 // us, follows the keying speed (from the marks)
char sym[16];
int n = 0;
bool wordPending = false;
bool cmdMode = false;

// ---- keying quality: "dit train" reference ----
// Picture a reference wave with a period of 2 dits. Key-down should happen when
// it goes negative, key-up when it goes positive: a dit ends half a period
// later (1 tick), a dah 1.5 periods later (3 ticks). Gaps are 1, 3 or 7 ticks,
// so two successive edges are always an odd number of ticks apart, and key-down
// edges land on even ticks, key-up edges on odd ticks.
//
// The reference is anchored to the previous edge and is nudged only part of the
// way (PHASE_GAIN) towards each new edge, so keying that wanders against the
// beat shows up as phase error. Speed (dit) adapts to the marks as before.
// The first key-down after a long pause locks the phase.
//
// The NeoPixel blinks in beat with this reference: lit while the wave is
// negative (when the key should be down), dark while it is positive. Its colour
// shows the keying quality. It switches off PAUSE_DITS dits after you stop keying.
const float PHASE_GAIN = 0.25f;    // 0 = rigid reference, 1 = follows every edge
const float Q_GOOD = 0.10f;        // smoothed phase error (in dits) at or below: green
const float Q_BAD  = 0.40f;        // at or above: red
const float PAUSE_DITS = 10;       // idle this long: reference stops, pixel off, next key-down relocks
unsigned long anchorT = 0;         // reference time of the latest edge (us)
bool anchorDown = false;           // true if the anchor tick is a key-down (wave going negative)
bool haveAnchor = false;
bool active = false;               // reference running, pixel blinks in its beat
float qErr = Q_GOOD;
bool pixelOn = false;

float qualityT() {                 // 0 = good, 1 = bad
  return constrain((qErr - Q_GOOD) / (Q_BAD - Q_GOOD), 0.0f, 1.0f);
}

void pixelOff() {
  pixel.clear();
  pixel.show();
  pixelOn = false;
}

void pixelShow() {                 // lit, in the current quality colour
  float t = qualityT();
  pixel.setPixelColor(0, pixel.Color((uint8_t)(255 * t), (uint8_t)(255 * (1 - t)), 0));
  pixel.show();
  pixelOn = true;
}

// The reference wave: lit while "negative" (key should be down), dark while "positive".
bool gridLit(unsigned long now) {
  long k = (long)floorf((float)(long)(now - anchorT) / dit);   // ticks since the anchor
  if (k < 0) k = 0;                // anchor nudged slightly ahead of now: still its first tick
  return (((k + (anchorDown ? 0 : 1)) & 1) == 0);
}

// Only touches the pixel when its state changes (or force: colour may have changed).
void updatePixel(unsigned long now, bool force) {
  bool on = active && gridLit(now);
  if (!on) { if (pixelOn) pixelOff(); }
  else if (!pixelOn || force) pixelShow();
}

// ---- edge history for the trace: where the key really went vs. the reference ----
struct Edge {
  unsigned long t;                 // when the edge really happened (us)
  unsigned long anchor;            // reference anchor after this edge (us)
  float dit;                       // dit length used by the reference from this anchor
  bool down;                       // true = key-down edge (reference starts "negative")
};
const int EDGE_N = 32;
Edge edges[EDGE_N];
int edgeHead = 0, edgeCount = 0;

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

int gapTicks(unsigned long g) {    // classify a gap as 1, 3 or 7 ticks
  return (g < 2 * dit) ? 1 : (g < 5 * dit) ? 3 : 7;
}

// Score an edge at time t that is expected 'ticks' dits after the previous edge.
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
#define DISPLAY_FONT u8g2_font_6x10_tf
const int CHAR_W = 6, ROW_H = 10;
const int COLS = 128 / CHAR_W;     // 21
const int ROWS = 2;                // text rows below the status row
const int TRACE_Y = 32;

char lines[ROWS][COLS + 1];
int curLine = 0, col = 0;
uint8_t dirty = 0;                 // bit n set = page n needs sending

void markPages(int y0, int y1) {
  for (int p = y0 / 8; p <= y1 / 8 && p < 8; p++) dirty |= (1 << p);
}

void markRows(int firstRow, int lastRow) {          // display rows, 0 = status row
  markPages(firstRow * ROW_H, lastRow * ROW_H + ROW_H - 1);
}

void serviceDisplay() {            // send one dirty page
  if (!dirty) return;
  int p = 0;
  while (!(dirty & (1 << p))) p++;
  dirty &= ~(1 << p);
  u8g2.updateDisplayArea(0, p, 16, 1);
}

void drawStatus() {
  char buf[26];
  int q = (int)(100 * (1.0f - qualityT()) + 0.5f);
  snprintf(buf, sizeof buf, "CW %2d WPM  Q %3d%%", (int)(1200000.0 / dit + 0.5), q);
  u8g2.setDrawColor(0); u8g2.drawBox(0, 0, 128, ROW_H); u8g2.setDrawColor(1);
  u8g2.drawStr(0, 0, buf);
  markRows(0, 0);
}

void drawRow(int i) {
  u8g2.setDrawColor(0); u8g2.drawBox(0, (i + 1) * ROW_H, 128, ROW_H); u8g2.setDrawColor(1);
  u8g2.drawStr(0, (i + 1) * ROW_H, lines[i]);
}

void newLine(bool &scrolled) {
  if (curLine < ROWS - 1) curLine++;
  else {
    memmove(lines[0], lines[1], (ROWS - 1) * (COLS + 1));
    scrolled = true;
  }
  lines[curLine][0] = 0;
  col = 0;
}

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
const int PX_PER_DIT = 6;
const int REF_Y = 35, REF_H = 8;   // reference bars; baseline just below
const int KEY_Y = 48, KEY_H = 12;  // key bars; baseline just below

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

void emit(const char* s) { Serial.print(s); showText(s); }

// ---- commands (arrow keys etc.) ----
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

void flushChar() {
  sym[n] = 0;
  auto it = MORSE.find(sym);
  const char* out = (it != MORSE.end()) ? it->second : "?";
  bool special = false;

  if (!strcmp(out, "<CMD>")) { cmdMode = true; special = true; }
  else if (cmdMode)          { cmdMode = false; sendCommand(out[0]); special = true; }
  else                         emit(out);

  n = 0;
  wordPending = !special;
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
  pixel.setBrightness(40);
  pixelOff();

  u8g2.begin();
  u8g2.setBusClock(400000);
  u8g2.setFont(DISPLAY_FONT);
  u8g2.setFontPosTop();
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  drawStatus();
  renderTrace(micros());                  // empty trace: just the baselines
  while (dirty) serviceDisplay();         // first paint, nobody is keying yet
}

void loop() {
  bool s = digitalRead(KEY);
  unsigned long now = micros();
  unsigned long d = now - lastChange;

  if (active) updatePixel(now, false);    // blink in the beat of the reference

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
        scoreEdge(now, gapTicks(d));
      }
    } else {                              // released: d = length of the mark
      noTone(BUZZER);
      anchorDown = false;
      bool dah = d > 2 * dit;
      scoreEdge(now, dah ? 3 : 1);        // score before the speed adapts
      if (n < 15) sym[n++] = dah ? '-' : '.';
      dit = 0.8 * dit + 0.2 * (dah ? d / 3.0 : d);
      dit = constrain(dit, 25000.0, 400000.0);
    }
    digitalWrite(LED_BUILTIN, s);         // onboard LED on while key is down (active low)
    last = s;
    lastChange = now;
    return;
  }

  if (last == HIGH) {                     // key up: gap timeouts and display work
    if (n > 0 && d > 2.5 * dit) flushChar();
    else if (wordPending && d > 5 * dit) {
      emit(" ");
      drawStatus();
      wordPending = false;
      cmdMode = false;
    }
    if (active && d > PAUSE_DITS * dit) { // stopped keying: reference stops, pixel off
      active = false;
      updatePixel(now, false);
      qErr = Q_GOOD;
    }
    serviceDisplay();                     // at most one 8 px page per pass
  }
}
