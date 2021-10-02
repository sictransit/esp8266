
#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <Pinger.h>
#include <ESP8266WiFiMulti.h>

#include <ESP8266HTTPClient.h>

#include <WiFiClient.h>

#include <Adafruit_NeoPixel.h>

#define PIN D2
// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS 1

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, PIN, NEO_GRB + NEO_KHZ800);

ESP8266WiFiMulti WiFiMulti;

Pinger pinger;

void setup() {

  pixels.begin();

  Serial.begin(115200);
  // Serial.setDebugOutput(true);

  Serial.println();
  Serial.println();
  Serial.println();

  for (uint8_t t = 4; t > 0; t--) {
    pixels.setPixelColor(0, pixels.Color(255, 255, 255));
    pixels.show();

    Serial.printf("[SETUP] WAIT %d...\n", t);
    Serial.flush();
    delay(500);

    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show();

    delay(500);
  }

  // Prevents the flash memory from being worn out, see: https://github.com/esp8266/Arduino/issues/1054 .
  // This will however delay node WiFi start-up by about 700 ms. The delay is 900 ms if we otherwise would have stored the WiFi network we want to connect to.
  WiFi.persistent(false);

  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP("SicTransit.Guest", "Gloria1314Mundi");

  pinger.OnReceive([](const PingerResponse & response)
  {
    // Return true to continue the ping sequence.
    // If current event returns false, the ping sequence is interrupted.
    return true;
  });

  pinger.OnEnd([](const PingerResponse & response)
  {
    if (response.TotalReceivedResponses < response.TotalSentRequests)
    {
      return false;
    }

    return true;
  });
}

void loop() {
  int delayTime = 10000;
  // wait for WiFi connection
  if ((WiFiMulti.run() == WL_CONNECTED)) {
    if (pinger.Ping("dns.google")) {
      //pixels.setPixelColor(0, pixels.Color(0, 255, 0));
    for (int i = 0; i <= 4; i++) {
      pixels.setPixelColor(0, pixels.Color(0,255,0));
      pixels.show();
      delay(500);
      pixels.setPixelColor(0, pixels.Color(0,0,0));
      pixels.show();
      delay(2000);
    }
      //pixels.show();
      delayTime = 30000;

    } else {
      pixels.setPixelColor(0, pixels.Color(255, 255, 0));
      pixels.show();
      delayTime = 60000;
    }
  } else {
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));
    pixels.show();
  }

  delay(delayTime);
}

void blink(int r, int g, int b) {
    for (int i = 0; i <= 3; i++) {
      pixels.setPixelColor(0, pixels.Color(r, g, b));
      pixels.show();
      delay(1000);
      pixels.clear();
      delay(500);
    }
  }      
