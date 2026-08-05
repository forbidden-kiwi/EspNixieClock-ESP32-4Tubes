#pragma once

#include "TftDisplay.h"
#include "Display_ST7789.h"

// Layout metrics — FreeSans9pt7b for menus and top clock (TftFonts.cpp).
// Row step must fit ascenders (~12px) + descenders (g/y/p ~5px).
#define TFT_MENU_ROW_STEP       18
#define TFT_CLOCK_ROW_STEP      18

void tftFontsInit(TftDisplay& display);
int16_t tftMenuFirstRowY(void);
int16_t tftMenuFontTopOffset(void);

void setMenuFont(TftDisplay& display);
void setClockFont(TftDisplay& display);

int16_t tftMenuTextWidth(TftDisplay& display, const char* text);
int16_t tftClockTextWidth(TftDisplay& display, const char* text);
int16_t tftMenuCenterX(TftDisplay& display, const char* text);
int16_t tftClockCenterX(TftDisplay& display, const char* text);
int16_t tftMenuValueRightX(TftDisplay& display, const char* value);

void tftSetMenuCursor(TftDisplay& display, int16_t x, int16_t rowTopY);
void tftSetClockCursor(TftDisplay& display, int16_t x, int16_t rowTopY);

void tftPrintMenuAt(TftDisplay& display, int16_t x, int16_t rowTopY, const char* text,
                    bool highlight);
void tftPrintMenuAt(TftDisplay& display, int16_t x, int16_t rowTopY, const char* text,
                    bool highlight, uint16_t fgColor);
// Compose a cleared band then stamp text (no clear-then-draw flash).
void tftPrintMenuAtInBand(TftDisplay& display, int16_t bandX, int16_t bandW,
                          int16_t textX, int16_t rowTopY, const char* text,
                          bool highlight, uint16_t fgColor, uint16_t bgColor);
void tftPrintMenuInline(TftDisplay& display, const char* text, bool highlight);

int16_t tftMenuCaptionWidth(void);
int16_t tftClockFontTopOffset(void);
