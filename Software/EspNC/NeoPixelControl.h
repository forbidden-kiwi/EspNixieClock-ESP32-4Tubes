#ifndef NEOPIXEL_CONTROL_H
#define NEOPIXEL_CONTROL_H

#include <Adafruit_NeoPixel.h>
#include "Globals.h"

#define RING_MODE_OFF  0
#define RING_MODE_FILL 1
#define RING_MODE_DOT  2

// Ring color indices: 0..11 static, then animated
#define RING_COLOR_RAINBOW       12
#define RING_COLOR_RAINBOW_CYCLE 13

extern Adafruit_NeoPixel strip;

extern int LedBrightness;
extern int LedBrightnessPercentage;
extern uint8_t led_brightness_indx;
extern const int led_brightness_num_intervals;
extern const int led_brightness_intervals[20];

extern uint8_t LED_effect; // 0=off, 1=rainbow, 2=color cycle, 3..14=static color
extern uint8_t static_color_indx;
extern const int static_color_num_colors;
extern const uint32_t staticColors[12];
extern uint8_t static_color;

// Seconds ring (independent of tube LED effect)
extern uint8_t ringMode; // RING_MODE_OFF / FILL / DOT
extern uint8_t ringBrightnessIndx;
extern int ringBrightnessPercentage;
extern uint8_t ringColorIndx;
extern const int ring_color_num_options; // static colors + rainbow + rainbow cycle

extern unsigned long rainbowCyclesPreviousMillis;
extern unsigned long rainbowPreviousMillis;
extern int rainbowCycles;
extern int rainbowCycleCycles;
extern unsigned long pixelsInterval;

void initNeoPixels();
/** Blank LEDs when tubes are off without changing LED_effect (menus show config). */
void updateLEDs();
void rainbowCycle();
void rainbow();
uint32_t Wheel(byte WheelPos);
uint32_t scaleColor(uint32_t color, int brightnessPercent);

#endif
