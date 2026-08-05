#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "Display_ST7789.h"

// Shared scanline (absolute screen X). Compose final pixels, then blit once per row.
uint16_t* tftComposeScanline(void);

void tftComposeFillRange(int16_t x0, int16_t x1Exclusive, uint16_t color);
void tftComposeBlitRange(int16_t x0, int16_t y, int16_t x1Exclusive);

void tftComposeFillBand(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

// Stamp 1bpp GFX bitmap for one destination scanline (supports integer scale).
// destRelY is Y relative to the top of the scaled bitmap.
void tftComposeStampBitmapRow(int16_t destX, int16_t destRelY,
                               const uint8_t* bitmap, int16_t bmpW, int16_t bmpH,
                               uint8_t scale, uint16_t color);

// Optional per-glyph FG for non-space letters (letterIndex counts non-spaces only).
typedef uint16_t (*TftComposeGlyphColorFn)(char ch, int letterIndex, void* ctx);

// Stamp FreeSans (or any GFXfont) for one row inside a text band.
// fontTopOffset is getTextBounds y1 (typically negative); rowInBand is Y - rowTopY.
void tftComposeStampTextRow(int16_t cursorX, int16_t rowInBand, int16_t fontTopOffset,
                            const GFXfont* font, const char* text, uint16_t fgColor,
                            bool bold, TftComposeGlyphColorFn colorFn, void* colorCtx);

// Fill band with bg, optional white highlight bar (hlBarW; 0 = to band end), then stamp text.
void tftComposeTextBand(int16_t bandX, int16_t rowTopY, int16_t bandW, int16_t bandH,
                        int16_t textX, const char* text, uint16_t fg, uint16_t bg,
                        bool highlight, int16_t hlBarW, int16_t fontTopOffset,
                        const GFXfont* font);

// Like tftComposeTextBand but per-letter colors when not highlighting.
void tftComposeTextBandColored(int16_t bandX, int16_t rowTopY, int16_t bandW, int16_t bandH,
                               int16_t textX, const char* text, uint16_t fg, uint16_t bg,
                               bool highlight, int16_t hlBarW, int16_t fontTopOffset,
                               const GFXfont* font, TftComposeGlyphColorFn colorFn,
                               void* colorCtx);
