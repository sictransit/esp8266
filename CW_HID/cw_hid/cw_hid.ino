// CW key -> built-in LED + NeoPixel + 600 Hz sidetone on passive piezo
// Board: LOLIN(WEMOS) D1 mini (ESP8266)

#include <Adafruit_NeoPixel.h>

const uint8_t KEY_PIN    = D2;   // GPIO4, key to GND, internal pull-up
const uint8_t BUZZER_PIN = D5;   // GPIO14, passive piezo
const uint8_t PIXEL_PIN  = D6;   // GPIO12, NeoPixel DIN
const uint8_t LED_PIN    = LED_BUILTIN; // GPIO2, active-low
const uint16_t TONE_HZ   = 600;

const uint8_t NUM_PIXELS = 1;
const uint8_t BRIGHTNESS = 40;   // 0-255, keep low

const unsigned long DEBOUNCE_MS = 5;

Adafruit_NeoPixel pixel(NUM_PIXELS, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

bool keyDown = false;            // debounced state
bool lastRaw = false;
unsigned long lastChange = 0;

void showPixel(bool down) {
  uint32_t color = down ? pixel.Color(0, 255, 0) : pixel.Color(0, 0, 0);  // green when down
  for (uint8_t i = 0; i < NUM_PIXELS; i++) pixel.setPixelColor(i, color);
  pixel.show();
}

void setKey(bool down) {
  digitalWrite(LED_PIN, down ? LOW : HIGH);  // active-low LED
  showPixel(down);
  if (down) {
    tone(BUZZER_PIN, TONE_HZ);
  } else {
    noTone(BUZZER_PIN);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(KEY_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  pixel.begin();
  pixel.setBrightness(BRIGHTNESS);
  setKey(false);
}

void loop() {
  bool raw = (digitalRead(KEY_PIN) == LOW);   // LOW = key pressed

  if (raw != lastRaw) {
    lastRaw = raw;
    lastChange = millis();
  }

  if ((millis() - lastChange) >= DEBOUNCE_MS && raw != keyDown) {
    keyDown = raw;
    setKey(keyDown);
    Serial.println(keyDown ? "DOWN" : "UP");
  }
}