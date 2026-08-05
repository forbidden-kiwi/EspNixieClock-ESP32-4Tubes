#include "Display_ST7789.h"

#define SPI_WRITE(_dat)      SPI.transfer(_dat)
#define SPI_WRITE_Word(_dat) SPI.transfer16(_dat)

// MADCTL: MY | MV | RGB — same as Adafruit ST7789 rotation 1 on 172x320 panel
#define MADCTL_LANDSCAPE  0xA0

static uint8_t lcdBatchNest = 0;

static void lcdSpiBegin() {
    if (lcdBatchNest == 0) {
        SPI.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
    }
}

static void lcdSpiEnd() {
    if (lcdBatchNest == 0) {
        SPI.endTransaction();
    }
}

static void LCD_WriteCommand(uint8_t cmd) {
    lcdSpiBegin();
    digitalWrite(PIN_LCD_CS, LOW);
    digitalWrite(PIN_LCD_DC, LOW);
    SPI_WRITE(cmd);
    digitalWrite(PIN_LCD_CS, HIGH);
    lcdSpiEnd();
}

static void LCD_WriteData(uint8_t data) {
    lcdSpiBegin();
    digitalWrite(PIN_LCD_CS, LOW);
    digitalWrite(PIN_LCD_DC, HIGH);
    SPI_WRITE(data);
    digitalWrite(PIN_LCD_CS, HIGH);
    lcdSpiEnd();
}

static void LCD_WriteData_Word(uint16_t data) {
    lcdSpiBegin();
    digitalWrite(PIN_LCD_CS, LOW);
    digitalWrite(PIN_LCD_DC, HIGH);
    SPI_WRITE_Word(data);
    digitalWrite(PIN_LCD_CS, HIGH);
    lcdSpiEnd();
}

static void LCD_WriteData_nbyte(const uint8_t* setData, uint8_t* readData, uint32_t size) {
    lcdSpiBegin();
    digitalWrite(PIN_LCD_CS, LOW);
    digitalWrite(PIN_LCD_DC, HIGH);
    SPI.transferBytes(setData, readData, size);
    digitalWrite(PIN_LCD_CS, HIGH);
    lcdSpiEnd();
}

static void SPI_Init() {
    SPI.begin(PIN_LCD_SCLK, PIN_LCD_MISO, PIN_LCD_MOSI);
}

static void LCD_Reset() {
    digitalWrite(PIN_LCD_CS, LOW);
    delay(50);
    digitalWrite(PIN_LCD_RST, LOW);
    delay(50);
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(50);
}

void LCD_Init(void) {
    pinMode(PIN_LCD_CS, OUTPUT);
    pinMode(PIN_LCD_DC, OUTPUT);
    pinMode(PIN_LCD_RST, OUTPUT);

    SPI_Init();
    LCD_Reset();

    LCD_WriteCommand(0x11);
    delay(120);

    LCD_WriteCommand(0x36);
    LCD_WriteData(MADCTL_LANDSCAPE);

    LCD_WriteCommand(0x3A);
    LCD_WriteData(0x05);

    LCD_WriteCommand(0xB0);
    LCD_WriteData(0x00);
    LCD_WriteData(0xE8);

    LCD_WriteCommand(0xB2);
    LCD_WriteData(0x0C);
    LCD_WriteData(0x0C);
    LCD_WriteData(0x00);
    LCD_WriteData(0x33);
    LCD_WriteData(0x33);

    LCD_WriteCommand(0xB7);
    LCD_WriteData(0x35);

    LCD_WriteCommand(0xBB);
    LCD_WriteData(0x35);

    LCD_WriteCommand(0xC0);
    LCD_WriteData(0x2C);

    LCD_WriteCommand(0xC2);
    LCD_WriteData(0x01);

    LCD_WriteCommand(0xC3);
    LCD_WriteData(0x13);

    LCD_WriteCommand(0xC4);
    LCD_WriteData(0x20);

    LCD_WriteCommand(0xC6);
    LCD_WriteData(0x0F);

    LCD_WriteCommand(0xD0);
    LCD_WriteData(0xA4);
    LCD_WriteData(0xA1);

    LCD_WriteCommand(0xD6);
    LCD_WriteData(0xA1);

    LCD_WriteCommand(0xE0);
    LCD_WriteData(0xF0);
    LCD_WriteData(0x00);
    LCD_WriteData(0x04);
    LCD_WriteData(0x04);
    LCD_WriteData(0x04);
    LCD_WriteData(0x05);
    LCD_WriteData(0x29);
    LCD_WriteData(0x33);
    LCD_WriteData(0x3E);
    LCD_WriteData(0x38);
    LCD_WriteData(0x12);
    LCD_WriteData(0x12);
    LCD_WriteData(0x28);
    LCD_WriteData(0x30);

    LCD_WriteCommand(0xE1);
    LCD_WriteData(0xF0);
    LCD_WriteData(0x07);
    LCD_WriteData(0x0A);
    LCD_WriteData(0x0D);
    LCD_WriteData(0x0B);
    LCD_WriteData(0x07);
    LCD_WriteData(0x28);
    LCD_WriteData(0x33);
    LCD_WriteData(0x3E);
    LCD_WriteData(0x36);
    LCD_WriteData(0x14);
    LCD_WriteData(0x14);
    LCD_WriteData(0x29);
    LCD_WriteData(0x32);

    LCD_WriteCommand(0x21);

    LCD_WriteCommand(0x11);
    delay(120);
    LCD_WriteCommand(0x29);
}

void LCD_SetCursor(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd) {
    LCD_WriteCommand(0x2A);
    LCD_WriteData(xStart >> 8);
    LCD_WriteData(xStart + Offset_X);
    LCD_WriteData(xEnd >> 8);
    LCD_WriteData(xEnd + Offset_X);

    LCD_WriteCommand(0x2B);
    LCD_WriteData(yStart >> 8);
    LCD_WriteData(yStart + Offset_Y);
    LCD_WriteData(yEnd >> 8);
    LCD_WriteData(yEnd + Offset_Y);

    LCD_WriteCommand(0x2C);
}

void LCD_addWindow(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd,
                   const uint16_t* color) {
    const uint16_t showWidth = xEnd - xStart + 1;
    const uint16_t showHeight = yEnd - yStart + 1;
    const uint32_t numBytes = (uint32_t)showWidth * showHeight * sizeof(uint16_t);

    LCD_SetCursor(xStart, yStart, xEnd, yEnd);
    LCD_WriteData_nbyte((const uint8_t*)color, nullptr, numBytes);
}

void LCD_beginWrite(void) {
    if (lcdBatchNest == 0) {
        SPI.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
    }
    lcdBatchNest++;
}

void LCD_endWrite(void) {
    if (lcdBatchNest == 0) {
        return;
    }
    lcdBatchNest--;
    if (lcdBatchNest == 0) {
        SPI.endTransaction();
    }
}

void LCD_fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (w == 0 || h == 0) {
        return;
    }
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) {
        return;
    }
    if (x + w > LCD_WIDTH) {
        w = LCD_WIDTH - x;
    }
    if (y + h > LCD_HEIGHT) {
        h = LCD_HEIGHT - y;
    }

    static uint16_t lineBuf[LCD_WIDTH];
    for (uint16_t i = 0; i < w; i++) {
        lineBuf[i] = color;
    }

    const uint16_t xEnd = x + w - 1;
    for (uint16_t row = 0; row < h; row++) {
        LCD_addWindow(x, y + row, xEnd, y + row, lineBuf);
    }
}

void LCD_drawPixel(uint16_t x, uint16_t y, uint16_t color) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) {
        return;
    }
    LCD_addWindow(x, y, x, y, &color);
}
