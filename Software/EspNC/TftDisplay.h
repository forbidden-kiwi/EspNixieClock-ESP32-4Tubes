#pragma once

#include <Adafruit_GFX.h>
#include "Display_ST7789.h"

/**
 * Adafruit_GFX renderer on top of the Waveshare low-level ST7789 driver.
 * Drop-in replacement for Adafruit_ST7789 in EspNC.
 */
class TftDisplay : public Adafruit_GFX {
public:
    TftDisplay();

    void begin();
    void fillScreen(uint16_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h,
                    uint16_t color);
    void startWrite();
    void endWrite();

private:
    uint8_t _batchDepth;
};
