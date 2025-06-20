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

int light = 0;
int mode = 0;  // 0: sleeping; 1: lurking; 2: hunting
int sensitivity = 128;

AsyncWebServer server(80);

void setup() {
  delay(1000);

  Serial.begin(115200);
  Serial.println("Setup ...");

  initFS();
  initWS();
  initAP();

  pinMode(BUZZER, OUTPUT);
  analogWriteFreq(320);

  Serial.println("Setup done!");
}

void initWS() {
  Serial.println("Init WS ...");

  server.serveStatic("/", LittleFS, "/");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/mode", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(mode));
  });

  server.on("/light", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(light));
  });

  server.on("/sensitivity", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(sensitivity));
  });

  server.on("/sensitivity", HTTP_PUT, [](AsyncWebServerRequest *request) {
    if (request->hasParam("value")) {
      sensitivity = request->getParam("value")->value().toInt();
    }
    request->send(200, "text/plain", String(sensitivity));
  });

  server.begin();

  Serial.println("HTTP server started");

  Serial.println("WS done!");
}

void initAP() {
  Serial.println("Init AP ...");

  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();

  Serial.print("AP IP address: ");
  Serial.println(myIP);

  Serial.println("AP done!");
}

void initFS() {
  Serial.println("Init FS ...");

  if (!LittleFS.begin()) {
    Serial.println("FS error!");
  } else {
    Serial.println("FS done!");
  }
}

void loop() {
  mosquito();
}

void mosquito() {
  static unsigned long lastRun = 0;
  static unsigned long lurkStart = 0;
  static unsigned long lurkDuration = 0;

  unsigned long now = millis();

  if (now - lastRun >= 25) {
    lastRun = now;

    light = analogRead(LDR);

    int currentMode = mode;

    if (light > sensitivity) {
      if (mode == 0) {
        mode = 1;
        lurkStart = now;
        lurkDuration = random(5000, 10000);  // lurk for 5 to 10 seconds
      } else if (mode == 1 && now - lurkStart >= lurkDuration) {
        mode = 2;  // hunting
      }
    } else {
      mode = 0;  // sleeping
    }

    if (mode == 2) {
      mosquito_sound();
    } else {
      analogWrite(BUZZER, 0);
    }
  }
}

void mosquito_sound() {
  static unsigned long lastFreqChange = 0;
  static unsigned long lastAmpChange = 0;
  static int freq = 520;
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
    if (freq >= 580) direction = -1;
    if (freq <= 520) direction = 1;
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