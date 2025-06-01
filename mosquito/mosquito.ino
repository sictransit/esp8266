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

  if (now - lastRun >= 200) 
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

    if (mode == 1)
    {     
      analogWrite(LED, 128);
    }
    else
    {
      analogWrite(LED, 0);
    }
  }
}