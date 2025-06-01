#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#ifndef APSSID
#define APSSID "ESPap"
#define APPSK "thereisnospoon"
#endif

#define LED 4
#define AIN A0

const char *ssid = APSSID;
const char *password = APPSK;

int brightness = 128;    
int fadeAmount = 1;    
int adcValue = 1023;
int pause = 0;

AsyncWebServer server(80);

void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println("Setup ...");

  initFS();
  initWS();
  initAP();    

  pinMode(LED,OUTPUT);  
  analogWriteFreq(320);

  Serial.println("Setup done!");
}

void initWS()
{
  Serial.println("Init WS ...");
  
  server.serveStatic("/", LittleFS, "/");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

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

void initFS() {
  Serial.println("Init FS ...");

  if (!LittleFS.begin()) {
    Serial.println("FS error!");
  }
  else {
    Serial.println("FS done!");
  }
}

void loop() {
    if (pause++ > 100)
    {
      pause=0;

      if (brightness <=128-8 || brightness >= 128+8)
      {
        fadeAmount*=-1;      
        adcValue = analogRead(AIN);
        //Serial.println(adcValue);
      }
      brightness += fadeAmount;    
      if (adcValue > 128)
      {
        analogWrite(LED, brightness);
      }
      else
      {
        analogWrite(LED, 0);
      }
    }
}