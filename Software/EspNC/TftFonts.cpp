#include "TftFonts.h"
#include "TftCompose.h"

#include <Fonts/FreeSans9pt7b.h>

static const int16_t kScreenW = 320;
static const int16_t kMarginX = 14;
static const int16_t kContentW = kScreenW - 2 * kMarginX;

static int16_t menuFontTopOffset = 0;
static int16_t clockFontTopOffset = 0;
static int16_t menuCaptionWidth = 108;
static int16_t menuFirstRowY = 38;

static int16_t measureTextWidth(TftDisplay& display, const char* text) {
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return (int16_t)w;
}

static void textBoundsAt(TftDisplay& display, const char* text, int16_t x, int16_t y,
                         int16_t* bx, int16_t* by, uint16_t* bw, uint16_t* bh) {
    display.getTextBounds(text, x, y, bx, by, bw, bh);
}

static int16_t highlightBarWidth(TftDisplay& display, int16_t textX, int16_t rowTopY,
                                 const char* text) {
    int16_t bx = 0;
    int16_t by = 0;
    uint16_t bw = 0;
    uint16_t bh = 0;
    const int16_t cursorY = rowTopY - menuFontTopOffset;
    textBoundsAt(display, text, textX, cursorY, &bx, &by, &bw, &bh);
    (void)bx;
    (void)by;
    (void)bh;
    int16_t hlW = (int16_t)(bw + 2);
    const int16_t rowRight = kMarginX + kContentW;
    if (textX > kMarginX + (kContentW / 2)) {
        hlW = (int16_t)(rowRight - textX);
    } else if (textX + hlW > rowRight) {
        hlW = (int16_t)(rowRight - textX);
    }
    if (hlW < 1) {
        hlW = 1;
    }
    return hlW;
}

void tftFontsInit(TftDisplay& display) {
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;

    setMenuFont(display);
    display.getTextBounds("Mg", 0, 0, &x1, &y1, &w, &h);
    menuFontTopOffset = y1;
    menuCaptionWidth = measureTextWidth(display, "Click for settings");
    // Title line, then a small gap before the first menu row (keep compact — row step is taller for descenders).
    menuFirstRowY = (int16_t)(h + 9);

    setClockFont(display);
    display.getTextBounds("0", 0, 0, &x1, &y1, &w, &h);
    clockFontTopOffset = y1;
}

int16_t tftMenuFirstRowY(void) {
    return menuFirstRowY;
}

int16_t tftMenuFontTopOffset(void) {
    return menuFontTopOffset;
}

int16_t tftClockFontTopOffset(void) {
    return clockFontTopOffset;
}

void setMenuFont(TftDisplay& display) {
    display.setFont(&FreeSans9pt7b);
}

void setClockFont(TftDisplay& display) {
    display.setFont(&FreeSans9pt7b);
}

int16_t tftMenuTextWidth(TftDisplay& display, const char* text) {
    setMenuFont(display);
    return measureTextWidth(display, text);
}

int16_t tftClockTextWidth(TftDisplay& display, const char* text) {
    setClockFont(display);
    return measureTextWidth(display, text);
}

int16_t tftMenuCenterX(TftDisplay& display, const char* text) {
    const int16_t w = tftMenuTextWidth(display, text);
    int16_t x = kMarginX + (kContentW - w) / 2;
    if (x < kMarginX) {
        x = kMarginX;
    }
    return x;
}

int16_t tftClockCenterX(TftDisplay& display, const char* text) {
    const int16_t w = tftClockTextWidth(display, text);
    int16_t x = kMarginX + (kContentW - w) / 2;
    if (x < kMarginX) {
        x = kMarginX;
    }
    return x;
}

int16_t tftMenuValueRightX(TftDisplay& display, const char* value) {
    const int16_t w = tftMenuTextWidth(display, value);
    int16_t x = kScreenW - kMarginX - w;
    if (x < kMarginX) {
        x = kMarginX;
    }
    return x;
}

void tftSetMenuCursor(TftDisplay& display, int16_t x, int16_t rowTopY) {
    setMenuFont(display);
    display.setCursor(x, rowTopY - menuFontTopOffset);
}

void tftSetClockCursor(TftDisplay& display, int16_t x, int16_t rowTopY) {
    setClockFont(display);
    display.setCursor(x, rowTopY - clockFontTopOffset);
}

void tftPrintMenuAtInBand(TftDisplay& display, int16_t bandX, int16_t bandW,
                          int16_t textX, int16_t rowTopY, const char* text,
                          bool highlight, uint16_t fgColor, uint16_t bgColor) {
    setMenuFont(display);
    int16_t hlW = 0;
    if (highlight && text != nullptr && text[0] != '\0') {
        hlW = highlightBarWidth(display, textX, rowTopY, text);
    }
    tftComposeTextBand(bandX, rowTopY, bandW, TFT_MENU_ROW_STEP, textX, text, fgColor, bgColor,
                       highlight, hlW, menuFontTopOffset, &FreeSans9pt7b);
}

void tftPrintMenuAt(TftDisplay& display, int16_t x, int16_t rowTopY, const char* text,
                    bool highlight) {
    tftPrintMenuAt(display, x, rowTopY, text, highlight, COLOR_RGB565_WHITE);
}

void tftPrintMenuAt(TftDisplay& display, int16_t x, int16_t rowTopY, const char* text,
                    bool highlight, uint16_t fgColor) {
    setMenuFont(display);
    int16_t bandX = x;
    int16_t bandW;
    int16_t hlW = 0;
    if (highlight && text != nullptr && text[0] != '\0') {
        hlW = highlightBarWidth(display, x, rowTopY, text);
        bandW = hlW;
    } else {
        const int16_t tw = (text != nullptr) ? measureTextWidth(display, text) : 0;
        bandW = (int16_t)(tw + 4);
        if (bandW < 1) {
            bandW = 1;
        }
    }
    const int16_t rowRight = kMarginX + kContentW;
    if (bandX + bandW > rowRight) {
        bandW = (int16_t)(rowRight - bandX);
    }
    tftComposeTextBand(bandX, rowTopY, bandW, TFT_MENU_ROW_STEP, x, text, fgColor,
                       COLOR_RGB565_BLACK, highlight, hlW, menuFontTopOffset, &FreeSans9pt7b);
}

void tftPrintMenuInline(TftDisplay& display, const char* text, bool highlight) {
    setMenuFont(display);
    const int16_t x = display.getCursorX();
    const int16_t baselineY = display.getCursorY();
    const int16_t rowTopY = (int16_t)(baselineY + menuFontTopOffset);
    int16_t hlW = 0;
    int16_t bandW;
    if (highlight && text != nullptr && text[0] != '\0') {
        hlW = highlightBarWidth(display, x, rowTopY, text);
        bandW = hlW;
    } else {
        bandW = (int16_t)(measureTextWidth(display, text) + 4);
        if (bandW < 1) {
            bandW = 1;
        }
    }
    const int16_t rowRight = kMarginX + kContentW;
    if (x + bandW > rowRight) {
        bandW = (int16_t)(rowRight - x);
    }
    tftComposeTextBand(x, rowTopY, bandW, TFT_MENU_ROW_STEP, x, text, COLOR_RGB565_WHITE,
                       COLOR_RGB565_BLACK, highlight, hlW, menuFontTopOffset, &FreeSans9pt7b);
    // Advance cursor like print() would.
    display.setCursor((int16_t)(x + measureTextWidth(display, text)), baselineY);
}

int16_t tftMenuCaptionWidth(void) {
    return menuCaptionWidth;
}
