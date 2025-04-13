#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

#ifndef APSSID
#define APSSID "ESPap"
#define APPSK "thereisnospoon"
#endif

#define LED 4
#define AIN A0

const char *ssid = APSSID;
const char *password = APPSK;

int brightness = 128;    // how bright the LED is
int fadeAmount = 1;    // how many points to fade the LED by
int adcValue = 1023;
int pause = 0;

ESP8266WebServer server(80);

void handleRoot() {
  server.send(200, "text/html", "<h1>" + String(adcValue) +"</h1>");
}

//=======================================================================
//                    Power on setup
//=======================================================================
void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println();
  Serial.print("Configuring access point...");
  /* You can remove the password parameter if you want the AP to be open. */
  WiFi.softAP(ssid, password);

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP server started");

  pinMode(LED,OUTPUT);  
  analogWriteFreq(320);
}

//=======================================================================
//                    Main Program Loop
//=======================================================================
void loop() {
    server.handleClient();

    if (pause++ > 100)
    {
      pause=0;

      if (brightness <=128-8 || brightness >= 128+8)
      {
        fadeAmount*=-1;      
        adcValue = analogRead(AIN);
        Serial.println(adcValue);
      }
      brightness += fadeAmount;    
      if (adcValue > 512)
      {
        analogWrite(LED, brightness);
      }
      else
      {
        analogWrite(LED, 0);
      }
    }
}
//=======================================================================