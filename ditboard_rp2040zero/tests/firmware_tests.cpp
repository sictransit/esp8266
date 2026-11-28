#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

thread_local int testCore = 0;
unsigned long testTime = 100000;
bool testKey = true, mounted = true, hidReady = true, sounding = false;
int displayTransfers = 0;
int pixelRgb = 0;
std::string serialText;
std::vector<int> reports;

#define HIGH true
#define LOW false
#define INPUT_PULLUP 2
#define OUTPUT 1
#define U8G2_R0 0
#define NEO_GRB 0
#define NEO_KHZ800 0
#define u8g2_font_6x10_tf 0
#define KEY_UP_ARROW 0xDA
#define KEY_DOWN_ARROW 0xD9
#define KEY_LEFT_ARROW 0xD8
#define KEY_RIGHT_ARROW 0xD7
#define KEY_HOME 0xD2
#define KEY_END 0xD5
#define KEY_DELETE 0xD4
#define KEY_TAB 0xB3
#define KEY_RETURN 0xB0
#define KEY_BACKSPACE 0xB2

template<class T> T constrain(T value, T low, T high) {
  return value < low ? low : value > high ? high : value;
}
unsigned long micros() { return testTime; }
bool digitalRead(int) { assert(testCore == 1); return testKey; }
void pinMode(int, int) { assert(testCore == 1); }
void digitalWrite(int, int) { assert(testCore == 1); }
void tone(int, int) { assert(testCore == 1); sounding = true; }
void noTone(int) { assert(testCore == 1); sounding = false; }
void tight_loop_contents() { std::this_thread::yield(); }

struct {
  void begin(int) { assert(testCore == 0); }
  void print(const char* text) { assert(testCore == 0); serialText += text; }
  void println(const char* text) { print(text); serialText += '\n'; }
  void println(unsigned int value) { print(std::to_string(value).c_str()); serialText += '\n'; }
} Serial;
struct {
  void setSDA(int) { assert(testCore == 0); }
  void setSCL(int) { assert(testCore == 0); }
} Wire;
struct U8G2_SH1106_128X64_NONAME_F_HW_I2C {
  explicit U8G2_SH1106_128X64_NONAME_F_HW_I2C(int) {}
  void begin() { assert(testCore == 0); }
  void setBusClock(int) { assert(testCore == 0); }
  void setFont(int) { assert(testCore == 0); }
  void setFontPosTop() { assert(testCore == 0); }
  void clearBuffer() { assert(testCore == 0); }
  void sendBuffer() { assert(testCore == 0); displayTransfers++; }
  void setDrawColor(int) { assert(testCore == 0); }
  void drawBox(int, int, int, int) { assert(testCore == 0); }
  void drawStr(int, int, const char*) { assert(testCore == 0); }
  void drawHLine(int, int, int) { assert(testCore == 0); }
  void drawVLine(int, int, int) { assert(testCore == 0); }
  void updateDisplayArea(int, int, int, int) { assert(testCore == 0); displayTransfers++; }
};
struct Adafruit_NeoPixel {
  Adafruit_NeoPixel(int, int, int) {}
  void begin() { assert(testCore == 1); }
  void setBrightness(int) { assert(testCore == 1); }
  void clear() { assert(testCore == 1); pixelRgb = 0; }
  void show() { assert(testCore == 1); }
  int Color(int red, int green, int blue) {
    assert(testCore == 1);
    return (red << 16) | (green << 8) | blue;
  }
  void setPixelColor(int, int color) { assert(testCore == 1); pixelRgb = color; }
};
bool tud_mounted() { assert(testCore == 0); return mounted; }
bool tud_hid_ready() { assert(testCore == 0); return hidReady; }
struct {
  void begin() { assert(testCore == 0); }
  int press(uint8_t key) {
    assert(testCore == 0 && mounted && hidReady);
    reports.push_back(key);
    hidReady = false;
    return 1;
  }
  void releaseAll() {
    assert(testCore == 0 && (!mounted || hidReady));
    reports.push_back(0);
    hidReady = false;
  }
} Keyboard;

struct queue_t {
  std::mutex mutex;
  size_t elementSize = 0, capacity = 0;
  std::deque<std::vector<uint8_t>> items;
};
void queue_init(queue_t* queue, size_t size, size_t count) {
  queue->elementSize = size;
  queue->capacity = count;
  queue->items.clear();
}
bool queue_try_add(queue_t* queue, const void* element) {
  std::lock_guard<std::mutex> lock(queue->mutex);
  if (queue->items.size() == queue->capacity) return false;
  const auto* bytes = static_cast<const uint8_t*>(element);
  queue->items.emplace_back(bytes, bytes + queue->elementSize);
  return true;
}
bool queue_try_remove(queue_t* queue, void* element) {
  std::lock_guard<std::mutex> lock(queue->mutex);
  if (queue->items.empty()) return false;
  memcpy(element, queue->items.front().data(), queue->elementSize);
  queue->items.pop_front();
  return true;
}

#include "firmware-under-test.inc"

void resetFixture() {
  testCore = 0;
  testTime = 100000;
  testKey = HIGH;
  mounted = hidReady = true;
  serialText.clear();
  reports.clear();
  queue_init(&outputQueue, sizeof(OutputEvent), OUTPUT_N);
  outputDropped.store(0);
  keyboardState.store(0);
  usbEpoch = 0;
  reportedOutputDropped = 0;
  usbKeyHead = usbKeyCount = 0;
  usbKeyDown = false;
  displayEdgeCount = 0;
  displayDit = 80000;
  displayError = Q_GOOD;
  memset(lines, 0, sizeof lines);
  curLine = col = 0;
  dirty = 0;
  testCore = 1;
  last = HIGH;
  lastChange = testTime;
  dit = 80000;
  n = 0;
  wordPending = cmdMode = false;
  activationVs = 0;
  keyboardEpoch = 0;
  offlineBlinkT = testTime;
  offlineBlueOn = true;
  pixelOn = false;
  pixelRgb = 0;
  haveAnchor = active = false;
  qErr = Q_GOOD;
  edgeCount = edgeHead = 0;
  resetSpeedEvidence();
}

void feedSpeed(float target, int count, bool jitter = false) {
  for (int i = 0; i < count; i++) {
    float previous = dit;
    float factor = jitter ? 1.0f + (i % 5 - 2) * 0.025f : 1.0f;
    adaptSpeed((unsigned long)(target * (i % 2 ? 3 : 1) * factor));
    assert(fabsf(dit - previous) <= previous * 0.03001f);
    assert(dit >= 25000 && dit <= 400000);
  }
}

void decode(const char* pattern) {
  testCore = 1;
  strcpy(sym, pattern);
  n = (int)strlen(pattern);
  flushChar();
}

void drainOutput() {
  testCore = 0;
  while (!outputQueue.items.empty() || usbKeyCount || usbKeyDown) {
    hidReady = true;
    loop();
  }
}

void resetOnlineFixture() {
  resetFixture();
  decode("...-");
  decode("...-");
  decode("...-");
  drainOutput();
  assert((keyboardEpoch & 1) && reports.empty());
  serialText.clear();
  memset(lines, 0, sizeof lines);
  curLine = col = 0;
  testCore = 1;
}

void keyCharacter(const char* pattern, unsigned long unit) {
  testCore = 1;
  for (const char* p = pattern; *p; p++) {
    testTime += unit;
    testKey = LOW;
    loop1();
    assert(sounding);
    testTime += unit * (*p == '-' ? 3 : 1);
    testKey = HIGH;
    loop1();
    assert(!sounding);
  }
  testTime += 3 * unit;
  loop1();
}

void testSpeed() {
  resetFixture();
  feedSpeed(120000, 23);
  assert(dit == 80000); // three confirmations must use fresh windows
  feedSpeed(120000, 1);
  assert(dit > 80000);
  feedSpeed(120000, 120);
  assert(fabsf(dit - 120000) < 100);

  for (float target : {60000.0f, 200000.0f, 25000.0f, 400000.0f}) {
    resetFixture();
    qErr = 1; // a genuine speed change must not be gated by poor quality
    feedSpeed(target, 160);
    assert(fabsf(dit - target) / target < 0.01f);
  }
  resetFixture();
  feedSpeed(60000, 160, true);
  assert(fabsf(dit - 60000) / 60000 < 0.025f);

  resetFixture();
  feedSpeed(120000, 30);
  float beforeOutlier = dit;
  adaptSpeed(1000000);
  assert(dit == beforeOutlier);

  resetFixture();
  feedSpeed(80000, 32);
  adaptSpeed(1000000);
  assert(dit == 80000);
  for (int i = 0; i < 100; i++) adaptSpeed(5000);
  assert(dit == 80000);
  for (int i = 0; i < 100; i++) adaptSpeed(1000000);
  assert(dit == 80000); // ambiguous single-length marks cannot set a new speed

  for (uint32_t seed = 1; seed <= 6; seed++) {
    resetFixture();
    uint32_t random = seed;
    for (int i = 0; i < 5000; i++) {
      random = random * 1664525u + 1013904223u;
      adaptSpeed(4000 + random % 1200000);
    }
    assert(fabsf(dit - 80000) / 80000 < 0.05f);
  }

  resetFixture();
  feedSpeed(120000, 9);
  testTime += 5000000;
  testKey = LOW;
  haveAnchor = true;
  loop1();
  assert(speedCount == 0 && speedConfirm == 0 && dit == 80000);

  resetFixture();
  for (int i = 0; i < 80; i++) keyCharacter(".-", 400000);
  assert(fabsf(dit - 400000) / 400000 < 0.025f);
}

void testCoreIsolationAndSnapshots() {
  resetOnlineFixture();
  int transfers = displayTransfers;
  for (int i = 0; i < 20; i++) keyCharacter(".-", 80000);
  assert(displayTransfers == transfers && serialText.empty() && reports.empty());
  assert(outputDropped.load() == 12 && outputQueue.items.size() == OUTPUT_N);
  assert(dit == 80000 && n == 0);
  unsigned long latestKeyTime = lastChange;
  drainOutput();
  assert(serialText.find("dropped events: 12") != std::string::npos);
  int presses = 0;
  for (int key : reports) if (key) { assert(key == 'A'); presses++; }
  assert(presses == OUTPUT_N);
  assert(displayEdgeCount == EDGE_N && displayEdges[EDGE_N - 1].t < latestKeyTime);
  for (int i = 1; i < displayEdgeCount; i++) assert(displayEdges[i - 1].t < displayEdges[i].t);

  resetOnlineFixture();
  edges[0] = {123456, 123450, 80000, true};
  edgeCount = 1;
  edgeHead = 1;
  postOutput("A", false, true, true);
  edges[0].t = 999999;
  dit = 120000;
  qErr = 1;
  testTime += 2000000;
  drainOutput();
  assert(displayEdges[0].t == 123456 && displayDit == 80000 && displayError == Q_GOOD);
}

void testUsbRouting() {
  const char* patterns[] = {"..-", "-..", ".-..", ".-.", "....", ".", "-..-", "-", "-."};
  const int keys[] = {KEY_UP_ARROW, KEY_DOWN_ARROW, KEY_LEFT_ARROW, KEY_RIGHT_ARROW,
                     KEY_HOME, KEY_END, KEY_DELETE, KEY_TAB, KEY_RETURN};
  for (int i = 0; i < 9; i++) {
    resetOnlineFixture();
    decode("-.-.-");
    decode(patterns[i]);
    assert(reports.empty());
    drainOutput();
    assert((reports == std::vector<int>{keys[i], 0}));
    assert(!wordPending && !cmdMode);
  }
  resetOnlineFixture();
  decode("-.-.-");
  decode(".-");
  drainOutput();
  assert(reports.empty() && serialText == "<?>");

  resetOnlineFixture();
  decode(".-");
  postOutput(" ", false, false, true);
  decode("-...-");
  decode("........");
  decode(".-.-.");
  drainOutput();
  assert((reports == std::vector<int>{'A',0,' ',0,KEY_RETURN,0,KEY_BACKSPACE,0,
                                     '<',0,'A',0,'R',0,'>',0}));

  resetOnlineFixture();
  decode(".-");
  testCore = 0;
  hidReady = false;
  loop();
  assert(reports.empty() && usbKeyCount == 1);
  hidReady = true;
  loop();
  assert(usbKeyDown);
  mounted = false;
  loop();
  assert(!usbKeyDown && usbKeyCount == 0);
  mounted = hidReady = true;
  loop();
  assert((reports == std::vector<int>{'A',0}));
}

void testOfflineControls() {
  resetFixture();
  keyCharacter("...-", 80000);
  keyCharacter("...-", 80000);
  keyCharacter("...-", 80000);
  assert(keyboardEpoch & 1);
  drainOutput();
  assert(reports.empty());
  keyCharacter(".-", 80000);
  drainOutput();
  assert((reports == std::vector<int>{'A',0}));
  keyCharacter("...-.-", 80000);
  drainOutput();
  assert(!(keyboardEpoch & 1) && pixelRgb == 255);
  assert((reports == std::vector<int>{'A',0}));

  resetFixture();
  decode(".-");
  decode("-...-");
  decode("........");
  decode("-.-.-");
  decode("..-");
  drainOutput();
  assert(!(keyboardEpoch & 1) && reports.empty() && usbKeyCount == 0);
  assert(serialText.find("A\n\b<UP>") != std::string::npos);

  resetFixture();
  decode("...-");
  decode("...-");
  assert(!(keyboardEpoch & 1) && activationVs == 2);
  testTime += (unsigned long)(6 * dit);
  testCore = 1;
  loop1();
  decode("...-");
  assert(!(keyboardEpoch & 1) && activationVs == 1);
  decode(".-");
  assert(activationVs == 0);
  decode("...-");
  decode("...-");
  decode("...-");
  assert((keyboardEpoch & 1) && !wordPending && !cmdMode);
  // Drain old offline events only after activation: none may leak into USB.
  drainOutput();
  assert(reports.empty() && usbKeyCount == 0);
  testCore = 1;
  testTime += (unsigned long)(6 * dit);
  loop1();
  drainOutput();
  assert(reports.empty()); // no leading word space from the activation signal
  decode("...-");
  decode("...-");
  decode("...-");
  drainOutput();
  assert((reports == std::vector<int>{'V',0,'V',0,'V',0}));

  decode("-.-.-");
  decode("...-.-"); // SK takes precedence even when command mode is pending
  decode(".-");
  drainOutput();
  assert(!(keyboardEpoch & 1) && !cmdMode);
  assert((reports == std::vector<int>{'V',0,'V',0,'V',0}));
  assert(serialText.find("<SK>A") != std::string::npos);

  resetFixture();
  decode("...-");
  decode("...-");
  testCore = 1;
  testTime += (unsigned long)(6 * dit);
  testKey = LOW;
  loop1(); // a word gap also resets the prefix if no key-up pass observed it
  assert(activationVs == 0 && sounding);
}

void testOfflineBlink() {
  resetFixture();
  updatePixel(testTime, true);
  assert(pixelRgb == 255);
  testTime = offlineBlinkT + 999999;
  loop1();
  assert(pixelRgb == 255);
  testTime++;
  loop1();
  assert(pixelRgb == 0);
  testTime += 1000000;
  loop1();
  assert(pixelRgb == 255);

  resetFixture();
  testTime = ~0UL - 500000;
  lastChange = offlineBlinkT = testTime;
  updatePixel(testTime, true);
  testTime += 999999; // crosses the unsigned micros() wrap
  loop1();
  assert(pixelRgb == 255);
  testTime++;
  loop1();
  assert(pixelRgb == 0);
  testTime += 1000000;
  loop1();
  assert(pixelRgb == 255);

  // Keying/poor quality cannot replace the blue indicator while offline.
  qErr = 1;
  active = true;
  anchorT = testTime;
  anchorDown = true;
  updatePixel(testTime, true);
  assert(pixelRgb == 255);
  decode("...-");
  decode("...-");
  decode("...-");
  assert(pixelRgb == 0xFF0000);
  decode("...-.-");
  assert(pixelRgb == 255);
  testTime += 11000000; // still blinks after the reference's idle timeout
  loop1();
  assert(!active && pixelRgb == 0);
  testTime += 1000000;
  loop1();
  assert(pixelRgb == 255);
}

void testOfflineBacklog() {
  resetOnlineFixture();
  testCore = 0;
  queueKey('X');
  serviceKeyboard();
  assert(usbKeyDown);
  queueKey('Y');
  testCore = 1;
  for (int i = 0; i < OUTPUT_N; i++) postOutput("A", false, false, true);
  decode("...-.-"); // shutdown must work even if its output event is dropped
  assert(!(keyboardState.load() & 1));
  testCore = 0;
  hidReady = false;
  loop();
  assert(usbKeyCount == 0 && usbKeyDown);
  assert((reports == std::vector<int>{'X'}));
  hidReady = true;
  loop();
  assert((reports == std::vector<int>{'X',0}));
  drainOutput();
  assert((reports == std::vector<int>{'X',0}));

  resetOnlineFixture();
  testCore = 0;
  queueKey('Y');
  testCore = 1;
  for (int i = 0; i < OUTPUT_N; i++) postOutput("A", false, false, true);
  decode("...-.-");
  decode("...-");
  decode("...-");
  decode("...-");
  assert(keyboardEpoch == 3 && outputDropped.load() == 4);
  drainOutput(); // core 0 sees online both times, but the session changed
  assert(reports.empty() && usbKeyCount == 0);
  decode("-...");
  drainOutput();
  assert((reports == std::vector<int>{'B',0}));
}

int main() {
  outputReady.store(false);
  std::thread timingSetup([] { testCore = 1; setup1(); });
  setup();
  timingSetup.join();
  assert(outputReady.load());
  assert(keyboardEpoch == 0 && reports.empty() && pixelRgb == 255);
  testSpeed();
  testCoreIsolationAndSnapshots();
  testUsbRouting();
  testOfflineControls();
  testOfflineBlink();
  testOfflineBacklog();
  std::cout << "PASS: speed confidence/outliers/random tapping/3-48 WPM changes, "
               "core isolation, snapshots, USB routing, offline controls/blink/backlog\n";
}
