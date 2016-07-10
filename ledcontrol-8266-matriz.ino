// https://github.com/wvdv2002/ESP8266-LED-Websockets/blob/master/arduino/ledcontrol/ledcontrol.ino
//Modificado por GNK 
//se pretende pasar a un control matricial e incluir los efectos de ESP8266 FastledNoise
// y finalmente todos los efectos , 


//#include <WiFiManager.h>

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <FastLED.h>
#include <Hash.h>
#include <EEPROM.h>
#include <WebSockets.h>
#include <WebSocketsServer.h>
#include "SettingsServer.h"
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266SSDP.h>



#include <ESP8266WebServer.h>



int  ledState = LOW;

#define USE_SERIAL Serial
// Wifi credentials

extern "C" {
  #include "user_interface.h"
}

// Defining LED strip

#define DATA_PIN D2                //Using WS2812B -- if you use APA102 or other 4-wire LEDs you need to also add a clock pin
#define CLOCK_PIN   D1
#define LED_TYPE    APA102
#define COLOR_ORDER GBR

#define MILLI_AMPERE      1000    // IMPORTANT: set here the max milli-Amps of your power supply 5V 2A = 2000
#define FRAMES_PER_SECOND  200    // here you can control the speed. 
int ledMode = 4;                  // this is the starting palette
const uint8_t kMatrixWidth  = 24;
const uint8_t kMatrixHeight = 20;
const bool    kMatrixSerpentineLayout = true;
#define NUM_LEDS kMatrixWidth* kMatrixHeight            //Number of LEDs in your strip
#define MAX_DIMENSION ((kMatrixWidth>kMatrixHeight) ? kMatrixWidth : kMatrixHeight)

int BRIGHTNESS =           128;   // this is half brightness 
int new_BRIGHTNESS =       128;   // shall be initially the same as brightness



CRGB leds[NUM_LEDS];
CRGBSet ledSet(leds, NUM_LEDS);    //Trying LEDSet from FastLED

// The 16 bit version of our coordinates
static uint16_t x;
static uint16_t y;
static uint16_t z;

// We're using the x/y dimensions to map to the x/y pixels on the matrix.  We'll
// use the z-axis for "time".  speed determines how fast time moves forward.  Try
// 1 for a very slow moving effect, or 60 for something that ends up looking like
// water.
uint16_t speed = 20; // speed is set dynamically once we've started up

// Scale determines how far apart the pixels in our noise matrix are.  Try
// changing these values around to see how it affects the motion of the display.  The
// higher the value of scale, the more "zoomed out" the noise iwll be.  A value
// of 1 will be so zoomed in, you'll mostly see solid colors.
uint16_t scale = 30; // scale is set dynamically once we've started up

// This is the array that we keep our computed noise values in
uint8_t noise[MAX_DIMENSION][MAX_DIMENSION];
 CRGBPalette16 currentPalette( CRGB::Black );

 CRGBPalette16 targetPalette( CRGB::Black );
uint8_t       colorLoop = 1;


//Some Variables
byte myEffect = 1;                  //what animation/effect should be displayed

byte myHue = 33;                    //I am using HSV, the initial settings display something like "warm white" color at the first start
byte mySaturation = 168;
byte myValue = 255;
unsigned int myWhiteLedValue=0;
byte rainbowHue = myHue;            //Using this so the rainbow effect doesn't overwrite the hue set on the website

int flickerTime = random(200, 400);
int flickerLed;
int flickerValue = 110 + random(-3, +3); //70 works nice, too
int flickerHue = 33;

bool eepromCommitted = true;      

unsigned long currentTime = 0;
unsigned long previousTime = 0;
unsigned long lastChangeTime = 0;
unsigned long currentChangeTime = 0;

#include "LEDanimations.h"
#include "LEDWebsockets.h"

void writeWhiteLedPWMIfChanged(int value)
{
  static int oldPWMValue = 9999;
  if (oldPWMValue != value)
  {
    oldPWMValue = value;
    analogWrite(0,value);
  }
}


void setup() {
   pinMode(LED_BUILTIN, OUTPUT);// led pin as aoutput
   
   // Initialize our coordinates to some random values
  x = random16();
  y = random16();
  z = random16();
  
  writeWhiteLedPWMIfChanged(0);  
  EEPROM.begin(6);  // Using simulated EEPROM on the ESP8266 flash to remember settings after restarting the ESP
  Serial.begin(115200);
  Serial.println("Ledtest example");
  LEDS.addLeds<LED_TYPE,DATA_PIN,CLOCK_PIN,COLOR_ORDER>(leds,NUM_LEDS);
   FastLED.setBrightness(BRIGHTNESS);
  set_max_power_in_volts_and_milliamps(5, MILLI_AMPERE);
  LEDColorCorrection{ TypicalSMD5050 };

  //LEDS.addLeds<WS2812B,DATA_PIN,GRB>(leds,NUM_LEDS);  // Initialize the LEDs

  // Reading EEPROM
  myEffect = 1;                         // Only read EEPROM for the myEffect variable after you're sure the animation you are testing won't break OTA updates, make your ESP restart etc. or you'll need to use the USB interface to update the module.
//  myEffect = EEPROM.read(0); //blocking effects had a bad effect on the website hosting, without commenting this away even restarting would not help
  myHue = EEPROM.read(1);
  mySaturation = EEPROM.read(2);
  myValue = EEPROM.read(3);
  myWhiteLedValue = EEPROM.read(4);
  myWhiteLedValue += EEPROM.read(5)*256;
  
  delay(100);                                         //Delay needed, otherwise showcolor doesn't light up all leds or they produce errors after turning on the power supply - you will need to experiment
  LEDS.showColor(CHSV(myHue, mySaturation, myValue));

  setupWiFi();
  startSettingsServer();
  //Websocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}




void loop() {

   EVERY_N_MILLISECONDS(500) {                           // FastLED based non-blocking delay to update/display the sequence.
     ledState=!ledState ;
     digitalWrite(LED_BUILTIN, ledState );
     
    }
    
  webSocket.loop();                           // handles websockets
  settingsServerTask();
  if (myEffect!=5)
  {
    writeWhiteLedPWMIfChanged(myWhiteLedValue);  
  }
  else
  {
    writeWhiteLedPWMIfChanged(0);      
  }


switch (myEffect) {                           // switches between animations
    case 1: // Solid Color
      EVERY_N_MILLISECONDS( 20 ) {
        ledSet = CHSV(myHue, mySaturation, myValue);
        LEDS.show();
      }

      break;
    case 2: // Ripple effect
      ripple();
      break;
    case 3: // Cylon effect
      cylon();
      break;
    case 4: // Fire effect
      Fire2012();
      break;
    case 5: // Turn off all LEDs
      EVERY_N_MILLISECONDS( 20 ) {
      ledSet.fadeToBlackBy(2);
      LEDS.show();
      
      }
      break;
    case 6: // loop through hues with all leds the same color. Can easily be changed to display a classic rainbow loop
      EVERY_N_MILLISECONDS( 15 ) {
      rainbowHue = rainbowHue + 1;
      LEDS.showColor(CHSV(rainbowHue, mySaturation, myValue));
     }
      break;
    case 7: // make a single, random LED act as a candle
      currentTime = millis();
      ledSet.fadeToBlackBy(1);
      leds[flickerLed] = CHSV(flickerHue, 255, flickerValue);
      flickerTime = random(150, 500);
      if (currentTime - previousTime > flickerTime) {
        flickerValue = 110 + random(-10, +10); //70 works best
        flickerHue = 33; //random(33, 34);
        previousTime = currentTime;
        LEDS.show();
      }
      break;
          case 8: // noise1 effect
			  ledMode = myEffect;
      LedsNoise ();
      LEDS.show();
      break;
    default: 
      LEDS.showColor(CRGB(0, 255, 0)); // Bright green in case of an error
    break;
  }
  
  // EEPROM-commit and websocket broadcast -- they get called once if there has been a change 1 second ago and no further change since. This happens for performance reasons.
  currentChangeTime = millis();
  if (currentChangeTime - lastChangeTime > 2000 && eepromCommitted == false) {
     Serial.print("Heap free: ");  
     Serial.println(system_get_free_heap_size());

    EEPROM.commit();
    eepromCommitted = true;
    String websocketStatusMessage = "H" + String(myHue) + ",S" + String(mySaturation) + ",V" + String(myValue)+ ",W" + String(myWhiteLedValue);
    webSocket.broadcastTXT(websocketStatusMessage); // Tell all connected clients which HSV values are running
    //LEDS.showColor(CRGB(0, 255, 0));  //for debugging to see when if-clause fires
    //delay(50);                        //for debugging to see when if-clause fires
  }
}
