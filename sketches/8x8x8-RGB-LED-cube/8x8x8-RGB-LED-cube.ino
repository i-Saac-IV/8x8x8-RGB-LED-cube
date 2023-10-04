/*

Creator: Isaac Pawley
Date: 23-09-2023
Repo: https://github.com/i-Saac-IV/8x8x8-RGB-LED-cube

Hiya! This is just some code to control an pretty cool 8x8x8 addressable LED cube. It may or may not work with smaller/larger cube sizes, but I can't test them.
Be sure to set size of the cube, the pin its attached to, the audio in pin and mic.

Pressing button A will change the VU meter animation, long press of the button changes the audio input (green: line in, red: mic, default: line in).
Pressing button B will change the colour paletteMode.

Original hardware:
Raspberry Pi Pico
512 WS2182B 8mm LEDs

*/

/* to do */
// write all the code...


#include <EasyButton.h>
#include <arduinoFFT.h>
#include <FastLED.h>


/* pin definitions, change as needed */
#define BUTTON_A_PIN 2
#define BUTTON_B_PIN 3
#define BRIGHTNESS_POT_PIN A0
#define MIC_PIN A1
#define AUDIO_PIN A2
#define ACT_LED_PIN 9
#define LED_CUBE_PIN 7

/* maths stuff, change as needed */
#define LONG_PRESS_LENGTH 1000  // duration of time (in millis) to press buttonA toi change the input mode (mic/audio)
#define LED_CUBE_SIZE 8         // number of leds along an edge, this assumes its a true cube, not rectangular prism (UPDATE IF STATMENTS IN FFT IF CHANGED FROM 8)
#define MAX_BRIGHTNESS 200
#define FRAMES_PER_SECOND 120
#define MAX_VAL_DELAY 50     // how offen the recentMaxVal is decayed (in millis)
#define MAX_VAL_FACTOR 0.90  // what the recentMaxVal is multiplied by every MAX_VAL_DELAY.
#define MAX_VAL_SCALE 1.75   // how much the recentMaxVal is multiplied by after a new max val is set to prevent too much peaking
#define DECAY_PERIOD 50      // duration of time (in millis) before the VUPeak is is decayed one unit.
#define VU_TIMEOUT 60        // duration of time (in seconds) that the VU meter will remain idle before displaying a random animation.
#define NUM_LEDS (LED_CUBE_SIZE * LED_CUBE_SIZE * LED_CUBE_SIZE)

// boring FastLED definitions
#define LED_TYPE WS2812B
#define COLOUR_ORDER GRB

/* FFT stuff, this stuff really matters later on in the code and changing this ~will~ give undsirable results unless you know what you're doing!! */
#define SAMPLES 512
#define SAMPLING_FREQ 40000  // Hz, must be 40000 or less due to ADC conversion time.
#define NUM_BANDS LED_CUBE_SIZE
#define AUDIO_SAMPLE_PERIOD 54  //duration of time (in millis) between each audio sample, this should be as low as possable for "real time" but is limited by speed of mC (54 min, rp r2040)
#define FILTER 500


/* global variables, generally these dont need changing */
double hue = 0.00;
unsigned long counter = 0UL;
volatile uint8_t mode = 0;
volatile bool updatePalette = 1;
uint8_t palette[LED_CUBE_SIZE + 1];
volatile uint8_t paletteSaturation = 0;
volatile uint8_t paletteMode = 0;
volatile int brightness;
volatile bool mic_en = 0;

// FFT stuff, again
unsigned int sampling_period;
unsigned int rawBandVal[NUM_BANDS];
volatile int prevVUheight[NUM_BANDS];
volatile unsigned int VUHeight[NUM_BANDS];
volatile unsigned int VUPeak[NUM_BANDS];
unsigned int recentMaxVal;
double vReal[SAMPLES];
double vImag[SAMPLES];
unsigned long fftSampling_timer;

// timers
unsigned long maxVal_timer;
unsigned long decay_timer;
unsigned long sample_timer;
unsigned long lastSound_timer;


/* class constructors */
EasyButton buttonA(BUTTON_A_PIN);
EasyButton buttonB(BUTTON_B_PIN);
CRGB led_cube[NUM_LEDS];
arduinoFFT FFT = arduinoFFT(vReal, vImag, SAMPLES, SAMPLING_FREQ);

/**************************************** CORE 0 ***************************************/

void setup() {  //setup for core 0 (FastLED core)
  delay(3000);
  Serial.begin(19200);
  Serial.println(__FILE__);
  Serial.println(__DATE__);
  Serial.println(__TIME__);

  FastLED.addLeds<LED_TYPE, LED_CUBE_PIN, COLOUR_ORDER>(led_cube, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(map(analogRead(BRIGHTNESS_POT_PIN), 0, 1024, 0, MAX_BRIGHTNESS));

  //led matrix sanitiy check
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

void loop() {  //loop for core 0 (FastLED core)
  FastLED.setBrightness(map(analogRead(BRIGHTNESS_POT_PIN), 0, 1024, 0, MAX_BRIGHTNESS));
  if (updatePalette) {  // update the colour paletteMode
    switch (paletteMode) {
      case 0:  // static rainbow
        for (int i = 0; i < LED_CUBE_SIZE - 1; i++) {
          palette[i] = (255 / LED_CUBE_PIN) * i;
        }
        break;
      case 1:  // rainbow
        for (int i = 0; i < LED_CUBE_SIZE - 1; i++) {
          palette[i] = ((255 / LED_CUBE_PIN) * i) + hue;
        }
        break;
      default:
        paletteMode = 0;
        break;
    }
    updatePalette = !updatePalette;
  }

  switch (mode) {
    case 0:
      confetti(hue, 200, paletteMode, 1);
      break;
    case 1:
      raining(hue, 255, 70, 5);
      break;
    default:
      mode = 0;
      break;
  }
  FastLED.show();
  hue -= 0.1;
  counter++;
  delay(1000 / FRAMES_PER_SECOND);
}

int calc_target_led(int x, int y, int z) {  // cube mapping magic
  // x = (LED_CUBE_SIZE - 1) - x; // flip x-axis
  y = (LED_CUBE_SIZE - 1) - y;  // flip y-axis
  // z = (LED_CUBE_SIZE - 1) - z; // flip z-axis
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

void confetti(int h, int s, int fade, int rate) {
  if (counter % rate == 1) {
    fadeToBlackBy(led_cube, NUM_LEDS, fade);
    led_cube[random(NUM_LEDS)] += CHSV(h + random(32), s, 255);
  }
}

void raining(int h, int s, int fade, int rate) {
  if (counter % rate == 1) {
  for (int yLevel = LED_CUBE_SIZE - 1; yLevel >= 0; yLevel--) {
    for (int led = 0; led < LED_CUBE_SIZE * LED_CUBE_SIZE; led++) {
      led_cube[led + ((yLevel * LED_CUBE_SIZE * LED_CUBE_SIZE) - 1)] += led_cube[led + ((yLevel * LED_CUBE_SIZE * LED_CUBE_SIZE) - 2)];
    }
    led_cube[calc_target_led(LED_CUBE_SIZE - 1, random(LED_CUBE_SIZE - 1), LED_CUBE_SIZE - 1)] += CHSV(h, s, 255);
  }
  fadeToBlackBy(led_cube, NUM_LEDS, fade);
  }
}




/**************************************** CORE 1 ***************************************/

void setup1() {  //setup for core 1 (FFT core)
  sampling_period = round(1000000 * (1.0 / SAMPLING_FREQ));
  pinMode(ACT_LED_PIN, OUTPUT);

  buttonA.begin();
  buttonB.begin();

  buttonA.onPressed(cycleMode);
  buttonB.onPressed(cyclePalette);
  buttonA.onPressedFor(LONG_PRESS_LENGTH, cycleInput);
  buttonA.onPressedFor(LONG_PRESS_LENGTH, cycleSaturation);
}

void loop1() {  //loop for core 1 (FFT core)
  buttonA.read();
  buttonB.read();

  if (maxVal_timer + MAX_VAL_DELAY > millis()) {
    recentMaxVal *= MAX_VAL_SCALE;
    if (recentMaxVal < FILTER * 2) {
      recentMaxVal = FILTER * 2;
    } else {
      lastSound_timer = millis();
    }
    maxVal_timer = millis();
  }

  if (decay_timer + DECAY_PERIOD > millis()) {
    for (int band = 0; band < NUM_BANDS; band++) {
      if (VUPeak[band > 0]) VUPeak[band] -= 1;
    }
    decay_timer = millis();
  }

  if (sample_timer + (AUDIO_SAMPLE_PERIOD / 2) > millis()) {
    digitalWrite(ACT_LED_PIN, HIGH);
    take_samples();
    digitalWrite(ACT_LED_PIN, LOW);
    do_FFT_maths();
    sample_timer = millis();
  }
}

void take_samples() {
  int readPin;
  if (mic_en) {
    readPin = MIC_PIN;
  } else {
    readPin = AUDIO_PIN;
  }
  for (int i = 0; i < SAMPLES; i++) {
    fftSampling_timer = micros();
    vReal[i] = analogRead(readPin);
    vImag[i] = 0;
    while ((micros() - fftSampling_timer) < sampling_period) {
      /* wait */
    }
  }
}

void do_FFT_maths() {
  /* Compute FFT */
  FFT.DCRemoval();
  FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(FFT_FORWARD);  // this takes ages
  FFT.ComplexToMagnitude();

  for (int i = 1; i < (SAMPLES / 2); i++) {
    if ((int)vReal[i] < FILTER) vReal[i] = 0;  // basic filtering FFT output into band values

    /* THE FOLLOWING LINES MAY NEED CHANGING IF ANY OF THE FFT DEFINITIONS ARE CHANGED */
    //8 bands, 11kHz top band
    if (i <= 2) rawBandVal[0] += (int)vReal[i];
    if (i > 2 && i <= 3) rawBandVal[1] += (int)vReal[i];
    if (i > 3 && i <= 6) rawBandVal[2] += (int)vReal[i];
    if (i > 6 && i <= 13) rawBandVal[3] += (int)vReal[i];
    if (i > 13 && i <= 26) rawBandVal[4] += (int)vReal[i];
    if (i > 26 && i <= 52) rawBandVal[5] += (int)vReal[i];
    if (i > 52 && i <= 105) rawBandVal[6] += (int)vReal[i];
    if (i > 105) rawBandVal[7] += (int)vReal[i];
  }

  for (int band = 0; band < NUM_BANDS; band++) {
    prevVUheight[band] = VUHeight[band];

    if (recentMaxVal < rawBandVal[band]) {
      recentMaxVal = rawBandVal[band];
      recentMaxVal *= MAX_VAL_SCALE;
    }
    VUHeight[band] = map(rawBandVal[band], 0, recentMaxVal, 0, LED_CUBE_PIN - 1);  // maps the rawBandVal to the size of the cube, with some auto scalling (gain??)
    rawBandVal[band] = 0;

    if (VUHeight[band] < prevVUheight[band]) VUHeight[band] = (VUHeight[band] + prevVUheight[band]) / 2;  // little but of smoothing between frames

    if (VUPeak[band] < VUHeight[band]) VUPeak[band] = VUHeight[band];
  }
}

void cycleMode() {
  mode++;
}

void cyclePalette() {
  updatePalette = !updatePalette;
  paletteMode++;
}

void cycleInput() {
  mic_en = !mic_en;
}

void cycleSaturation() {
  paletteSaturation++;
  switch (paletteSaturation) {
    case 0:

      break;
    case 1:

      break;
    default:
      paletteSaturation = 0;
      break;
  }
}