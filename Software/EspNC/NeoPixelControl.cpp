#include "NeoPixelControl.h"
#include <TimeLib.h>

// Provided by EspNC.ino
extern bool nixieOn;

Adafruit_NeoPixel strip(LED_COUNT, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

int LedBrightness;
int LedBrightnessPercentage;
uint8_t led_brightness_indx;
const int led_brightness_num_intervals = 20;
const int led_brightness_intervals[20] = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50,
                                          55, 60, 65, 70, 75, 80, 85, 90, 95, 100};

uint8_t LED_effect = 0;
uint8_t static_color_indx;
const int static_color_num_colors = 12;
const uint32_t staticColors[12] = {
    strip.Color(255, 0, 0),
    strip.Color(250, 25, 0),
    strip.Color(240, 50, 0),
    strip.Color(210, 75, 0),
    strip.Color(180, 140, 0),
    strip.Color(110, 200, 0),
    strip.Color(0, 255, 0),
    strip.Color(0, 150, 200),
    strip.Color(0, 0, 255),
    strip.Color(60, 0, 170),
    strip.Color(130, 0, 180),
    strip.Color(180, 0, 70)
};
uint8_t static_color;

uint8_t ringMode = RING_MODE_FILL;
uint8_t ringBrightnessIndx = 19; // 100%
int ringBrightnessPercentage = 100;
uint8_t ringColorIndx = 2; // Orange
const int ring_color_num_options = static_color_num_colors + 2; // + rainbow, rainbow cycle

unsigned long rainbowCyclesPreviousMillis = 0;
unsigned long rainbowPreviousMillis = 0;
int rainbowCycles = 0;
int rainbowCycleCycles = 0;
static int ringRainbowCycleCycles = 0;
unsigned long pixelsInterval = 50;

uint32_t scaleColor(uint32_t color, int brightnessPercent) {
    if (brightnessPercent <= 0) {
        return 0;
    }
    if (brightnessPercent >= 100) {
        return color;
    }
    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);
    r = (uint8_t)((r * brightnessPercent) / 100);
    g = (uint8_t)((g * brightnessPercent) / 100);
    b = (uint8_t)((b * brightnessPercent) / 100);
    return strip.Color(r, g, b);
}

static void clearTubeLeds() {
    for (uint16_t i = 0; i < TUBE_LED_COUNT; i++) {
        strip.setPixelColor(i, 0);
    }
}

static void fillTubeLeds(uint32_t color) {
    uint32_t scaled = scaleColor(color, LedBrightnessPercentage);
    for (uint16_t i = 0; i < TUBE_LED_COUNT; i++) {
        strip.setPixelColor(i, scaled);
    }
}

static void clearRingLeds() {
    for (uint16_t i = 0; i < RING_LED_COUNT; i++) {
        strip.setPixelColor(TUBE_LED_COUNT + i, 0);
    }
}

/**
 * Seconds ring: Fill (0..sec) or Dot (current second only).
 * Color: static palette, rainbow (spatial gradient), or rainbow cycle (shifting).
 */
static void updateSecondsRing() {
    if (ringMode == RING_MODE_OFF || !nixieOn) {
        clearRingLeds();
        return;
    }

    uint8_t sec = second();
    if (sec > 59) {
        sec = 59;
    }

    if (ringColorIndx == RING_COLOR_RAINBOW) {
        // Spatial rainbow across the ring (same idea as tube LED "Rainbow")
        ringRainbowCycleCycles++;
        if (ringRainbowCycleCycles >= 256 * 5) ringRainbowCycleCycles = 0;
        for (uint16_t i = 0; i < RING_LED_COUNT; i++) {
            uint16_t idx = TUBE_LED_COUNT + i;
            bool on = (ringMode == RING_MODE_FILL) ? (i <= sec) : (i == sec);
            if (on) {
                uint16_t hue = (RING_LED_COUNT > 1) ? (i * 255 / (RING_LED_COUNT - 1)) : 0;
                uint32_t c = scaleColor(Wheel((hue + ringRainbowCycleCycles) & 255),
                                        ringBrightnessPercentage);
                strip.setPixelColor(idx, c);
            } else {
                strip.setPixelColor(idx, 0);
            }
        }
        return;
    }

    if (ringColorIndx == RING_COLOR_RAINBOW_CYCLE) {
        // Smooth solid hue sweep (~60s per full spectrum via 16-bit HSV + gamma)
        const uint32_t cycleMs = 60000UL;
        uint16_t hue = (uint16_t)(((millis() % cycleMs) * 65536UL) / cycleMs);
        uint32_t color = scaleColor(strip.gamma32(strip.ColorHSV(hue)), ringBrightnessPercentage);
        for (uint16_t i = 0; i < RING_LED_COUNT; i++) {
            uint16_t idx = TUBE_LED_COUNT + i;
            bool on = (ringMode == RING_MODE_FILL) ? (i <= sec) : (i == sec);
            strip.setPixelColor(idx, on ? color : 0);
        }
        return;
    }

    uint32_t color = scaleColor(staticColors[ringColorIndx % static_color_num_colors],
                                ringBrightnessPercentage);

    for (uint16_t i = 0; i < RING_LED_COUNT; i++) {
        uint16_t idx = TUBE_LED_COUNT + i;
        bool on = (ringMode == RING_MODE_FILL) ? (i <= sec) : (i == sec);
        strip.setPixelColor(idx, on ? color : 0);
    }
}

void initNeoPixels() {
    strip.begin();
    strip.setBrightness(255);
    strip.show();
}

void updateLEDs() {
    static unsigned long lastUpdateMs = 0;
    unsigned long nowMs = millis();
    if ((uint32_t)(nowMs - lastUpdateMs) < pixelsInterval) {
        return;
    }
    lastUpdateMs = nowMs;

    // Global brightness left at full; tubes and ring scale independently
    strip.setBrightness(255);
    LedBrightness = map(LedBrightnessPercentage, 0, 100, 0, 255);

    // Night/manual off: blank strip without changing LED_effect (menus show config)
    if (!nixieOn) {
        strip.fill(0);
        strip.show();
        yield();
        return;
    }

    switch (LED_effect) {
        case 0:
            clearTubeLeds();
            break;

        case 1:
            rainbowCycleCycles++;
            if (rainbowCycleCycles >= 256 * 5) rainbowCycleCycles = 0;
            rainbowCycle();
            break;

        case 2:
            rainbowCycles++;
            if (rainbowCycles >= 256) rainbowCycles = 0;
            rainbow();
            break;

        default:
            if (LED_effect >= 3 && LED_effect <= 14) {
                fillTubeLeds(staticColors[LED_effect - 3]);
            } else {
                clearTubeLeds();
            }
            break;
    }

    updateSecondsRing();
    strip.show();
    yield();
}

void rainbowCycle() {
    for (uint16_t i = 0; i < TUBE_LED_COUNT; i++) {
        uint16_t hue = (TUBE_LED_COUNT > 1) ? (i * RAINBOW_SPAN / (TUBE_LED_COUNT - 1)) : 0;
        uint32_t c = scaleColor(Wheel((hue + rainbowCycleCycles) & 255), LedBrightnessPercentage);
        strip.setPixelColor(i, c);
    }
}

void rainbow() {
    for (uint16_t i = 0; i < TUBE_LED_COUNT; i++) {
        uint32_t c = scaleColor(Wheel((i + rainbowCycles) & 255), LedBrightnessPercentage);
        strip.setPixelColor(i, c);
    }
}

uint32_t Wheel(byte WheelPos) {
    WheelPos = 255 - WheelPos;
    if (WheelPos < 85) {
        return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
    }
    if (WheelPos < 170) {
        WheelPos -= 85;
        return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
    }
    WheelPos -= 170;
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}
