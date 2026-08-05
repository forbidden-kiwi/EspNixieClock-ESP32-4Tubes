#pragma once

#include <Arduino.h>
#include <SPI.h>
#include "Globals.h"

// Landscape 320x172 (matches former Adafruit init(172,320) + setRotation(1))
#define LCD_WIDTH   320
#define LCD_HEIGHT  172

#define SPIFreq     80000000UL

#define PIN_LCD_MISO  5
#define PIN_LCD_MOSI  PIN_TFT_MOSI
#define PIN_LCD_SCLK  PIN_TFT_SCLK
#define PIN_LCD_CS    PIN_TFT_CS
#define PIN_LCD_DC    PIN_TFT_DC
#define PIN_LCD_RST   PIN_TFT_RST

// Panel offsets for 172x320 ST7789 in landscape (Adafruit rotation 1)
#define Offset_X    0
#define Offset_Y    34

#define COLOR_RGB565_WHITE  0xFFFF
#define COLOR_RGB565_BLACK  0x0000
// Warm amber highlight bar / accents (RGB 255,186,32)
#define COLOR_RGB565_AMBER  0xFDC4
#define COLOR_RGB565_DIM    0x8410  // gray for disabled / Off
#define COLOR_RGB565_OK     0x07E0  // green for On / connected
#define COLOR_RGB565_WARN   0xFE60  // soft yellow hints
#define COLOR_RGB565_HL_BAR COLOR_RGB565_AMBER
#define COLOR_RGB565_HL_TEXT COLOR_RGB565_BLACK

void LCD_Init(void);
void LCD_SetCursor(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd);
void LCD_addWindow(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd,
                   const uint16_t* color);
void LCD_beginWrite(void);
void LCD_endWrite(void);
void LCD_fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void LCD_drawPixel(uint16_t x, uint16_t y, uint16_t color);
