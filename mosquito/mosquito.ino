#include <ESP8266WiFi.h>

#define LED 4
#define AIN A0

int brightness = 128;    // how bright the LED is
int fadeAmount = 1;    // how many points to fade the LED by
int adcValue = 1023;

//=======================================================================
//                    Power on setup
//=======================================================================
void setup() {
  Serial.begin(115200);
  pinMode(LED,OUTPUT);
  analogWriteFreq(320);
  //analogWrite(LED, 200);
}

//=======================================================================
//                    Main Program Loop
//=======================================================================
void loop() {
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
    //delay(1);
}
//=======================================================================