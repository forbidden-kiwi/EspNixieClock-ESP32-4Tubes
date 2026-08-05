#include "TftCompose.h"

static uint16_t s_scanline[LCD_WIDTH];

uint16_t* tftComposeScanline(void) {
    return s_scanline;
}

void tftComposeFillRange(int16_t x0, int16_t x1Exclusive, uint16_t color) {
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1Exclusive > LCD_WIDTH) {
        x1Exclusive = LCD_WIDTH;
    }
    for (int16_t x = x0; x < x1Exclusive; x++) {
        s_scanline[x] = color;
    }
}

void tftComposeBlitRange(int16_t x0, int16_t y, int16_t x1Exclusive) {
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1Exclusive > LCD_WIDTH) {
        x1Exclusive = LCD_WIDTH;
    }
    if (x1Exclusive <= x0 || y < 0 || y >= LCD_HEIGHT) {
        return;
    }
    LCD_addWindow((uint16_t)x0, (uint16_t)y, (uint16_t)(x1Exclusive - 1), (uint16_t)y,
                  &s_scanline[x0]);
}

void tftComposeFillBand(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) {
        w = (int16_t)(w + x);
        x = 0;
    }
    if (y < 0) {
        h = (int16_t)(h + y);
        y = 0;
    }
    if (x + w > LCD_WIDTH) {
        w = (int16_t)(LCD_WIDTH - x);
    }
    if (y + h > LCD_HEIGHT) {
        h = (int16_t)(LCD_HEIGHT - y);
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    tftComposeFillRange(x, (int16_t)(x + w), color);
    const int16_t x1 = (int16_t)(x + w);
    for (int16_t row = 0; row < h; row++) {
        tftComposeBlitRange(x, (int16_t)(y + row), x1);
    }
}

void tftComposeStampBitmapRow(int16_t destX, int16_t destRelY,
                               const uint8_t* bitmap, int16_t bmpW, int16_t bmpH,
                               uint8_t scale, uint16_t color) {
    if (scale < 1) {
        scale = 1;
    }
    const int16_t srcRow = (int16_t)(destRelY / scale);
    if (srcRow < 0 || srcRow >= bmpH) {
        return;
    }
    const int16_t byteWidth = (int16_t)((bmpW + 7) / 8);
    for (int16_t i = 0; i < bmpW; i++) {
        if (!(pgm_read_byte(bitmap + (int32_t)srcRow * byteWidth + i / 8) & (0x80 >> (i & 7)))) {
            continue;
        }
        const int16_t px = (int16_t)(destX + i * scale);
        for (uint8_t s = 0; s < scale; s++) {
            const int16_t x = (int16_t)(px + s);
            if (x >= 0 && x < LCD_WIDTH) {
                s_scanline[x] = color;
            }
        }
    }
}

void tftComposeStampTextRow(int16_t cursorX, int16_t rowInBand, int16_t fontTopOffset,
                            const GFXfont* font, const char* text, uint16_t fgColor,
                            bool bold, TftComposeGlyphColorFn colorFn, void* colorCtx) {
    if (font == nullptr || text == nullptr) {
        return;
    }
    const int16_t baselineFromBandTop = (int16_t)(-fontTopOffset);
    int16_t x = cursorX;
    int letterIndex = 0;

    for (const char* p = text; *p; p++) {
        const uint8_t c = (uint8_t)*p;
        if (c < font->first || c > font->last) {
            x = (int16_t)(x + pgm_read_byte(&font->glyph[0].xAdvance));
            continue;
        }
        const GFXglyph* glyph = &font->glyph[c - font->first];
        const uint16_t bo = pgm_read_word(&glyph->bitmapOffset);
        const uint8_t gw = pgm_read_byte(&glyph->width);
        const uint8_t gh = pgm_read_byte(&glyph->height);
        const int8_t xa = (int8_t)pgm_read_byte(&glyph->xAdvance);
        const int8_t xo = (int8_t)pgm_read_byte(&glyph->xOffset);
        const int8_t yo = (int8_t)pgm_read_byte(&glyph->yOffset);

        uint16_t fg = fgColor;
        if (c != ' ') {
            if (colorFn != nullptr) {
                fg = colorFn((char)c, letterIndex, colorCtx);
            }
            letterIndex++;
        }

        const int16_t bitRow = (int16_t)(rowInBand - baselineFromBandTop - yo);
        if (bitRow >= 0 && bitRow < (int16_t)gh && gw > 0) {
            const uint8_t* bitmap = font->bitmap;
            const uint32_t bitOffset = (uint32_t)bitRow * gw;
            uint8_t bits = 0;
            for (uint8_t col = 0; col < gw; col++) {
                const uint32_t b = bitOffset + col;
                if ((b & 7) == 0) {
                    bits = pgm_read_byte(bitmap + bo + (b / 8));
                } else if (col == 0) {
                    bits = pgm_read_byte(bitmap + bo + (b / 8));
                    bits <<= (b & 7);
                }
                if (bits & 0x80) {
                    const int16_t px = (int16_t)(x + xo + col);
                    if (px >= 0 && px < LCD_WIDTH) {
                        s_scanline[px] = fg;
                    }
                    if (bold) {
                        const int16_t px2 = (int16_t)(px + 1);
                        if (px2 >= 0 && px2 < LCD_WIDTH) {
                            s_scanline[px2] = fg;
                        }
                    }
                }
                bits <<= 1;
            }
        }
        x = (int16_t)(x + xa);
    }
}

static void composeTextBandInner(int16_t bandX, int16_t rowTopY, int16_t bandW, int16_t bandH,
                                 int16_t textX, const char* text, uint16_t fg, uint16_t bg,
                                 bool highlight, int16_t hlBarW, int16_t fontTopOffset,
                                 const GFXfont* font, TftComposeGlyphColorFn colorFn,
                                 void* colorCtx) {
    if (bandW <= 0 || bandH <= 0) {
        return;
    }
    if (bandX < 0) {
        bandW = (int16_t)(bandW + bandX);
        bandX = 0;
    }
    if (bandX + bandW > LCD_WIDTH) {
        bandW = (int16_t)(LCD_WIDTH - bandX);
    }
    if (bandW <= 0) {
        return;
    }

    const int16_t bandX1 = (int16_t)(bandX + bandW);
        const uint16_t textFg = highlight ? COLOR_RGB565_HL_TEXT : fg;
        const bool bold = highlight;
        const bool useColors = !highlight && colorFn != nullptr;

        int16_t hlW = hlBarW;
        if (highlight) {
            if (hlW <= 0) {
                hlW = (int16_t)(bandX1 - textX);
            }
            if (hlW < 1) {
                hlW = 1;
            }
        }

        for (int16_t row = 0; row < bandH; row++) {
            const int16_t y = (int16_t)(rowTopY + row);
            if (y < 0 || y >= LCD_HEIGHT) {
                continue;
            }
            tftComposeFillRange(bandX, bandX1, bg);
            if (highlight) {
                const int16_t hx0 = (textX < bandX) ? bandX : textX;
                const int16_t hx1 = (int16_t)(textX + hlW);
                tftComposeFillRange(hx0, (hx1 > bandX1) ? bandX1 : hx1, COLOR_RGB565_HL_BAR);
            }
        if (text != nullptr && text[0] != '\0') {
            tftComposeStampTextRow(textX, row, fontTopOffset, font, text, textFg, bold,
                                   useColors ? colorFn : nullptr, colorCtx);
        }
        tftComposeBlitRange(bandX, y, bandX1);
    }
}

void tftComposeTextBand(int16_t bandX, int16_t rowTopY, int16_t bandW, int16_t bandH,
                        int16_t textX, const char* text, uint16_t fg, uint16_t bg,
                        bool highlight, int16_t hlBarW, int16_t fontTopOffset,
                        const GFXfont* font) {
    composeTextBandInner(bandX, rowTopY, bandW, bandH, textX, text, fg, bg, highlight,
                         hlBarW, fontTopOffset, font, nullptr, nullptr);
}

void tftComposeTextBandColored(int16_t bandX, int16_t rowTopY, int16_t bandW, int16_t bandH,
                               int16_t textX, const char* text, uint16_t fg, uint16_t bg,
                               bool highlight, int16_t hlBarW, int16_t fontTopOffset,
                               const GFXfont* font, TftComposeGlyphColorFn colorFn,
                               void* colorCtx) {
    composeTextBandInner(bandX, rowTopY, bandW, bandH, textX, text, fg, bg, highlight,
                         hlBarW, fontTopOffset, font, colorFn, colorCtx);
}
