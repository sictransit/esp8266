#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#ifndef APSSID
#define APSSID "mosquito"
#define APPSK "myggizzz!"
#endif

#define LED 4
#define AIN A0

const char *ssid = APSSID;
const char *password = APPSK;

int light = 0;
int mode = 0; // 0: sleeping; 1: hunting
int threshold = 128;

AsyncWebServer server(80);

void setup()
{
  delay(1000);

  Serial.begin(115200);
  Serial.println("Setup ...");

  initFS();
  initWS();
  initAP();

  pinMode(LED, OUTPUT);
  analogWriteFreq(320);

  Serial.println("Setup done!");
}

void initWS()
{
  Serial.println("Init WS ...");

  server.serveStatic("/", LittleFS, "/");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/index.html", "text/html"); });

  server.on("/mode", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", String(mode)); });

  server.on("/light", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", String(light)); });

  server.on("/threshold", HTTP_GET, [](AsyncWebServerRequest *request)
            { 
              if (request->hasParam("value")) {
          threshold = request->getParam("value")->value().toInt();
      }
              request->send(200, "text/plain", String(threshold)); });



  server.begin();
  Serial.println("HTTP server started");

  Serial.println("WS done!");
}

void initAP()
{
  Serial.println("Init AP ...");

  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();

  Serial.print("AP IP address: ");
  Serial.println(myIP);

  Serial.println("AP done!");
}

void initFS()
{
  Serial.println("Init FS ...");

  if (!LittleFS.begin())
  {
    Serial.println("FS error!");
  }
  else
  {
    Serial.println("FS done!");
  }
}

void loop()
{
  mosquito();
}

void mosquito()
{
  static unsigned long lastRun = 0;
  unsigned long now = millis();

  if (now - lastRun >= 25) 
  {
    lastRun = now;

    light = analogRead(AIN);

    int currentMode = mode;

    if (light > threshold)
    {
      mode = 1; // hunting
    }
    else
    {
      mode = 0; // sleeping
    }

    mosquito_sound();
  }
}

void mosquito_sound()
{
  static unsigned long lastFreqChange = 0;
  static unsigned long lastAmpChange = 0;
  static int freq = 520;
  static int direction = 1;
  static int amplitude = 100;
  static int ampDir = 1;
  unsigned long now = millis();
  static unsigned long ampInterval = 25; // Start with 25 ms
  static unsigned long freqInterval = 2000; // Start with 2000 ms

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

  // Only make sound in "hunting" mode
  if (mode == 1)
    analogWrite(LED, amplitude);
  else
    analogWrite(LED, 0);
}