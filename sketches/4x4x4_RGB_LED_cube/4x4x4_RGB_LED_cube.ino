/*

Creator: Isaac Pawley
Date: 23-09-2023
Repo: https://github.com/i-Saac-IV/8x8x8-RGB-LED-cube

Hiya! This is just some code to control an pretty cool 4x4x4 addressable LED cube. It may or may not work with smaller/larger cube sizes, but I can't test them.
Be sure to set size of the cube, the pin its attached to, the audio in pin and mic.

This is a testing sketch.

Original hardware:
Arduino Uno
64 WS2182B 8mm LEDs

*/

/* to do */


#include <FastLED.h>

/* pin definitions, change as needed */
#define POT_A_PIN A1
#define BRIGHTNESS_POT_PIN A0
#define ACT_LED_PIN 9
#define LED_CUBE_PIN 7

/* maths stuff, change as needed */
#define LED_CUBE_SIZE 4  // number of leds along an edge, this assumes its a true cube, not rectangular prism
#define MAX_BRIGHTNESS 100
#define FRAMES_PER_SECOND 120
#define NUM_LEDS (LED_CUBE_SIZE * LED_CUBE_SIZE * LED_CUBE_SIZE)

// boring FastLED definitions
#define LED_TYPE PL9823
#define COLOUR_ORDER RGB

/* global variables, generally these dont need changing */
double hue = 0.00;
unsigned long counter = 0UL;

/* class constructors */
CRGB led_cube[NUM_LEDS];

/**************************************** CORE 0 ***************************************/

void setup() {  //setup for core 0 (FastLED core)
  // delay(3000);
  Serial.begin(19200);
  Serial.println(__FILE__);
  Serial.println(__DATE__);
  Serial.println(__TIME__);

  FastLED.addLeds<LED_TYPE, LED_CUBE_PIN, COLOUR_ORDER>(led_cube, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(map(analogRead(BRIGHTNESS_POT_PIN), 0, 1024, 0, MAX_BRIGHTNESS));

  // led matrix sanitiy check
  fill_solid(led_cube, NUM_LEDS, CRGB::Red);
  FastLED.show();
  delay(333);
  fill_solid(led_cube, NUM_LEDS, CRGB::Green);
  FastLED.show();
  delay(333);
  fill_solid(led_cube, NUM_LEDS, CRGB::Blue);
  FastLED.show();
  delay(333);
  FastLED.clear();
}

void loop() {
  FastLED.setBrightness(map(analogRead(BRIGHTNESS_POT_PIN), 0, 1024, 0, MAX_BRIGHTNESS));

  switch (round(map(analogRead(POT_A_PIN), 0, 1024, 0, 10))) {
    case 0:
      confetti(hue, 200, 20, 5);
      break;
    case 1:
      raining(hue, 255, 0.75, 7, 0.9);
      break;
    default:
      FastLED.clear();
      break;
  }

  Serial.println();
  FastLED.show();
  hue -= 0.1;
  counter++;
  delay(1000 / FRAMES_PER_SECOND);
}

int calc_target_led(int x, int y, int z) {  // cube mapping magic
  x = (LED_CUBE_SIZE - 1) - x;              // flip x-axis
  // y = (LED_CUBE_SIZE - 1) - y;  // flip y-axis
  z = (LED_CUBE_SIZE - 1) - z;  // flip z-axis
  int t = (y * LED_CUBE_SIZE * LED_CUBE_SIZE) + (z * LED_CUBE_SIZE);
  if (z % 2 == 0) {
    t += (LED_CUBE_SIZE - 1) - x;
  } else {
    t += x;
  }
  if (t > NUM_LEDS - 1) {  // very basic error check.
    char buffer[60];
    sprintf(buffer, "ERROR\tx: %d\ty: %d\tz: %d\tt: %d", x, y, z, t);
    Serial.println(buffer);
  }
  return t;
}

/************************************* ANIMATIONS *************************************/

void confetti(uint8_t h, uint8_t s, uint8_t dimFration, uint8_t rate) {
  if (counter % rate == 0) {
    fadeToBlackBy(led_cube, NUM_LEDS, dimFration);
    led_cube[random(NUM_LEDS)] += CHSV(h + random(32), s, 255);
  }
}

void raining(uint8_t h, uint8_t s, float dimFration, uint8_t rate, unsigned long numberOfDrops) {
  if (counter % rate == 0) {
    for (int yLevel = 0; yLevel <= LED_CUBE_SIZE - 2; yLevel++) {
      for (int led = 0; led <= (LED_CUBE_SIZE * LED_CUBE_SIZE) - 1; led++) {
        int copyFromPos = led + ((yLevel + 1) * LED_CUBE_SIZE * LED_CUBE_SIZE);
        int pasteToPos = led + (yLevel * LED_CUBE_SIZE * LED_CUBE_SIZE);
        uint8_t newRed = led_cube[copyFromPos].r * dimFration;
        uint8_t newGreen = led_cube[copyFromPos].g * dimFration;
        uint8_t newBlue = led_cube[copyFromPos].b * dimFration;
        led_cube[pasteToPos] = led_cube[copyFromPos];
        led_cube[copyFromPos] = CRGB(newRed, newGreen, newBlue);
      }
    }
    if (numberOfDrops % 1 != 0) {
      if (random((int)((numberOfDrops % 1) * 100))) led_cube[calc_target_led(random(LED_CUBE_SIZE), LED_CUBE_SIZE - 1, random(LED_CUBE_SIZE))] = CHSV(h, s, 255);
    } else {
      for (int i = 0; i <= numberOfDrops; i++) {
        led_cube[calc_target_led(random(LED_CUBE_SIZE), LED_CUBE_SIZE - 1, random(LED_CUBE_SIZE))] = CHSV(h, s, 255);
      }
    }
  }
}
