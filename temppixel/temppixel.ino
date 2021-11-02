#include <Arduino.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Adafruit_NeoPixel.h>
#include "ArduinoJson.h"

#define PIN D2
#define DELAYTIME 60000
#define GRADIENT_SIZE 90

const float MIN_TEMP = -35;
const float MAX_TEMP = 35;

const char* SMHI = "http://opendata-download-metobs.smhi.se/api/version/latest/parameter/1/station/97530/period/latest-hour/data.json";

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, PIN, NEO_GRB + NEO_KHZ800);
ESP8266WiFiMulti WiFiMulti;

unsigned int TempGradient [GRADIENT_SIZE] = {
  0xff0ef0,
  0xff0df0,
  0xff0cf0,
  0xff0bf0,
  0xff0af0,
  0xff09f0,
  0xff08f0,
  0xff07f0,
  0xff06f0,
  0xff05f0,
  0xff04f0,
  0xff03f0,
  0xff02f0,
  0xff01f0,
  0xff00f0,
  0xff00e0,
  0xff00d0,
  0xff00c0,
  0xff00b0,
  0xff00a0,
  0xff0090,
  0xff0080,
  0xff0070,
  0xff0060,
  0xff0050,
  0xff0040,
  0xff0030,
  0xff0020,
  0xff0010,
  0xff0000,
  0xff0a00,
  0xff1400,
  0xff1e00,
  0xff2800,
  0xff3200,
  0xff3c00,
  0xff4600,
  0xff5000,
  0xff5a00,
  0xff6400,
  0xff6e00,
  0xff7800,
  0xff8200,
  0xff8c00,
  0xff9600,
  0xffa000,
  0xffaa00,
  0xffb400,
  0xffbe00,
  0xffc800,
  0xffd200,
  0xffdc00,
  0xffe600,
  0xfff000,
  0xfffa00,
  0xfdff00,
  0xd7ff00,
  0xb0ff00,
  0x8aff00,
  0x65ff00,
  0x3eff00,
  0x17ff00,
  0x00ff10,
  0x00ff36,
  0x00ff5c,
  0x00ff83,
  0x00ffa8,
  0x00ffd0,
  0x00fff4,
  0x00e4ff,
  0x00d4ff,
  0x00c4ff,
  0x00b4ff,
  0x00a4ff,
  0x0094ff,
  0x0084ff,
  0x0074ff,
  0x0064ff,
  0x0054ff,
  0x0044ff,
  0x0032ff,
  0x0022ff,
  0x0012ff,
  0x0002ff,
  0x0000ff,
  0x0100ff,
  0x0200ff,
  0x0300ff,
  0x0400ff,
  0x0500ff
};

void setup() {
  pixels.begin();

  Serial.begin(115200);
  Serial.println();

  // Prevents the flash memory from being worn out, see: https://github.com/esp8266/Arduino/issues/1054 .
  // This will however delay node WiFi start-up by about 700 ms. The delay is 900 ms if we otherwise would have stored the WiFi network we want to connect to.
  WiFi.persistent(false);

  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP("SicTransit.Net", "GloriaMundi1723");
}

void loop() {
  // check wifi connection
  if ((WiFiMulti.run() == WL_CONNECTED)) {
    String json = httpGETRequest(SMHI);

    DynamicJsonBuffer  JSONBuffer(300); // TODO: check memory pool size
    JsonObject& parsed = JSONBuffer.parseObject(json); 

    // TODO: handle parsing errors 
    if (!parsed.success()) {
      Serial.println("Parsing failed");
      delay(5000);
      return;
    }

    double temp = parsed["value"][0]["value"];
    //Serial.println(temp);    

    int index = -GRADIENT_SIZE / (MAX_TEMP - MIN_TEMP) * (temp + MIN_TEMP) ;

    if (index < 0) {
      index = 0;
    }
    else if (index >= GRADIENT_SIZE) {
      index = GRADIENT_SIZE - 1;
    }

        Serial.println(index);


    int t = TempGradient[index];

    int r = (t >> 16) & 0xff;
    int g = (t >> 8) & 0xff;
    int b = t & 0xff;;

    pixels.setPixelColor(0, pixels.Color(r, g, b));
    pixels.show();

    delay(DELAYTIME);

  } else  {
    delay(1000);
  }
}

String httpGETRequest(const char* serverName) {
  WiFiClient client;
  HTTPClient http;

  http.begin(client, serverName);

  int httpResponseCode = http.GET();

  String payload = "{}";

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();

  return payload;
}
