// CW key -> built-in LED + NeoPixel + 600 Hz sidetone + SH1106 OLED
// Board: LOLIN(WEMOS) D1 mini (ESP8266)

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>

const uint8_t KEY_PIN    = D7;   // GPIO13, key to GND, internal pull-up
const uint8_t BUZZER_PIN = D5;   // GPIO14, passive piezo
const uint8_t PIXEL_PIN  = D6;   // GPIO12, NeoPixel DIN
const uint8_t LED_PIN    = LED_BUILTIN; // GPIO2, active-low
const uint16_t TONE_HZ   = 600;

const uint8_t NUM_PIXELS = 1;
const uint8_t BRIGHTNESS = 40;

const unsigned long DEBOUNCE_MS = 5;

Adafruit_NeoPixel pixel(NUM_PIXELS, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);  // D1=SCL, D2=SDA

bool keyDown = false;            // debounced state
bool lastRaw = false;
unsigned long lastChange = 0;    // raw edge time (debounce)
unsigned long stateStart = 0;    // when the current debounced state began
unsigned long lastDownMs = 0;    // duration of the last key-down
unsigned long lastUpMs   = 0;    // duration of the last key-up

void showPixel(bool down) {
  uint32_t color = down ? pixel.Color(0, 255, 0) : pixel.Color(0, 0, 0);
  for (uint8_t i = 0; i < NUM_PIXELS; i++) pixel.setPixelColor(i, color);
  pixel.show();
}

void drawDisplay() {
  char line[24];
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_ncenB14_tf);
  u8g2.drawStr(0, 22, keyDown ? "KEY DOWN" : "KEY UP");

  u8g2.setFont(u8g2_font_6x12_tf);
  snprintf(line, sizeof(line), "Last down: %lu ms", lastDownMs);
  u8g2.drawStr(0, 44, line);
  snprintf(line, sizeof(line), "Last up:   %lu ms", lastUpMs);
  u8g2.drawStr(0, 58, line);

  u8g2.sendBuffer();
}

void setKey(bool down) {
  digitalWrite(LED_PIN, down ? LOW : HIGH);  // active-low LED
  if (down) {
    tone(BUZZER_PIN, TONE_HZ);
  } else {
    noTone(BUZZER_PIN);
  }
  showPixel(down);
}

void setup() {
  Serial.begin(115200);
  pinMode(KEY_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  pixel.begin();
  pixel.setBrightness(BRIGHTNESS);

  u8g2.begin();
  u8g2.setBusClock(400000);      // faster redraws, less time blocked

  stateStart = millis();
  setKey(false);
  drawDisplay();
}

void loop() {
  bool raw = (digitalRead(KEY_PIN) == LOW);   // LOW = key pressed

  if (raw != lastRaw) {
    lastRaw = raw;
    lastChange = millis();
  }

  if ((millis() - lastChange) >= DEBOUNCE_MS && raw != keyDown) {
    unsigned long now = lastChange;           // timestamp of the actual edge
    unsigned long dur = now - stateStart;
    if (keyDown) lastDownMs = dur; else lastUpMs = dur;
    stateStart = now;

    keyDown = raw;
    setKey(keyDown);                          // sound and lights first
    drawDisplay();                            // then the slow part
    Serial.println(keyDown ? "DOWN" : "UP");
  }
}