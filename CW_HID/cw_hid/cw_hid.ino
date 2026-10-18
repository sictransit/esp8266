// CW key -> built-in LED + 600 Hz sidetone on passive piezo
// Board: LOLIN(WEMOS) D1 mini (ESP8266)

const uint8_t KEY_PIN    = D2;   // GPIO4, key to GND, internal pull-up
const uint8_t BUZZER_PIN = D5;   // GPIO14, passive piezo
const uint8_t LED_PIN    = LED_BUILTIN; // GPIO2, active-low
const uint16_t TONE_HZ   = 600;

const unsigned long DEBOUNCE_MS = 5;

bool keyDown = false;            // debounced state
bool lastRaw = false;
unsigned long lastChange = 0;

void setKey(bool down) {
  digitalWrite(LED_PIN, down ? LOW : HIGH);  // active-low LED
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