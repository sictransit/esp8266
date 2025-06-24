#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#define APSSID "mosquito"
#define APPSK "myggizzz!"

#define BUZZER 4
#define LDR A0

const char *ssid = APSSID;
const char *password = APPSK;

const int MAX_LIGHT = 1023;
const int BASE_FREQ = 520;      // Base frequency for mosquito sound
const int FREQ_VARIATION = 60;  // Frequency variation for mosquito sound

enum Mode {
  DEAD = -1,
  SLEEPING = 0,
  LURKING = 1,
  HUNTING = 2
};

Mode mode = DEAD;  // Use enum for mode
int light = 0;
int sensitivity = MAX_LIGHT / 2;

AsyncWebServer server(80);

void setup() {
  Serial.println("Starting setup...");
  delay(1000);

  Serial.begin(115200);

  Serial.println("Init FS ...");
  initFS();

  Serial.println("Init WS ...");
  initWS();

  Serial.println("Init AP ...");
  initAP();

  pinMode(BUZZER, OUTPUT);
  analogWriteFreq(320);

  Serial.println("Setup done!");

  // Set initial mode
  mode = SLEEPING;
}

void initWS() {
  server.serveStatic("/", LittleFS, "/");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/mode", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(mode));
  });

  server.on("/light", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(light * 100 / MAX_LIGHT));
  });

  server.on("/sensitivity", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(sensitivity * 100 / MAX_LIGHT));
  });

  server.on("/sensitivity", HTTP_PUT, [](AsyncWebServerRequest *request) {
    if (request->hasParam("value")) {
      sensitivity = (request->getParam("value")->value().toInt()) * MAX_LIGHT / 100;
    }
    request->send(200, "text/plain", String(sensitivity));
  });

  server.begin();

  Serial.println("HTTP server started");
}

void initAP() {
  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();

  Serial.print("AP IP address: ");
  Serial.println(myIP);
}

void initFS() {
  if (!LittleFS.begin()) {
    Serial.println("FS error!");
  } else {
    Serial.println("FS done!");
  }
}

void loop() {
  unsigned long now = millis();
  static unsigned long lastRun = 0;

  if (now - lastRun >= 25 && mode != DEAD) {  // run every 25 ms if not dead
    lastRun = now;
    handle_mode(now);
  }
}

void handle_mode(unsigned long now) {
  static unsigned long lurkStart = 0;
  static unsigned long lurkDuration = 0;

  light = analogRead(LDR);

  Mode currentMode = mode;

  if (light > sensitivity) {
    if (mode == SLEEPING) {
      mode = LURKING;
      lurkStart = now;
      lurkDuration = random(5000, 10000);  // lurk for 5 to 10 seconds
    } else if (mode == LURKING && now - lurkStart >= lurkDuration) {
      mode = HUNTING;  // hunting
    }
  } else {
    mode = SLEEPING;  // sleeping
  }

  if (mode == HUNTING) {
    mosquito_sound();
  } else if (mode == SLEEPING && currentMode == HUNTING) {
    analogWrite(BUZZER, 0);
  }
}

void mosquito_sound() {
  static unsigned long lastFreqChange = 0;
  static unsigned long lastAmpChange = 0;
  static int freq = BASE_FREQ;
  static int direction = 1;
  static int amplitude = 100;
  static int ampDir = 1;
  unsigned long now = millis();
  static unsigned long ampInterval = 25;     // Start with 25 ms
  static unsigned long freqInterval = 2000;  // Start with 2000 ms

  // Slowly drift frequency every 2 seconds
  if (now - lastFreqChange >= freqInterval) {
    lastFreqChange = now;
    freq += direction;
    if (freq >= BASE_FREQ + FREQ_VARIATION) direction = -1;
    if (freq <= BASE_FREQ) direction = 1;
    analogWriteFreq(freq);
    freqInterval = random(1800, 2200);
  }

  // Slowly drift amplitude every 100 ms
  if (now - lastAmpChange >= ampInterval) {
    lastAmpChange = now;
    amplitude += ampDir;
    if (amplitude >= 512) ampDir = -1;
    if (amplitude <= 100) ampDir = 1;
    ampInterval = random(15, 25);
  }

  analogWrite(BUZZER, amplitude);
}