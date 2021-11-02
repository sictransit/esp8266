#include <U8g2lib.h>
#include <Arduino.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include "ArduinoJson.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_NeoPixel.h>
#include <PubSubClient.h>

#define PIN D3

#define BME_SCK 13
#define BME_MISO 12
#define BME_MOSI 11
#define BME_CS 10

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, PIN, NEO_GRB + NEO_KHZ800);
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);
Adafruit_BMP280 bme; // I2C

const char* SMHI = "http://opendata-download-metobs.smhi.se/api/version/latest/parameter/1/station/97530/period/latest-hour/data.json";

ESP8266WiFiMulti WiFiMulti;
WiFiClient simpleClient;
PubSubClient mqttClient(simpleClient);

double p0 = 0;
double t0 = 0;

void setup(void) {
  Serial.begin(115200);
  Serial.println();

  pixels.begin();

  bool status;

  // default settings
  // (you can also pass in a Wire library object like &Wire2)
  status = bme.begin(0x76);
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }

  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setFont(u8g2_font_ncenB14_tf);

  // Prevents the flash memory from being worn out, see: https://github.com/esp8266/Arduino/issues/1054 .
  // This will however delay node WiFi start-up by about 700 ms. The delay is 900 ms if we otherwise would have stored the WiFi network we want to connect to.
  WiFi.persistent(false);

  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP("SicTransit.Guest", "Gloria1314Mundi");
}

void loop(void) {
  if ((WiFiMulti.run() == WL_CONNECTED)) {

    Serial.println(bme.readTemperature());

    double temp = bme.readTemperature();
    double pres = bme.readPressure() / 100;

    String sign = "";
    if (temp > 0) {
      sign = "+";
    }

    String tempLabel = sign + String(temp, 2) + " °C";
    Serial.println(tempLabel);

    String presLabel = String(pres, 3) + " hPa";
    Serial.println(presLabel);


    u8g2.firstPage();
    do {
      //keep this the same as it pages through the data generating the screen
      u8g2.setCursor(0, 20);

      u8g2.print("Inomhus");
      u8g2.setCursor(0, 40);
      u8g2.print(tempLabel);

      u8g2.setCursor(0, 60);
      u8g2.print(presLabel);

    } while ( u8g2.nextPage() );

    if (abs(p0 - pres) < 0.1) {
      blink(0, 255, 0, 100);
    } else if (temp > t0) {
      blink(255, 0, 0, 2000);
    }
    else
    {
      blink(0, 0, 255, 100);
    }

    p0 = pres;
    t0 = temp;

  } else  {
    Serial.println("No WiFi!");
  }

  delay(500);
}

void blink(int r, int g, int b, int t) {
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
  delay(t);
  pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  pixels.show();
}
