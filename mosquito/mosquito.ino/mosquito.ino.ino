#include <ESP8266WiFi.h>
#define LED 4

int brightness = 0;    // how bright the LED is
int fadeAmount = 5;    // how many points to fade the LED by
int sensorValue = 0;
int sensorPin = A0;
int analogFrequency = 330;
int analogRange = 256;
int frequency = 0;
int delta = 1;

//=======================================================================
//                    Power on setup
//=======================================================================
void setup() {
  Serial.begin(115200);
  pinMode(LED,OUTPUT);
  analogWriteFreq(analogFrequency);
  analogWriteRange(analogRange);
  frequency = analogFrequency;
}

//=======================================================================
//                    Main Program Loop
//=======================================================================
void loop() {
  
  if (frequency > analogFrequency + 10 || frequency < analogFrequency - 10)
  {
    delta *= -1;
    Serial.println(frequency);
  }
  
  frequency += delta;
    
  analogWrite(LED,frequency);
  delay(1000);  
}
//=======================================================================