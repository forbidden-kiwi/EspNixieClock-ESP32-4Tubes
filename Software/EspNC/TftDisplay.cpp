#include "TftDisplay.h"

TftDisplay::TftDisplay()
    : Adafruit_GFX(LCD_WIDTH, LCD_HEIGHT), _batchDepth(0) {}

void TftDisplay::begin() {
    LCD_Init();
}

void TftDisplay::fillScreen(uint16_t color) {
    fillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

void TftDisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > LCD_WIDTH) {
        w = LCD_WIDTH - x;
    }
    if (y + h > LCD_HEIGHT) {
        h = LCD_HEIGHT - y;
    }
    LCD_fillRect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, color);
}

void TftDisplay::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || y < 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT) {
        return;
    }
    LCD_drawPixel((uint16_t)x, (uint16_t)y, color);
}

void TftDisplay::drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h,
                            uint16_t color) {
    const int16_t byteWidth = (w + 7) / 8;
    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            if (pgm_read_byte(bitmap + (int32_t)j * byteWidth + i / 8) & (0x80 >> (i & 7))) {
                drawPixel(x + i, y + j, color);
            }
        }
    }
}

void TftDisplay::startWrite() {
    if (_batchDepth++ == 0) {
        LCD_beginWrite();
    }
}

void TftDisplay::endWrite() {
    if (_batchDepth == 0) {
        return;
    }
    if (--_batchDepth == 0) {
        LCD_endWrite();
    }
}
