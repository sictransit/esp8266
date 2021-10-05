#include <Adafruit_NeoPixel.h>

#define PIN D2
// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS 1

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, PIN, NEO_GRB + NEO_KHZ800);


void setup()
{
  pixels.begin();
}

void loop()
{
  double pi = 3.14159;
  
  int delayval = 20; 
  int rP = 283;
  int gP = 547;
  int bP = 1087;  

  for (int i = 0; i < rP*gP*bP; i++)
  {
    double r = sin(2*pi*(i%rP)/rP)*127+127;
    double g = sin(2*pi*(i%gP)/gP)*127+127;
    double b = sin(2*pi*(i%bP)/bP)*127+127;    

   
    pixels.setPixelColor(0, pixels.Color(r, g, b));
    pixels.show();
    delay(delayval);
  }
}
