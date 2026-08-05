/*
 * ESP Nixie Clock
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License <http://www.gnu.org/licenses/> for more details.
 */

#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include "TftDisplay.h"
#include "TftFonts.h"
#include "TftCompose.h"
#include <Fonts/FreeSans9pt7b.h>
#include <avdweb_Switch.h>
#include <ALib0.h>

#include "NixieDisplay.h"
#include "HvSupply.h"
#include "GlobeImages.h"
#include "Globals.h"
#include "Ws2812Control.h"
#include "WifiProvision.h"

// WiFi configuration
#define AP_NAME "NixieClock"
#define AP_PASSWORD "password"

// TFT landscape after setRotation(1): 320 x 172
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 172
// Inset for rounded panel corners (visible area is smaller than full framebuffer)
#define DISP_MARGIN_X 14
#define DISP_MARGIN_Y 8
#define CONTENT_W (SCREEN_WIDTH - 2 * DISP_MARGIN_X)
#define menuRowY(row) ((int16_t)(DISP_MARGIN_Y + tftMenuFirstRowY() + (row) * TFT_MENU_ROW_STEP))

#define COLOR_FG COLOR_RGB565_WHITE
#define COLOR_BG COLOR_RGB565_BLACK
#define COLOR_ACCENT COLOR_RGB565_AMBER
#define COLOR_DIM COLOR_RGB565_DIM
#define COLOR_ON COLOR_RGB565_OK
#define COLOR_OFF COLOR_RGB565_DIM

static inline int16_t menuLabelX() {
    return DISP_MARGIN_X;
}

static inline int16_t logoOriginX(int blockW) {
    int16_t x = (SCREEN_WIDTH - blockW) / 2;
    if (x < DISP_MARGIN_X) {
        x = DISP_MARGIN_X;
    }
    if (x + blockW > SCREEN_WIDTH - DISP_MARGIN_X) {
        x = SCREEN_WIDTH - DISP_MARGIN_X - blockW;
    }
    return x;
}

static bool topStaticDrawn = false;
static bool topClockFrameDrawn = false;

static int lastTftSec = -1;
static int lastTftMin = -1;
static int lastTftHour = -1;
static int lastTftDay = -1;
static int lastTftMonth = -1;
static int lastTftYear = -1;
static int lastTftIsPm = -1;
static bool lastTftSet12_24 = true;
static bool lastTftEnableDst = false;
static bool lastTftDstActive = false;

// Encoder and button pins (Waveshare ESP32-C6 â€” see Globals.h)
const int encoderPinA = PIN_ENCODER_A;
const int encoderPinB = PIN_ENCODER_B;
const int encoderButtonPin = PIN_ENCODER_BUTTON;

// Define when HV5622 drives colon dots (blink menu). Undefine if a separate colon PCB is used.
#define CLOCK_COLON
const int enableBlink_num_state = 4;
const int enableBlink_state[enableBlink_num_state] = {0, 1, 2, 3}; // off, slow, fast, always on

// Timezone DST rule indices
#define TZ_EUROPE 0     // Last Sunday March to last Sunday October
#define TZ_USA 1        // Second Sunday March to first Sunday November
#define TZ_AUSTRALIA 2  // First Sunday October to first Sunday April
#define TZ_NEWZEALAND 3 // Last Sunday September to first Sunday April
#define TZ_CHILE 4      // First Sunday September to first Sunday April

TimeChangeRule dstRules[] = {
    {"DST", Last, Sun, Mar, 2, 0},   // Europe
    {"DST", Second, Sun, Mar, 2, 0}, // USA/Canada
    {"DST", First, Sun, Oct, 2, 0},  // Australia
    {"DST", Last, Sun, Sep, 2, 0},   // New Zealand
    {"DST", First, Sun, Sep, 2, 0},  // Chile
};

TimeChangeRule stdRules[] = {
    {"STD", Last, Sun, Oct, 2, 0},  // Europe
    {"STD", First, Sun, Nov, 2, 0}, // USA/Canada
    {"STD", First, Sun, Apr, 2, 0}, // Australia
    {"STD", First, Sun, Apr, 2, 0}, // New Zealand
    {"STD", First, Sun, Apr, 2, 0}, // Chile
};

// EEPROM layout (bytes 19 and 24 unused / reserved)
const int EEPROM_addr_UTC_offset = 0;
const int EEPROM_addr_DST = 1;
const int EEPROM_addr_12_24 = 2;
const int EEPROM_addr_protect = 3;
const int EEPROM_addr_shutoff_en = 4;
const int EEPROM_addr_shutoff_off = 5;
const int EEPROM_addr_shutoff_on = 6;
const int EEPROM_addr_showzero = 7;
const int EEPROM_addr_blink_colon = 8;
const int EEPROM_addr_screensaver = 9;
const int EEPROM_addr_led = 10;
const int EEPROM_addr_showdate = 11;
const int EEPROM_addr_showdate_second = 12;
const int EEPROM_addr_showdate_interval = 13;
const int EEPROM_addr_showdate_duration = 14;
const int EEPROM_addr_static_color = 15;
const int EEPROM_addr_led_brightness = 16;
const int EEPROM_addr_DST_rule = 17;
const int EEPROM_addr_date_format = 18;
const int EEPROM_addr_ring_mode = 20;
const int EEPROM_addr_ring_brightness = 21;
const int EEPROM_addr_ring_color = 22;
const int EEPROM_addr_ring_magic = 23;
const int EEPROM_addr_roll_down = 25;
const uint8_t RING_EEPROM_MAGIC = 0x51;

bool wifiConnected = false;
uint8_t enableBlink_indx;
uint8_t enableBlink;
NixieDisplay nixie;
HvSupply hv_supply;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "time.nist.gov", 0, 7200000); // update every 2 hours
TimeChangeRule myDST = {"DST", Last, Sun, Mar, 2, 0};
TimeChangeRule mySTD = {"STD", Last, Sun, Oct, 2, 0};
uint8_t currentTimeZone = TZ_EUROPE;
Timezone myTZ(myDST, mySTD);
TftDisplay display;
static bool displayBacklightOn = true;
const int debouncePeriod = 50;
const int longPressPeriod = 3000; // long press toggles tubes on/off
Switch encoderButton = Switch(encoderButtonPin, INPUT_PULLUP, LOW, debouncePeriod, longPressPeriod);
int encoderPos, encoderPosPrev;
// Crossfade flags: false = animation pending; set true when the fade has finished
bool transitionToDate = true;
bool transitionFromDate = true;
uint8_t datePhase = 0;             // 0 = DD/MM or MM/DD, 1 = YYYY
int waitTime = 100; // crossfade step delay (ms)

// Date sequence: hold time per frame is independent of crossfade (~0.8s each)
// Cathode protect ~5s from :00; +2s buffer â†’ start not allowed in 0..6
#define DATE_START_MIN_SEC 7
#define DATE_CROSSFADE_BUDGET_SEC 3

enum DateSeq {
    DATE_IDLE = 0,
    DATE_HOLD1,
    DATE_HOLD2
};
static DateSeq dateSeq = DATE_IDLE;
static uint32_t dateHoldStartMs = 0;
static int lastDateTriggerMinute = -1;

// Menu enumeration
enum Menu {
    TOP,
    SETTINGS1,
    SETTINGS2,
    SETTINGS3,
    SET_UTC_OFFSET,
    DST_MENU,
    ENABLE_DST,
    DST_RULE,
    SET_12_24,
    BLINK_COLON,
    CATHODE_PROTECT,
    AUTO_SHUTOFF,
    AUTO_SHUTOFF_ENABLE,
    AUTO_SHUTOFF_OFFTIME,
    AUTO_SHUTOFF_ONTIME,
    LED_MENU,
    STATIC_COLOR,
    LED_BRIGHTNESS,
    SHOW_ZERO,
    SHOW_DATE,
    SHOW_DATE_MENU,
    SHOW_DATE_SECOND,
    SHOW_DATE_INTERVAL,
    SHOW_DATE_DURATION,
    SHOW_DATE_FORMAT,
    ROLL_DOWN,
    SCREENSAVER_MENU,
    SCREENSAVER,
    SET_TIME,
    SET_TIME_MANUALLY,
    SET_TIME_WIFI,
    RESET_WIFI,
    RING_MENU,
    RING_MODE,
    RING_BRIGHTNESS,
    RING_COLOR
} menu;

static Menu setTimeReturnMenu = SETTINGS3;
static int setTimeReturnPos = 3;
static Menu resetWifiReturnMenu = SETTINGS3;
static int resetWifiReturnPos = 2;

// Configuration
bool enableDST;
bool set12_24;                     // 0 = 12-hour, 1 = 24-hour
bool showZero;
bool showDate;
bool enableRollDown;               // roll digits down when a value decreases
uint8_t ssOption;                  // 0=off, 1=bouncing logo, 2=display off

uint8_t interval_indx;
const int num_intervals = 8;
const int intervals[num_intervals] = {0, 1, 5, 10, 15, 20, 30, 60}; // cathode protect (min)

uint8_t showdate_interval;
uint8_t date_interval_indx;
const int date_num_intervals = 6;
const int date_intervals[date_num_intervals] = {1, 2, 5, 10, 15, 30}; // minutes
uint8_t showdate_duration;         // hold seconds per date frame (not total)
uint8_t date_duration_indx;
const int date_duration_num_intervals = 8;
const int date_duration_intervals[date_duration_num_intervals] = {3, 4, 5, 6, 7, 8, 9, 10}; // sec/frame
uint8_t showdate_second;           // start second (>= DATE_START_MIN_SEC)
uint8_t dateFormat;                // 0=DD:MM, 1=MM:DD
uint8_t date_format_indx;
const int date_format_num_intervals = 2;
const int date_format_intervals[date_format_num_intervals] = {0, 1};

bool enableAutoShutoff;
int autoShutoffOfftime;            // 0..95 = 15-minute slots from midnight
int autoShutoffOntime;
time_t protectTimer = 0;
unsigned long menuLastActivityMs = 0;  // idle timeout uses millis (immune to setTime/NTP jumps)
#define MENU_IDLE_TIMEOUT_MS 60000UL
bool nixieOn = true;               // Nixie tubes on/off state
bool manualOverride = false;       // Manual override for shutoff
bool initProtectionTimer = false;  // Sync protection timer at top of hour
static bool firstEntry = true;
static int setHour = 0, setMinute = 0, setDay = 1, setMonth = 1, setYear = 2023, setAmPm = 0;
static int field = 0;
static const char* amPmLabels[] = {"AM", "PM"};

// Forward declarations (Arduino prototype gen breaks on default args)
void displayTime(bool forceTft = false);
void displayDate();
void updateSelection();
void syncLedRadioMarkFromEffect(void);
static void refreshLedColorCycleLabelAnim(void);
static void printMenuLabelColor(const char* label, int16_t y, bool highlight, uint16_t fg);
static void printMenuLabelRainbow(const char* label, int16_t y, bool highlight, uint8_t hueOffset);
int dateStartMaxSec();
void clampShowdateSecond();
void displayClear();
void setDisplayBacklight(bool on);
void initDisplayBacklight();
void setMenuTextSize();
void setClockTextSize();
void drawTopStatic();
void drawUtcOffsetPreview();
void updateTopTftClock(bool forceFull);
void invalidateTopClock();
void clearMenuRow(int16_t y);
void printMenuTitle(const char* title);
void printMenuValueRight(const char* value, int16_t y, bool highlight);
void printMenuLabel(const char* label, int16_t y, bool highlight);
void fifteenMinToHM(int& hours, int& minutes, int fifteenMin);
void formatShutoffTime(char* buf, size_t buflen, int fifteenMin);
void configModeCallback(ESP_WiFiManager* myWiFiManager);
void resetWiFi();

// Screensaver bitmaps
#define NIXIE_HEIGHT 24
#define NIXIE_WIDTH 17
static const int SS_LOGO_LETTER_PITCH = 18;
static const int SS_LOGO_W = 4 * SS_LOGO_LETTER_PITCH + NIXIE_WIDTH; // 89
static const int SS_BLOCK_H = NIXIE_HEIGHT + 8 + TFT_MENU_ROW_STEP;
static int16_t ssBlockW = 120;

// TOP screen: 2Ã— logo + clock rows below (separate from menu clockRowY)
#define TOP_LOGO_SCALE 2
#define TOP_LOGO_H (NIXIE_HEIGHT * TOP_LOGO_SCALE)
#define TOP_LOGO_PITCH (SS_LOGO_LETTER_PITCH * TOP_LOGO_SCALE)
#define TOP_LOGO_W (4 * TOP_LOGO_PITCH + NIXIE_WIDTH * TOP_LOGO_SCALE)
#define topCaptionRowY() ((int16_t)(SCREEN_HEIGHT - DISP_MARGIN_Y - TFT_MENU_ROW_STEP))
// fromBottom 1 = last visible row, 2 = row above that
#define menuBottomRowY(fromBottom) \
    ((int16_t)(SCREEN_HEIGHT - DISP_MARGIN_Y - (fromBottom) * TFT_MENU_ROW_STEP))

// Vertically center time+date between logo and bottom caption.
static int16_t topClockBaseY(void) {
    const int16_t afterLogo = (int16_t)(DISP_MARGIN_Y + TOP_LOGO_H);
    const int16_t beforeCap = topCaptionRowY();
    const int16_t blockH = (int16_t)(TFT_CLOCK_ROW_STEP * 2);
    int16_t y = (int16_t)(afterLogo + (beforeCap - afterLogo - blockH) / 2);
    if (y < (int16_t)(afterLogo + 4)) {
        y = (int16_t)(afterLogo + 4);
    }
    return y;
}
#define topClockRowY(row) ((int16_t)(topClockBaseY() + (row) * TFT_CLOCK_ROW_STEP))

// Partial highlight clear: labels only (leave right-side values alone).
#define MENU_LABEL_CLEAR_W 160
// Right gutter for values ("rainbow cycle" ~109px + pad).
#define MENU_VALUE_GUTTER_W 120

static const unsigned char PROGMEM NixieTube_bmp[] = {
    B00000000, B10000000, B00000000, // ........#........
    B00000001, B11000000, B00000000, // .......###.......
    B00000001, B01000000, B00000000, // .......#.#.......
    B00000011, B01100000, B00000000, // ......##.##......
    B00000110, B00110000, B00000000, // .....##...##.....
    B00011100, B00011100, B00000000, // ...###.....###...
    B00010000, B00000100, B00000000, // ...#.........#...
    B00010000, B00000100, B00000000, // ...#.........#...
    B00010000, B00000100, B00000000, // ...#.........#...
    B00010000, B00000100, B00000000, // ...#.........#...
    B00010000, B00000100, B00000000, // ...#.........#...
    B00010000, B00000100, B00000000, // ...#.........#...
    B00010000, B00000100, B00000000, // ...#.........#...
    B00010000, B00000100, B00000000, // ...#.........#...
    B00010000, B00000100, B00000000, // ...#.........#...
    B00010000, B00000100, B00000000, // ...#.........#...
    B00010000, B00000100, B00000000, // ...#.........#...
    B00010000, B00000100, B00000000, // ...#.........#...
    B00010000, B00000100, B00000000, // ...#.........#...
    B00010000, B00000100, B00000000, // ...#.........#...
    B00001111, B11111000, B00000000, // ....#########....
    B00000101, B01010000, B00000000, // .....#.#.#.#.....
    B00000101, B01010000, B00000000, // .....#.#.#.#.....
    B00000101, B01010000, B00000000 // .....#.#.#.#.....
};

static const unsigned char PROGMEM NixieN_letter_bmp[] = {
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000001, B00100000, B00000000, // .......#..#......
    B00000011, B00100000, B00000000, // ......##..#......
    B00000011, B00100000, B00000000, // ......##..#......
    B00000011, B00100000, B00000000, // ......##..#......
    B00000011, B10100000, B00000000, // ......###.#......
    B00000011, B10100000, B00000000, // ......###.#......
    B00000010, B10100000, B00000000, // ......#.#.#......
    B00000010, B11100000, B00000000, // ......#.###......
    B00000010, B11100000, B00000000, // ......#.###......
    B00000010, B01100000, B00000000, // ......#..##......
    B00000010, B01100000, B00000000, // ......#..##......
    B00000010, B01000000, B00000000, // ......#..#.......
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000 // .................
};

static const unsigned char PROGMEM NixieI_letter_bmp[] = {
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000011, B11100000, B00000000, // ......#####......
    B00000001, B11000000, B00000000, // .......###.......
    B00000000, B10000000, B00000000, // ........#........
    B00000000, B10000000, B00000000, // ........#........
    B00000000, B10000000, B00000000, // ........#........
    B00000000, B10000000, B00000000, // ........#........
    B00000000, B10000000, B00000000, // ........#........
    B00000000, B10000000, B00000000, // ........#........
    B00000000, B10000000, B00000000, // ........#........
    B00000000, B10000000, B00000000, // ........#........
    B00000001, B11000000, B00000000, // .......###.......
    B00000011, B11100000, B00000000, // ......#####......
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000 // .................
};

static const unsigned char PROGMEM NixieX_letter_bmp[] = {
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000010, B00100000, B00000000, // ......#...#......
    B00000010, B00100000, B00000000, // ......#...#......
    B00000011, B01100000, B00000000, // ......##.##......
    B00000001, B01000000, B00000000, // .......#.#.......
    B00000001, B11000000, B00000000, // .......###.......
    B00000000, B10000000, B00000000, // ........#........
    B00000000, B10000000, B00000000, // ........#........
    B00000001, B11000000, B00000000, // .......###.......
    B00000001, B01000000, B00000000, // .......#.#.......
    B00000011, B01100000, B00000000, // ......##.##......
    B00000010, B00100000, B00000000, // ......#...#......
    B00000010, B00100000, B00000000, // ......#...#......
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000 // .................
};

static const unsigned char PROGMEM NixieE_letter_bmp[] = {
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000001, B11100000, B00000000, // .......####......
    B00000011, B11000000, B00000000, // ......####.......
    B00000011, B00000000, B00000000, // ......##.........
    B00000011, B00000000, B00000000, // ......##.........
    B00000011, B00000000, B00000000, // ......##.........
    B00000011, B11000000, B00000000, // ......####.......
    B00000011, B10000000, B00000000, // ......###........
    B00000011, B00000000, B00000000, // ......##.........
    B00000011, B00000000, B00000000, // ......##.........
    B00000011, B00000000, B00000000, // ......##.........
    B00000011, B11000000, B00000000, // ......####.......
    B00000001, B11100000, B00000000, // .......####......
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000, // .................
    B00000000, B00000000, B00000000 // .................
};

// Stamp shared tube outline, then amber letter glyph on top.
static void stampNixieLogoCell(int16_t x, int16_t row, const unsigned char* letterBmp,
                               int16_t scale, uint16_t tubeColor, uint16_t letterColor) {
    tftComposeStampBitmapRow(x, row, NixieTube_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, scale, tubeColor);
    tftComposeStampBitmapRow(x, row, letterBmp, NIXIE_WIDTH, NIXIE_HEIGHT, scale, letterColor);
}

static void stampNixieLogoWord(int16_t logoX, int16_t row, int16_t pitch, int16_t scale,
                               uint16_t tubeColor, uint16_t letterColor) {
    stampNixieLogoCell(logoX + 0 * pitch, row, NixieN_letter_bmp, scale, tubeColor, letterColor);
    stampNixieLogoCell(logoX + 1 * pitch, row, NixieI_letter_bmp, scale, tubeColor, letterColor);
    stampNixieLogoCell(logoX + 2 * pitch, row, NixieX_letter_bmp, scale, tubeColor, letterColor);
    stampNixieLogoCell(logoX + 3 * pitch, row, NixieI_letter_bmp, scale, tubeColor, letterColor);
    stampNixieLogoCell(logoX + 4 * pitch, row, NixieE_letter_bmp, scale, tubeColor, letterColor);
}

static void drawDstGlobe(uint8_t tzIndex) {
    if (tzIndex > TZ_CHILE) {
        tzIndex = TZ_EUROPE;
    }
    const unsigned char* bmp = kDstGlobeBitmaps[tzIndex];
    const int16_t gx = (int16_t)(SCREEN_WIDTH - DISP_MARGIN_X - GLOBE_BMP_W);
    const int16_t gy = (int16_t)(SCREEN_HEIGHT - DISP_MARGIN_Y - GLOBE_BMP_H);
    const int16_t gx1 = (int16_t)(gx + GLOBE_BMP_W);
    // Leave the header row untouched so "SELECT DST RULE" is not redrawn over the globe.
    const int16_t titleY1 = (int16_t)(DISP_MARGIN_Y + TFT_MENU_ROW_STEP);
    const int16_t spareX1 =
        (int16_t)(menuLabelX() + tftMenuTextWidth(display, "SELECT DST RULE") + 6);

    for (int16_t row = 0; row < GLOBE_BMP_H; row++) {
        const int16_t sy = (int16_t)(gy + row);
        int16_t x0 = gx;
        if (sy < titleY1 && spareX1 > gx) {
            x0 = spareX1;
            if (x0 >= gx1) {
                continue;
            }
        }
        tftComposeFillRange(x0, gx1, COLOR_BG);
        tftComposeStampBitmapRow(gx, row, bmp, GLOBE_BMP_W, GLOBE_BMP_H, 1, COLOR_FG);
        // Blit only outside the spared top-left corner (header pixels stay as-is).
        tftComposeBlitRange(x0, sy, gx1);
    }
}

void displayClear() {
    display.fillScreen(COLOR_BG);
}

void setMenuTextSize() {
    setMenuFont(display);
}

void setClockTextSize() {
    setClockFont(display);
}

void clearMenuRow(int16_t y) {
    tftComposeFillBand(DISP_MARGIN_X, y, CONTENT_W, TFT_MENU_ROW_STEP, COLOR_BG);
}

static void clearMenuLabelArea(int16_t y) {
    tftComposeFillBand(DISP_MARGIN_X, y, MENU_LABEL_CLEAR_W, TFT_MENU_ROW_STEP, COLOR_BG);
}

static void composeMenuLineCenteredColor(int16_t y, const char* text, uint16_t fg) {
    tftComposeTextBand(DISP_MARGIN_X, y, CONTENT_W, TFT_MENU_ROW_STEP,
                       tftMenuCenterX(display, text), text, fg, COLOR_BG, false, 0,
                       tftMenuFontTopOffset(), &FreeSans9pt7b);
}

static void composeMenuLineCentered(int16_t y, const char* text) {
    composeMenuLineCenteredColor(y, text, COLOR_FG);
}

static void composeMenuLineLeftColor(int16_t y, const char* text, uint16_t fg) {
    tftComposeTextBand(DISP_MARGIN_X, y, CONTENT_W, TFT_MENU_ROW_STEP,
                       menuLabelX(), text, fg, COLOR_BG, false, 0,
                       tftMenuFontTopOffset(), &FreeSans9pt7b);
}

static void composeMenuLineLeft(int16_t y, const char* text) {
    composeMenuLineLeftColor(y, text, COLOR_FG);
}

// Stamp one editable field into the current scanline; advances *x by text width.
static void composeStampField(int16_t* x, int16_t rowInBand, const char* text, bool highlight) {
    const int16_t tw = tftMenuTextWidth(display, text);
    if (highlight) {
        const int16_t hlW = (int16_t)(tw + 2);
        tftComposeFillRange(*x, (int16_t)(*x + hlW), COLOR_RGB565_HL_BAR);
        tftComposeStampTextRow(*x, rowInBand, tftMenuFontTopOffset(), &FreeSans9pt7b, text,
                               COLOR_RGB565_HL_TEXT, true, nullptr, nullptr);
    } else {
        tftComposeStampTextRow(*x, rowInBand, tftMenuFontTopOffset(), &FreeSans9pt7b, text,
                               COLOR_FG, false, nullptr, nullptr);
    }
    *x = (int16_t)(*x + tw);
}

// Right-side value that always draws (ignores nav-scroll skip flags).
static void printMenuValueRightAlwaysColor(const char* value, int16_t y, bool highlight,
                                           uint16_t fgColor) {
    // Clear through the right margin â€” "*" phantoms otherwise remain past CONTENT_W.
    const int16_t gx = (int16_t)(SCREEN_WIDTH - DISP_MARGIN_X - MENU_VALUE_GUTTER_W);
    const int16_t gw = (int16_t)(SCREEN_WIDTH - gx);
    if (value == nullptr || value[0] == '\0') {
        tftComposeFillBand(gx, y, gw, TFT_MENU_ROW_STEP, COLOR_BG);
        return;
    }
    tftPrintMenuAtInBand(display, gx, gw, tftMenuValueRightX(display, value), y, value,
                         highlight, fgColor, COLOR_BG);
}

static void printMenuValueRightAlways(const char* value, int16_t y, bool highlight) {
    printMenuValueRightAlwaysColor(value, y, highlight, COLOR_FG);
}

static constexpr uint16_t rgb888To565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Map WS2812 RGB to TFT text color: full-scale chroma, with a violet bias so
// blue-heavy hues (violet/purple) do not collapse to plain blue on ST7789.
static uint16_t ws2812To565(uint32_t c) {
    uint8_t r = (uint8_t)((c >> 16) & 0xFF);
    uint8_t g = (uint8_t)((c >> 8) & 0xFF);
    uint8_t b = (uint8_t)(c & 0xFF);
    uint8_t mx = r;
    if (g > mx) {
        mx = g;
    }
    if (b > mx) {
        mx = b;
    }
    if (mx == 0) {
        return COLOR_FG;
    }
    // Normalize to full brightness for readability on black.
    r = (uint8_t)((uint16_t)r * 255 / mx);
    g = (uint8_t)((uint16_t)g * 255 / mx);
    b = (uint8_t)((uint16_t)b * 255 / mx);

    // Blue-dominant with a red foot â†’ push toward violet/purple on the panel.
    if (b >= 200 && g < 60 && r > 15 && r < 160) {
        const int boost = 70 + ((160 - (int)r) / 2);
        int nr = (int)r + boost;
        int nb = (int)b - 30;
        if (nr > 255) {
            nr = 255;
        }
        if (nb < 170) {
            nb = 170;
        }
        r = (uint8_t)nr;
        b = (uint8_t)nb;
    }
    return rgb888To565(r, g, b);
}

static const char* const kStaticColorNames[] = {
    "red", "vermilion", "orange", "amber",
    "yellow", "chartreuse", "green", "teal",
    "blue", "violet", "purple", "magenta"
};

// Hand-tuned TFT colors (distinct on black). WS2812 LEDs keep staticColors[].
static const uint16_t kStaticColorTft565[] = {
    rgb888To565(255, 48, 48),   // red
    rgb888To565(255, 72, 24),   // vermilion
    rgb888To565(255, 140, 0),   // orange
    rgb888To565(255, 186, 32),  // amber
    rgb888To565(255, 228, 48),  // yellow
    rgb888To565(168, 255, 40),  // chartreuse
    rgb888To565(32, 255, 72),   // green
    rgb888To565(0, 210, 220),   // teal
    rgb888To565(48, 120, 255),  // blue (slight cyan so violet stays distinct)
    rgb888To565(168, 72, 255),  // violet
    rgb888To565(210, 48, 255),  // purple
    rgb888To565(255, 40, 140),  // magenta
};

struct RainbowColorCtx {
    uint8_t hueOffset;
    int colored;
};

static uint16_t rainbowGlyphColor(char ch, int letterIndex, void* ctx) {
    (void)ch;
    RainbowColorCtx* c = (RainbowColorCtx*)ctx;
    uint8_t hue = (uint8_t)(c->hueOffset + (letterIndex * 256) / c->colored);
    return ws2812To565(Wheel(hue));
}

// colorIndex: 0..11 static palette, 12 rainbow, 13 rainbow cycle
static void printRainbowTextRight(const char* text, int16_t y, bool highlight,
                                  uint8_t hueOffset) {
    const int16_t gx = (int16_t)(SCREEN_WIDTH - DISP_MARGIN_X - MENU_VALUE_GUTTER_W);
    const int16_t gw = (int16_t)(SCREEN_WIDTH - gx);
    if (text == nullptr || text[0] == '\0') {
        tftComposeFillBand(gx, y, gw, TFT_MENU_ROW_STEP, COLOR_BG);
        return;
    }
    if (highlight) {
        tftPrintMenuAtInBand(display, gx, gw, tftMenuValueRightX(display, text), y, text,
                             true, COLOR_FG, COLOR_BG);
        return;
    }

    const int len = (int)strlen(text);
    int colored = 0;
    for (int i = 0; i < len; i++) {
        if (text[i] != ' ') {
            colored++;
        }
    }
    if (colored < 1) {
        colored = 1;
    }
    RainbowColorCtx ctx = {hueOffset, colored};
    tftComposeTextBandColored(gx, y, gw, TFT_MENU_ROW_STEP,
                              tftMenuValueRightX(display, text), text, COLOR_FG, COLOR_BG,
                              false, 0, tftMenuFontTopOffset(), &FreeSans9pt7b,
                              rainbowGlyphColor, &ctx);
}

static void printColorNameRight(uint8_t colorIndex, int16_t y, bool highlight) {
    if (colorIndex < (uint8_t)static_color_num_colors) {
        printMenuValueRightAlwaysColor(kStaticColorNames[colorIndex], y, highlight,
                                       kStaticColorTft565[colorIndex]);
        return;
    }
    if (colorIndex == RING_COLOR_RAINBOW) {
        printRainbowTextRight("rainbow", y, highlight, 0);
        return;
    }
    if (colorIndex == RING_COLOR_RAINBOW_CYCLE) {
        // Phase shifts with time so the spectrum slowly crawls while editing/viewing.
        printRainbowTextRight("rainbow cycle", y, highlight, (uint8_t)((millis() / 40) & 255));
        return;
    }
    printMenuValueRightAlways("", y, false);
}

void invalidateTopClock() {
    topClockFrameDrawn = false;
    lastTftSec = -1;
    lastTftMin = -1;
    lastTftHour = -1;
    lastTftDay = -1;
    lastTftMonth = -1;
    lastTftYear = -1;
    lastTftIsPm = -1;
}

static void drawTopDateLineAt(int16_t rowY) {
    static const char* const monthNames[] = {
        "Jan", "Feb", "March", "April", "May", "June",
        "July", "Aug", "Sept", "Oct", "Nov", "Dec"
    };
    char dateStr[20];
    snprintf(dateStr, sizeof(dateStr), "%s %d, %d",
             monthNames[month() - 1], day(), year());
    setClockTextSize();
    tftComposeTextBand(DISP_MARGIN_X, rowY, CONTENT_W, TFT_CLOCK_ROW_STEP,
                       tftClockCenterX(display, dateStr), dateStr, COLOR_FG, COLOR_BG,
                       false, 0, tftClockFontTopOffset(), &FreeSans9pt7b);
}

static void drawTopTimeLineAt(int16_t rowY) {
    char timeStr[28];
    const bool dstActive = enableDST && myTZ.utcIsDST(myTZ.toUTC(now()));
    if (set12_24) {
        if (enableDST) {
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d %s",
                     hour(), minute(), second(), dstActive ? "DST" : "STD");
        } else {
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
                     hour(), minute(), second());
        }
    } else if (enableDST) {
        snprintf(timeStr, sizeof(timeStr), "%2d:%02d:%02d %s %s",
                 hourFormat12(), minute(), second(),
                 isPM() ? "PM" : "AM", dstActive ? "DST" : "STD");
    } else {
        snprintf(timeStr, sizeof(timeStr), "%2d:%02d:%02d %s",
                 hourFormat12(), minute(), second(), isPM() ? "PM" : "AM");
    }
    setClockTextSize();
    tftComposeTextBand(DISP_MARGIN_X, rowY, CONTENT_W, TFT_CLOCK_ROW_STEP,
                       tftClockCenterX(display, timeStr), timeStr, COLOR_FG, COLOR_BG,
                       false, 0, tftClockFontTopOffset(), &FreeSans9pt7b);
}

static void drawTopDateLine() {
    drawTopDateLineAt(topClockRowY(1));
}

void drawUtcOffsetPreview() {
    // One row below the UTCÂ±n line (itself below the title) for clearer spacing.
    drawTopTimeLineAt(menuRowY(2));
    drawTopDateLineAt(menuRowY(3));
}

void updateTopTftClock(bool forceFull) {
    if (menu != TOP) {
        return;
    }

    if (!topStaticDrawn) {
        displayClear();
        drawTopStatic();
    }

    const int hourDisp = set12_24 ? (int)hour() : (int)hourFormat12();
    const int sec = second();
    const int min = minute();
    const int d = day();
    const int mo = month();
    const int y = year();
    const int pm = isPM() ? 1 : 0;
    const bool dstActive = enableDST && myTZ.utcIsDST(myTZ.toUTC(now()));

    const bool layoutChanged = (set12_24 != lastTftSet12_24) || (enableDST != lastTftEnableDst);
    const bool needFull = forceFull || !topClockFrameDrawn || layoutChanged;
    const bool timeChanged = needFull
        || sec != lastTftSec || min != lastTftMin || hourDisp != lastTftHour
        || pm != lastTftIsPm || (enableDST && dstActive != lastTftDstActive);
    const bool dateChanged = needFull
        || d != lastTftDay || mo != lastTftMonth || y != lastTftYear;

    if (timeChanged) {
        drawTopTimeLineAt(topClockRowY(0));
        lastTftSec = sec;
        lastTftMin = min;
        lastTftHour = hourDisp;
        lastTftIsPm = pm;
        lastTftSet12_24 = set12_24;
        lastTftEnableDst = enableDST;
        lastTftDstActive = dstActive;
        topClockFrameDrawn = true;
    }
    if (dateChanged) {
        drawTopDateLine();
        lastTftDay = d;
        lastTftMonth = mo;
        lastTftYear = y;
    }
}

void drawTopStatic() {
    const int logoX = logoOriginX(TOP_LOGO_W);
    const int logoY = DISP_MARGIN_Y;
    const int16_t outH = (int16_t)(NIXIE_HEIGHT * TOP_LOGO_SCALE);
    const int16_t logoRight = (int16_t)(logoX + TOP_LOGO_W);

    display.startWrite();
    for (int16_t row = 0; row < outH; row++) {
        tftComposeFillRange(logoX, logoRight, COLOR_BG);
        stampNixieLogoWord(logoX, row, TOP_LOGO_PITCH, TOP_LOGO_SCALE, COLOR_FG, COLOR_ACCENT);
        tftComposeBlitRange(logoX, (int16_t)(logoY + row), logoRight);
    }
    display.endWrite();

    tftComposeTextBand(DISP_MARGIN_X, topCaptionRowY(), CONTENT_W, TFT_MENU_ROW_STEP,
                       tftMenuCenterX(display, "Click for settings"), "Click for settings",
                       COLOR_FG, COLOR_BG, false, 0, tftMenuFontTopOffset(), &FreeSans9pt7b);
    topStaticDrawn = true;
}

void setDisplayBacklight(bool on) {
    displayBacklightOn = on;
    ledcWrite(PIN_TFT_BL, on ? BL_PWM_DUTY_ON : 0);
}

void initDisplayBacklight() {
    ledcAttach(PIN_TFT_BL, BL_PWM_FREQ_HZ, BL_PWM_RES_BITS);
    setDisplayBacklight(true);
}

void setup() {
    pinMode(encoderPinA, INPUT_PULLUP);
    pinMode(encoderPinB, INPUT_PULLUP);

    // Onboard ST7789 TFT (320x172 landscape)
    initDisplayBacklight();
    display.begin();
    tftFontsInit(display);
    ssBlockW = tftMenuCaptionWidth();
    if (ssBlockW < SS_LOGO_W) {
        ssBlockW = SS_LOGO_W;
    }
    setClockTextSize();
    displayClear();

    composeMenuLineCentered(DISP_MARGIN_Y, "Connecting...");
    composeMenuLineCentered(menuRowY(1), "Cycle Power after a");
    composeMenuLineCentered(menuRowY(2), "few minutes if no");
    composeMenuLineCentered(menuRowY(3), "connection.");

    // Configure WiFi (ESP_WiFiManager MultiWiFi + captive portal)
    wifiConnected = wifiProvisionBegin(AP_NAME, AP_PASSWORD, 180, configModeCallback);

    // Display WiFi connection status
    displayClear();
    if (wifiConnected) {
        const int16_t wifiStatusY = (int16_t)((SCREEN_HEIGHT - TFT_MENU_ROW_STEP) / 2);
        composeMenuLineCenteredColor(wifiStatusY, "Wifi Connected", COLOR_ON);
    } else {
        composeMenuLineLeftColor(DISP_MARGIN_Y, "No Wifi Available", COLOR_RGB565_WARN);
        composeMenuLineLeft(menuRowY(1), "Running without time");
        composeMenuLineLeft(menuRowY(2), "Set Wifi in Settings");
    }
    delay(2000);

    // Start NTP client
    timeClient.begin();

    // Update local time
    displayClear();
    {
        const int16_t statusY = (int16_t)((SCREEN_HEIGHT - TFT_MENU_ROW_STEP) / 2);
        composeMenuLineCentered(statusY, "Updating local time");
    }

    unsigned long ntpStartMs = millis();
    bool ntpOk = false;
    while (!ntpOk && millis() - ntpStartMs < 5000) {
        ntpOk = timeClient.update();
        if (!ntpOk) {
            delay(500);
        }
    }
    const unsigned long ntpElapsedMs = millis() - ntpStartMs;
    if (ntpElapsedMs < 2000UL) {
        delay(2000UL - ntpElapsedMs);
    }

    if (ntpOk && wifiConnected) {
        setTime(myTZ.toLocal(timeClient.getEpochTime()));
    } else {
        setTime(0);
    }

    // Initialize EEPROM
    EEPROM.begin(512);

    // Load settings from EEPROM with validation
    int utc_offset = (((int)EEPROM.read(EEPROM_addr_UTC_offset) + 12) % 24) - 12;

    currentTimeZone = EEPROM.read(EEPROM_addr_DST_rule);
    if (currentTimeZone > TZ_CHILE) currentTimeZone = TZ_EUROPE;

    enableDST = EEPROM.read(EEPROM_addr_DST) != 0;
    if (enableDST > 1) enableDST = 0;

    set12_24 = EEPROM.read(EEPROM_addr_12_24) != 0;
    if (set12_24 > 1) set12_24 = 1;

    // Cathode protect stored as minutes (0,1,5,...), like date interval.
    // Legacy: values not in intervals[] but < num_intervals are treated as index.
    {
        uint8_t rawProtect = EEPROM.read(EEPROM_addr_protect);
        interval_indx = 0;
        bool found = false;
        for (int i = 0; i < num_intervals; i++) {
            if (intervals[i] == (int)rawProtect) {
                interval_indx = i;
                found = true;
                break;
            }
        }
        if (!found && rawProtect < num_intervals) {
            interval_indx = rawProtect;
        }
    }
    
    enableAutoShutoff = EEPROM.read(EEPROM_addr_shutoff_en) != 0;
    if (enableAutoShutoff > 1) enableAutoShutoff = 0;
    autoShutoffOfftime = (int)EEPROM.read(EEPROM_addr_shutoff_off);
    autoShutoffOntime = (int)EEPROM.read(EEPROM_addr_shutoff_on);
    if (autoShutoffOfftime > 95) autoShutoffOfftime = 88; // Default to 22:00
    if (autoShutoffOntime > 95) autoShutoffOntime = 24;   // Default to 06:00

    showZero = EEPROM.read(EEPROM_addr_showzero) != 0;
    if (showZero > 1) showZero = 0;

    showDate = EEPROM.read(EEPROM_addr_showdate) != 0;
    if (showDate > 1) showDate = 0;

    {
        uint8_t rawRollDown = EEPROM.read(EEPROM_addr_roll_down);
        enableRollDown = (rawRollDown == 1);
    }

    // Interval is stored as minutes (1,2,5,...), not as an array index
    {
        uint8_t rawInterval = EEPROM.read(EEPROM_addr_showdate_interval);
        date_interval_indx = 0;
        showdate_interval = date_intervals[0];
        for (int i = 0; i < date_num_intervals; i++) {
            if (date_intervals[i] == rawInterval) {
                date_interval_indx = i;
                showdate_interval = rawInterval;
                break;
            }
        }
    }

    static_color = EEPROM.read(EEPROM_addr_static_color);
    if (static_color >= static_color_num_colors) static_color = 2;
    static_color_indx = static_color;

    showdate_second = EEPROM.read(EEPROM_addr_showdate_second);

    // Duration is hold seconds per frame (3..10), not an array index
    {
        uint8_t rawDuration = EEPROM.read(EEPROM_addr_showdate_duration);
        date_duration_indx = 0;
        showdate_duration = date_duration_intervals[0];
        for (int i = 0; i < date_duration_num_intervals; i++) {
            if (date_duration_intervals[i] == rawDuration) {
                date_duration_indx = i;
                showdate_duration = rawDuration;
                break;
            }
        }
    }
    clampShowdateSecond();

    enableBlink = EEPROM.read(EEPROM_addr_blink_colon);
    if (enableBlink >= enableBlink_num_state) enableBlink = 0;
    enableBlink_indx = enableBlink;

    ssOption = EEPROM.read(EEPROM_addr_screensaver);
    if (ssOption > 2) ssOption = 0;

    LED_effect = EEPROM.read(EEPROM_addr_led);
    if (LED_effect > 14) LED_effect = 1;
    syncLedRadioMarkFromEffect();
    if (LED_effect >= 3 && LED_effect <= 14) {
        static_color_indx = (uint8_t)(LED_effect - 3);
        static_color = static_color_indx;
    }

    uint8_t brightnessIndex = EEPROM.read(EEPROM_addr_led_brightness);
    if (brightnessIndex >= led_brightness_num_intervals) brightnessIndex = 0;
    LedBrightnessPercentage = led_brightness_intervals[brightnessIndex];
    led_brightness_indx = brightnessIndex;

    dateFormat = EEPROM.read(EEPROM_addr_date_format);
    if (dateFormat > 1) dateFormat = 0;

    // First-time seconds ring settings: blank EEPROM often reads as 0
    if (EEPROM.read(EEPROM_addr_ring_magic) != RING_EEPROM_MAGIC) {
        ringMode = RING_MODE_FILL;
        ringBrightnessIndx = 19;
        ringBrightnessPercentage = led_brightness_intervals[ringBrightnessIndx];
        ringColorIndx = 2;
        EEPROM.write(EEPROM_addr_ring_mode, ringMode);
        EEPROM.write(EEPROM_addr_ring_brightness, ringBrightnessIndx);
        EEPROM.write(EEPROM_addr_ring_color, ringColorIndx);
        EEPROM.write(EEPROM_addr_ring_magic, RING_EEPROM_MAGIC);
        EEPROM.commit();
    } else {
        ringMode = EEPROM.read(EEPROM_addr_ring_mode);
        if (ringMode > RING_MODE_DOT) ringMode = RING_MODE_FILL;

        ringBrightnessIndx = EEPROM.read(EEPROM_addr_ring_brightness);
        if (ringBrightnessIndx >= led_brightness_num_intervals) ringBrightnessIndx = 19;
        ringBrightnessPercentage = led_brightness_intervals[ringBrightnessIndx];

        ringColorIndx = EEPROM.read(EEPROM_addr_ring_color);
        if (ringColorIndx >= ring_color_num_options) ringColorIndx = 2;
    }

    // Configure timezone
    mySTD = stdRules[currentTimeZone];
    myDST = dstRules[currentTimeZone];
    mySTD.offset = utc_offset * 60;
    myDST.offset = mySTD.offset;
    if (enableDST) myDST.offset += 60;
    myTZ = Timezone(myDST, mySTD);

    // Initialize components
    initWs2812();
    menu = TOP;
    topStaticDrawn = false;
    invalidateTopClock();
    updateSelection();
    timeClient.update();
    setTime(myTZ.toLocal(timeClient.getEpochTime()));
    hv_supply.begin();
    nixie.begin();
    displayTime();
    menuLastActivityMs = millis();
}

// DVD-logo style bouncing screensaver (NIXIE + caption)
static int16_t ssBounceX = 0;
static int16_t ssBounceY = 0;
static int8_t ssBounceVX = 1;
static int8_t ssBounceVY = 1;
static int16_t ssPrevX = -1;
static int16_t ssPrevY = -1;
static bool ssNeedsFullClear = true;

void resetScreensaverBounce() {
    ssBounceX = logoOriginX(ssBlockW);
    ssBounceY = (SCREEN_HEIGHT - SS_BLOCK_H) / 2;
    if (ssBounceY < DISP_MARGIN_Y) {
        ssBounceY = DISP_MARGIN_Y;
    }
    ssBounceVX = random(2) ? 1 : -1;
    ssBounceVY = random(2) ? 1 : -1;
    ssPrevX = -1;
    ssPrevY = -1;
    ssNeedsFullClear = true;
}

static void ssWriteBlackLine(int16_t x, int16_t y, int16_t w) {
    if (w <= 0) {
        return;
    }
    tftComposeFillBand(x, y, w, 1, COLOR_BG);
}

// Compose one scanline: black bg + NIXIE letters + caption (final pixels, one SPI write).
static void ssWriteContentLine(int16_t bx, int16_t by, int16_t absY, int16_t clearW) {
    const int16_t x1 = (int16_t)(bx + clearW);
    tftComposeFillRange(bx, x1, COLOR_BG);
    const int16_t relY = (int16_t)(absY - by);
    if (relY >= 0 && relY < NIXIE_HEIGHT) {
        const int16_t logoX = (int16_t)(bx + (ssBlockW - SS_LOGO_W) / 2);
        stampNixieLogoWord(logoX, relY, SS_LOGO_LETTER_PITCH, 1, COLOR_FG, COLOR_ACCENT);
    }
    const int16_t textTop = (int16_t)(NIXIE_HEIGHT + 4);
    if (relY >= textTop && relY < (int16_t)(textTop + TFT_MENU_ROW_STEP)) {
        tftComposeStampTextRow(bx, (int16_t)(relY - textTop), tftMenuFontTopOffset(),
                               &FreeSans9pt7b, "Click for settings", COLOR_FG, false,
                               nullptr, nullptr);
    }
    tftComposeBlitRange(bx, absY, x1);
}

void renderScreensaverFrame() {
    // FreeSans last-letter glyphs can overhang past getTextBounds width.
    const int16_t kClearPadR = 4;
    const int16_t clearW = (int16_t)(ssBlockW + kClearPadR);
    const int16_t h = (int16_t)SS_BLOCK_H;

    display.startWrite();
    if (ssNeedsFullClear) {
        displayClear();
        ssNeedsFullClear = false;
        for (int16_t row = 0; row < h; row++) {
            ssWriteContentLine(ssBounceX, ssBounceY, (int16_t)(ssBounceY + row), clearW);
        }
        ssPrevX = ssBounceX;
        ssPrevY = ssBounceY;
        display.endWrite();
        return;
    }

    if (ssPrevX < 0 || (ssPrevX == ssBounceX && ssPrevY == ssBounceY)) {
        display.endWrite();
        return;
    }

    // Top-to-bottom: each SPI line is already the final pixels (no black-then-draw flash).
    const int16_t y0 = (ssPrevY < ssBounceY) ? ssPrevY : ssBounceY;
    const int16_t y1 = (ssPrevY + h > ssBounceY + h) ? (int16_t)(ssPrevY + h) : (int16_t)(ssBounceY + h);
    const int16_t dx = (int16_t)(ssBounceX - ssPrevX);

    for (int16_t y = y0; y < y1; y++) {
        const bool inNew = (y >= ssBounceY) && (y < (int16_t)(ssBounceY + h));
        const bool inOld = (y >= ssPrevY) && (y < (int16_t)(ssPrevY + h));

        if (inNew) {
            if (inOld && dx > 0) {
                ssWriteBlackLine(ssPrevX, y, dx);
            } else if (inOld && dx < 0) {
                ssWriteBlackLine((int16_t)(ssBounceX + clearW), y, (int16_t)(-dx));
            }
            ssWriteContentLine(ssBounceX, ssBounceY, y, clearW);
        } else if (inOld) {
            ssWriteBlackLine(ssPrevX, y, clearW);
        }
    }
    ssPrevX = ssBounceX;
    ssPrevY = ssBounceY;
    display.endWrite();
}

void updateScreensaverBounce() {
    static unsigned long lastMoveMs = 0;
    unsigned long nowMs = millis();
    // ~25 fps â€” scanline updates keep SPI load manageable.
    if ((uint32_t)(nowMs - lastMoveMs) < 40) {
        return;
    }
    lastMoveMs = nowMs;

    ssBounceX += ssBounceVX;
    ssBounceY += ssBounceVY;

    if (ssBounceX <= DISP_MARGIN_X) {
        ssBounceX = DISP_MARGIN_X;
        ssBounceVX = 1;
    } else if (ssBounceX + ssBlockW >= SCREEN_WIDTH - DISP_MARGIN_X) {
        ssBounceX = SCREEN_WIDTH - DISP_MARGIN_X - ssBlockW;
        ssBounceVX = -1;
    }
    if (ssBounceY <= DISP_MARGIN_Y) {
        ssBounceY = DISP_MARGIN_Y;
        ssBounceVY = 1;
    } else if (ssBounceY + SS_BLOCK_H >= SCREEN_HEIGHT - DISP_MARGIN_Y) {
        ssBounceY = SCREEN_HEIGHT - DISP_MARGIN_Y - SS_BLOCK_H;
        ssBounceVY = -1;
    }

    renderScreensaverFrame();
}

void loop() {
    updateEncoderPos();
    encoderButton.poll();

    static bool lastButtonState = false;
    bool currentButtonState = encoderButton.pushed();

    // Long press (3 s): toggle tubes on/off and return to TOP
    if (encoderButton.longPress()) {
        manualOverride = true;
        nixieOn = !nixieOn;
        menu = TOP;
        topStaticDrawn = false;
        invalidateTopClock();
        updateSelection();
    } else if (currentButtonState && !lastButtonState) {
        menuLastActivityMs = millis();
        updateMenu();
    }
    lastButtonState = currentButtonState;

    updateLEDs();

    // Timekeeping is local via TimeLib; NTP only on the 2h interval below.

    // Non-blocking WiFi state machine (no periodic blocking wifiMulti.run)
    wifiProvisionLoop();
    bool wasWifiConnected = wifiConnected;
    wifiConnected = wifiProvisionConnected();

    // NTP every 2 hours. WiFi reconnect only when a sync is due and the link is down â€”
    // async WiFi.begin, not a blocking scan loop.
    static unsigned long lastNtpUpdate = 0;
    static bool ntpReconnectStarted = false;
    const unsigned long ntpUpdateInterval = 7200000UL; // 2 hours
    bool ntpDue = (millis() - lastNtpUpdate >= ntpUpdateInterval);

    if (wifiConnected && (!wasWifiConnected || ntpDue)) {
        if (timeClient.update()) {
            setTime(myTZ.toLocal(timeClient.getEpochTime()));
        }
        lastNtpUpdate = millis();
        ntpReconnectStarted = false;
    } else if (ntpDue && !wifiConnected) {
        if (!ntpReconnectStarted) {
            wifiProvisionRequestReconnect();
            ntpReconnectStarted = true;
        }
        // Finished credential pass without a link â€” wait until next NTP window
        if (ntpReconnectStarted && wifiProvisionReconnectIdle()) {
            lastNtpUpdate = millis();
            ntpReconnectStarted = false;
        }
    }

    // Handle cathode protection (~5s slot from :00). Skip while date sequence runs.
    static time_t lastTriggerMinute = -1;
    if (interval_indx > 0 && interval_indx < num_intervals) {
        if (menu != SET_UTC_OFFSET && menu != ENABLE_DST) {
            int currentMinute = minute();
            int intervalMinutes = intervals[interval_indx];
            if (currentMinute % intervalMinutes == 0 && currentMinute != lastTriggerMinute && second() == 0) {
                if (nixieOn && !nixie.isSlotMachineActive() && dateSeq == DATE_IDLE) {
                    cathodeProtect();
                    lastTriggerMinute = currentMinute;
                }
            }
        }
    }

    // Update slot machine effect every 50ms
    static unsigned long lastUpdate = 0;
    const unsigned long updateInterval = 50;
    if (millis() - lastUpdate >= updateInterval) {
        nixie.updateSlotMachine();
        lastUpdate = millis();
    }

    // Update display based on current state (~20 Hz; was ~1 kHz and heated CPU/panel)
    static unsigned long lastMainTickMs = 0;
    const unsigned long mainTickMs = (dateSeq != DATE_IDLE) ? 40UL : 50UL;
    if ((uint32_t)(millis() - lastMainTickMs) >= mainTickMs) {
        lastMainTickMs = millis();
        evalShutoffTime();

        if (nixie.isSlotMachineActive()) {
            // Tubes driven by slot machine; skip date start for this minute if trigger hits
            if (showDate && showdate_interval > 0 && (minute() % showdate_interval == 0)
                && second() == showdate_second && minute() != lastDateTriggerMinute) {
                lastDateTriggerMinute = minute();
            }
        } else if (dateSeq == DATE_HOLD1) {
            displayDate();
            // Start hold timer only after crossfade finished (taskDelay is cooperative)
            if (transitionToDate) {
                if (dateHoldStartMs == 0) {
                    dateHoldStartMs = millis();
                }
                if ((uint32_t)(millis() - dateHoldStartMs) >= (uint32_t)showdate_duration * 1000UL) {
                    datePhase = 1;
                    transitionToDate = false;
                    dateSeq = DATE_HOLD2;
                    dateHoldStartMs = 0; // set after crossfade to YYYY completes
                    displayDate(); // start cooperative crossfade (resumes on later calls)
                }
            }
        } else if (dateSeq == DATE_HOLD2) {
            displayDate();
            if (transitionToDate) {
                if (dateHoldStartMs == 0) {
                    dateHoldStartMs = millis();
                }
                if ((uint32_t)(millis() - dateHoldStartMs) >= (uint32_t)showdate_duration * 1000UL) {
                    transitionFromDate = false;
                    dateSeq = DATE_IDLE;
                    displayTime(); // crossfade back to HH:MM
                }
            }
        } else {
            // DATE_IDLE: maybe start date, else show time
            bool dateMinute = showDate
                && (showdate_interval > 0)
                && (minute() % showdate_interval == 0);
            int mn = minute();
            if (dateMinute
                && second() == showdate_second
                && mn != lastDateTriggerMinute) {
                lastDateTriggerMinute = mn;
                datePhase = 0;
                transitionToDate = false;
                dateSeq = DATE_HOLD1;
                dateHoldStartMs = 0; // set after crossfade completes
                displayDate(); // start cooperative crossfade (resumes on later calls)
            } else {
                displayTime();
            }
        }

        // Crawl "Color Cycle" label colors while LED options are on screen.
        static unsigned long lastLedLabelAnimMs = 0;
        if ((menu == LED_MENU || menu == STATIC_COLOR || menu == LED_BRIGHTNESS)
            && (uint32_t)(millis() - lastLedLabelAnimMs) >= 80UL) {
            lastLedLabelAnimMs = millis();
            refreshLedColorCycleLabelAnim();
        }
    }

    // Screensaver: backlight off, or DVD-style bouncing NIXIE logo
    if (menu == SCREENSAVER) {
        if (ssOption == 2) {
            setDisplayBacklight(false);
        } else if (ssOption == 1) {
            setDisplayBacklight(true);
            updateScreensaverBounce();
        }
    }

    // Reset to screensaver or TOP menu after 60 seconds of inactivity
    static Menu lastMenu = TOP;
    static time_t lastMenuChange = now();
    if (menu != lastMenu) {
        lastMenuChange = now();
        lastMenu = menu;
    }
    if ((uint32_t)(millis() - menuLastActivityMs) > MENU_IDLE_TIMEOUT_MS && menu != SCREENSAVER) {
        if (ssOption > 0) {
            menu = SCREENSAVER;
            setDisplayBacklight(true);
            if (ssOption == 1) {
                resetScreensaverBounce();
            } else if (ssOption == 2) {
                setDisplayBacklight(false);
            }
            updateSelection();
        } else if (menu != TOP) {
            menu = TOP;
            topStaticDrawn = false;
    invalidateTopClock();
            updateSelection();
        }
    }
}

/**
 * Trigger cathode protection effect
 */
void cathodeProtect() {
    int startHour = set12_24 ? hour() : hourFormat12();
    int startMinute = minute();

    time_t currentTime = myTZ.toLocal(timeClient.getEpochTime());
    time_t endTime = currentTime + 5; // Target time is 5 seconds ahead
    int targetHour = set12_24 ? hour(endTime) : hourFormat12(endTime);
    int targetMinute = minute(endTime);

    nixie.startSlotMachine(startHour, startMinute, targetHour, targetMinute);
}

/**
 * Display current time on Nixie tubes and TFT
 */
void displayTime(bool forceTft) {
    int hour12_24 = set12_24 ? (unsigned char)hour() : (unsigned char)hourFormat12();
    unsigned char hourBcd = decToBcd((unsigned char)hour12_24);
    unsigned char minBcd = decToBcd((unsigned char)minute());
    bool colonBlinkState;

    // Handle colon blinking
    static unsigned long lastSecondChange = 0;
    static int lastSecond = -1;
    int currentSecond = second();
    unsigned long currentMillis = millis();

    if (currentSecond != lastSecond) {
        lastSecondChange = currentMillis;
        lastSecond = currentSecond;
    }

    switch (enableBlink) {
        case 0: colonBlinkState = false; break;
        case 1: colonBlinkState = !(bool)(currentSecond % 2); break;
        case 2: colonBlinkState = (currentMillis - lastSecondChange) < 500; break;
        case 3: colonBlinkState = true; break;
    }

    // Control high voltage supply
    hv_supply.switchOn();

    if (!nixieOn) {
        hourBcd = 255;
        minBcd = 255;
        hv_supply.switchOff();
        // Do not clear LED_effect here â€” updateLEDs() already blanks pixels when
        // !nixieOn, and zeroing would move the LED-menu "*" to Disable.
        colonBlinkState = false;
    }

    // Per-tube roll-down when a digit decreases (independent animations)
    static int8_t lastDigits[4] = {-1, -1, -1, -1};
    static bool rollActive[4] = {false, false, false, false};
    static uint8_t rollFrom[4];
    static uint8_t rollTo[4];
    static unsigned long rollStartMs[4];
    const unsigned long rollStepMs = 35;

    uint8_t targetDigits[4] = {
        (uint8_t)((hour12_24 / 10) % 10),
        (uint8_t)(hour12_24 % 10),
        (uint8_t)((minute() / 10) % 10),
        (uint8_t)(minute() % 10)
    };
    uint8_t showDigits[4];

    if (!nixieOn || transitionFromDate == false) {
        for (int i = 0; i < 4; i++) {
            rollActive[i] = false;
            lastDigits[i] = -1;
            showDigits[i] = targetDigits[i];
        }
    } else {
        for (int i = 0; i < 4; i++) {
            if (!enableRollDown) {
                rollActive[i] = false;
            }
            if (enableRollDown
                && lastDigits[i] >= 0
                && targetDigits[i] < (uint8_t)lastDigits[i]
                && !rollActive[i]) {
                rollActive[i] = true;
                rollFrom[i] = (uint8_t)lastDigits[i];
                rollTo[i] = targetDigits[i];
                rollStartMs[i] = currentMillis;
            }

            if (rollActive[i]) {
                int span = (int)rollFrom[i] - (int)rollTo[i];
                int numValues = span + 1;
                unsigned long elapsed = currentMillis - rollStartMs[i];
                unsigned long totalMs = (unsigned long)numValues * rollStepMs;
                if (span <= 0 || elapsed >= totalMs) {
                    showDigits[i] = rollTo[i];
                    rollActive[i] = false;
                    lastDigits[i] = (int8_t)rollTo[i];
                } else {
                    int idx = (int)(elapsed / rollStepMs);
                    if (idx >= numValues) idx = numValues - 1;
                    showDigits[i] = (uint8_t)((int)rollFrom[i] - idx);
                }
            } else {
                showDigits[i] = targetDigits[i];
                lastDigits[i] = (int8_t)targetDigits[i];
            }
        }
    }

    // Update Nixie tubes with animation (4 tubes: HH:MM)
    if (transitionFromDate == false) {
        taskBegin();
        nixie.disableSegments(hourTens, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(hourUnits, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(minuteTens, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(minuteUnits, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);

        if (!showZero && targetDigits[0] == 0) {
            nixie.disableSegments(hourTens, 10);
        } else {
            nixie.enableSegment(hourTens[targetDigits[0]]);
        }
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.enableSegment(hourUnits[targetDigits[1]]);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.enableSegment(minuteTens[targetDigits[2]]);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.enableSegment(minuteUnits[targetDigits[3]]);
        nixie.updateDisplay();
        taskDelay(waitTime);

        transitionFromDate = true;
        transitionToDate = false;
        taskEnd();
        for (int i = 0; i < 4; i++) {
            lastDigits[i] = (int8_t)targetDigits[i];
            rollActive[i] = false;
        }
    } else {
        nixie.disableAllSegments();
        if (!showZero && showDigits[0] == 0) {
            nixie.disableSegments(hourTens, 10);
        } else {
            nixie.enableSegment(hourTens[showDigits[0]]);
        }
        nixie.enableSegment(hourUnits[showDigits[1]]);
        nixie.enableSegment(minuteTens[showDigits[2]]);
        nixie.enableSegment(minuteUnits[showDigits[3]]);
        transitionToDate = false;
    }

    // Left colon between HH and MM only (seconds separator dots removed)
    nixie.disableSegment(UpperRightDot);
    nixie.disableSegment(LowerRightDot);
    if (!colonBlinkState || enableBlink == 0) {
        nixie.disableSegment(UpperLeftDot);
        nixie.disableSegment(LowerLeftDot);
    } else {
        nixie.enableSegment(UpperLeftDot);
        nixie.enableSegment(LowerLeftDot);
    }

    nixie.updateDisplay();

    if (menu == TOP) {
        updateTopTftClock(forceTft);
    }
}

/**
 * Latest allowed start second so both frame holds + crossfade budget fit in the minute.
 */
int dateStartMaxSec() {
    int maxStart = 60 - (2 * (int)showdate_duration + DATE_CROSSFADE_BUDGET_SEC);
    if (maxStart < DATE_START_MIN_SEC) {
        maxStart = DATE_START_MIN_SEC;
    }
    return maxStart;
}

/**
 * Clamp showdate_second into [DATE_START_MIN_SEC, dateStartMaxSec()].
 */
void clampShowdateSecond() {
    int maxS = dateStartMaxSec();
    if (showdate_second < DATE_START_MIN_SEC) {
        showdate_second = DATE_START_MIN_SEC;
    }
    if (showdate_second > maxS) {
        showdate_second = (uint8_t)maxS;
    }
}

/**
 * Resolve the four digits for the current date frame.
 * phase 0: DD:MM or MM:DD; phase 1: YYYY
 */
void getDateDigits(uint8_t phase, uint8_t& d0, uint8_t& d1, uint8_t& d2, uint8_t& d3) {
    if (phase == 0) {
        if (dateFormat == 0) { // DD:MM
            d0 = (day() / 10) % 10;
            d1 = day() % 10;
            d2 = (month() / 10) % 10;
            d3 = month() % 10;
        } else { // MM:DD
            d0 = (month() / 10) % 10;
            d1 = month() % 10;
            d2 = (day() / 10) % 10;
            d3 = day() % 10;
        }
    } else {
        int y = year();
        d0 = y / 1000;
        d1 = (y / 100) % 10;
        d2 = (y / 10) % 10;
        d3 = y % 10;
    }
}

/**
 * Enable the four date digits on HH:MM tubes (no animation).
 */
void enableDateDigits(uint8_t phase) {
    uint8_t d0, d1, d2, d3;
    getDateDigits(phase, d0, d1, d2, d3);
    nixie.enableSegment(hourTens[d0]);
    nixie.enableSegment(hourUnits[d1]);
    nixie.enableSegment(minuteTens[d2]);
    nixie.enableSegment(minuteUnits[d3]);
}

/**
 * Display current date on Nixie tubes and TFT (2 frames: day/month then YYYY).
 * Hold duration is managed by the dateSeq state machine in loop().
 * Crossfade uses ALib0 taskDelay (cooperative): must stay inline in this
 * function so early returns resume here on the next loop call.
 */
void displayDate() {
    int hour12_24 = set12_24 ? (unsigned char)hour() : (unsigned char)hourFormat12();
    unsigned char hourBcd = decToBcd((unsigned char)hour12_24);
    unsigned char minBcd = decToBcd((unsigned char)minute());

    hv_supply.switchOn();

    if (!nixieOn) {
        hourBcd = 255;
        minBcd = 255;
        hv_supply.switchOff();
        // Do not clear LED_effect â€” see displayTime().
    }

    if (transitionToDate == false) {
        // static: must survive cooperative taskDelay returns/resumes
        static uint8_t d0, d1, d2, d3;

        taskBegin();
        getDateDigits(datePhase, d0, d1, d2, d3);

        nixie.disableSegments(hourTens, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(hourUnits, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);

        // Year frame: turn off DD:MM separator after hour units, before minute tens
        if (datePhase == 1) {
            nixie.disableSegment(UpperLeftDot);
            nixie.disableSegment(LowerLeftDot);
            nixie.updateDisplay();
            taskDelay(waitTime);
        }

        nixie.disableSegments(minuteTens, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(minuteUnits, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);

        nixie.enableSegment(hourTens[d0]);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.enableSegment(hourUnits[d1]);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.enableSegment(minuteTens[d2]);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.enableSegment(minuteUnits[d3]);
        nixie.updateDisplay();
        taskDelay(waitTime);
        taskEnd();

        transitionToDate = true;
        transitionFromDate = false;
    } else {
        nixie.disableAllSegments();
        enableDateDigits(datePhase);
        transitionFromDate = false;
    }

    // Date separator: lower-left dot only on DD:MM / MM:DD (phase 0), never on YYYY
    nixie.disableSegment(UpperRightDot);
    nixie.disableSegment(LowerRightDot);
    nixie.disableSegment(UpperLeftDot);
    if (datePhase == 0) {
        nixie.enableSegment(LowerLeftDot);
    } else {
        nixie.disableSegment(LowerLeftDot);
    }

    nixie.updateDisplay();

    if (menu == TOP) {
        updateTopTftClock(false);
    }
}

unsigned char decToBcd(unsigned char val) {
    return (((val / 10) * 16) + (val % 10));
}

#define colonDigit(digit) digit < 10 ? ":0" : ":"
void formattedTime(char* tod, int hours, int minutes, int seconds) {
    sprintf(tod, "%d%s%d%s%d", hours, colonDigit(minutes), minutes, colonDigit(seconds), seconds);
}

/**
 * Auto shutoff window in minutes-from-midnight.
 * Off window is [off, on) or wraps past midnight when off > on.
 * manualOverride is cleared when the window edge flips.
 */
void evalShutoffTime() {
    if (!enableAutoShutoff) return;

    int mn = 60 * hour() + minute();
    int mn_on = 15 * autoShutoffOntime;
    int mn_off = 15 * autoShutoffOfftime;

    static bool prevShutoffState = true;
    if (((mn_off < mn_on) && (mn >= mn_off) && (mn < mn_on)) ||
        (mn_off > mn_on) && ((mn >= mn_off) || (mn < mn_on))) {
        if (!manualOverride) nixieOn = false;
        if (prevShutoffState == true) manualOverride = false;
        prevShutoffState = false;
    } else {
        if (!manualOverride) nixieOn = true;
        if (prevShutoffState == false) manualOverride = false;
        prevShutoffState = true;
    }
}

void updateEncoderPos() {
    static int encoderA, encoderB, encoderA_prev;

    encoderA = digitalRead(encoderPinA);
    encoderB = digitalRead(encoderPinB);

    if ((!encoderA) && (encoderA_prev)) { // falling edge on A; B selects direction
        encoderPosPrev = encoderPos;
        encoderB ? encoderPos++ : encoderPos--;
        menuLastActivityMs = millis();
        if (menu == SCREENSAVER) {
            // First interaction wakes to TOP; next rotate/press navigates from there
            setDisplayBacklight(true);
            encoderPos = 0;
            encoderPosPrev = 0;
            menu = TOP;
            topStaticDrawn = false;
            invalidateTopClock();
            updateSelection();
        } else if (menu != TOP) {
            updateSelection();
        }
    }
    encoderA_prev = encoderA;
}

#ifdef CLOCK_COLON
const int n_set1 = 7;
#else
const int n_set1 = 6;
#endif
const int n_set2 = 7;
const int n_set3 = 5;

// Partial menu redraw: only refresh highlight rows on encoder navigation
static bool partialMenuRedraw = false;
static bool skipMenuLabels = false;
static bool skipMenuValues = false;
static bool valuesIgnoreRowFilter = false;
static int16_t partialRowY0 = -1;
static int16_t partialRowY1 = -1;

static bool menuRowVisible(int16_t y) {
    if (!partialMenuRedraw) {
        return true;
    }
    return y == partialRowY0 || y == partialRowY1;
}

static bool shouldDrawMenuValue(int16_t y) {
    if (skipMenuValues) {
        return false;
    }
    return valuesIgnoreRowFilter || menuRowVisible(y);
}

// "*" = active Disable/Rainbow/Color Cycle. Tracked separately from LED_effect so
// temporary LED_effect changes cannot move the mark to Disable.
static uint8_t ledRadioMark = 0; // 0..2, or 0xFF when a static color is active
static bool forceLedRadioRedraw = false;
static bool forceSsRadioRedraw = false;

void syncLedRadioMarkFromEffect(void) {
    if (LED_effect <= 2) {
        ledRadioMark = LED_effect;
    } else {
        ledRadioMark = 0xFF;
    }
}

static void drawRadioMarkAt(int16_t y, bool on) {
    // Clear the whole value gutter on radio rows â€” removes phantoms from older * positions.
    const int16_t gx = (int16_t)(SCREEN_WIDTH - DISP_MARGIN_X - MENU_VALUE_GUTTER_W);
    const int16_t gw = (int16_t)(SCREEN_WIDTH - gx);
    const int16_t markX = (int16_t)(SCREEN_WIDTH - DISP_MARGIN_X - 10);
    if (on) {
        tftComposeTextBand(gx, y, gw, TFT_MENU_ROW_STEP, markX, "*", COLOR_ACCENT, COLOR_BG,
                           false, 0, tftMenuFontTopOffset(), &FreeSans9pt7b);
    } else {
        tftComposeFillBand(gx, y, gw, TFT_MENU_ROW_STEP, COLOR_BG);
    }
}

static void drawLedRadioMarks(void) {
    static uint8_t lastDrawnMark = 0xFE;
    // Skip only on pure highlight scrolls when the mark has not moved.
    if (skipMenuValues && !forceLedRadioRedraw && lastDrawnMark == ledRadioMark) {
        return;
    }
    for (int row = 0; row < 3; row++) {
        drawRadioMarkAt(menuRowY(row), ledRadioMark == (uint8_t)row);
    }
    lastDrawnMark = ledRadioMark;
    forceLedRadioRedraw = false;
}

// Animate "Color Cycle" label while the LED options screen is visible.
static void refreshLedColorCycleLabelAnim(void) {
    if (menu != LED_MENU && menu != STATIC_COLOR && menu != LED_BRIGHTNESS) {
        return;
    }
    // Solid highlight bar while selected â€” no hue crawl underneath.
    if (menu == LED_MENU && mod(encoderPos, 6) == 2) {
        return;
    }
    const char* label = "Color Cycle";
    const int16_t y = menuRowY(2);
    const int len = (int)strlen(label);
    int colored = 0;
    for (int i = 0; i < len; i++) {
        if (label[i] != ' ') {
            colored++;
        }
    }
    if (colored < 1) {
        colored = 1;
    }
    RainbowColorCtx ctx = {(uint8_t)((millis() / 40) & 255), colored};
    tftComposeTextBandColored(DISP_MARGIN_X, y, MENU_LABEL_CLEAR_W, TFT_MENU_ROW_STEP,
                              menuLabelX(), label, COLOR_FG, COLOR_BG, false, 0,
                              tftMenuFontTopOffset(), &FreeSans9pt7b, rainbowGlyphColor, &ctx);
}

// "*" = active screensaver mode (Disable / Bouncing Logo / Turn Off Display).
static void drawScreensaverRadioMarks(void) {
    static uint8_t lastDrawnOption = 0xFF;
    if (skipMenuValues && !forceSsRadioRedraw && lastDrawnOption == ssOption) {
        return;
    }
    for (int row = 0; row < 3; row++) {
        drawRadioMarkAt(menuRowY(row), ssOption == row);
    }
    lastDrawnOption = ssOption;
    forceSsRadioRedraw = false;
}

static bool isNavListMenu(int m) {
    return m == SETTINGS1 || m == SETTINGS2 || m == SETTINGS3
        || m == DST_MENU || m == AUTO_SHUTOFF || m == LED_MENU || m == RING_MENU
        || m == SHOW_DATE_MENU || m == SCREENSAVER_MENU || m == SET_TIME;
}

// Parent screen for inline value editors that fall through to the same UI.
static int menuScreenRoot(int m) {
    switch (m) {
        case SET_12_24:
        case BLINK_COLON:
        case SHOW_ZERO:
            return SETTINGS1;
        case ROLL_DOWN:
            return SETTINGS2;
        case ENABLE_DST:
            return DST_MENU;
        case AUTO_SHUTOFF_ENABLE:
        case AUTO_SHUTOFF_OFFTIME:
        case AUTO_SHUTOFF_ONTIME:
            return AUTO_SHUTOFF;
        case SHOW_DATE:
        case SHOW_DATE_SECOND:
        case SHOW_DATE_INTERVAL:
        case SHOW_DATE_DURATION:
        case SHOW_DATE_FORMAT:
            return SHOW_DATE_MENU;
        case RING_MODE:
        case RING_BRIGHTNESS:
        case RING_COLOR:
            return RING_MENU;
        case STATIC_COLOR:
        case LED_BRIGHTNESS:
            return LED_MENU;
        default:
            return m;
    }
}

static bool isInlineEditMenu(int m) {
    return menuScreenRoot(m) != m;
}

static int inlineEditRowIndex(int m) {
    switch (m) {
        case SET_12_24: return 2;
#ifdef CLOCK_COLON
        case BLINK_COLON: return 3;
        case SHOW_ZERO: return 4;
#else
        case SHOW_ZERO: return 3;
#endif
        case ROLL_DOWN: return 3;
        case ENABLE_DST: return 0;
        case AUTO_SHUTOFF_ENABLE: return 0;
        case AUTO_SHUTOFF_OFFTIME: return 1;
        case AUTO_SHUTOFF_ONTIME: return 2;
        case SHOW_DATE: return 0;
        case SHOW_DATE_SECOND: return 1;
        case SHOW_DATE_DURATION: return 2;
        case SHOW_DATE_INTERVAL: return 3;
        case SHOW_DATE_FORMAT: return 4;
        case RING_MODE: return 0;
        case RING_BRIGHTNESS: return 1;
        case RING_COLOR: return 2;
        case STATIC_COLOR: return 3;
        case LED_BRIGHTNESS: return 4;
        default: return -1;
    }
}

static int navListRowCount(int m) {
    switch (m) {
        case SETTINGS1: return n_set1;
        case SETTINGS2: return n_set2;
        case SETTINGS3: return n_set3;
        case DST_MENU: return 3;
        case AUTO_SHUTOFF: return 4;
        case LED_MENU: return 6;
        case RING_MENU: return 4;
        case SHOW_DATE_MENU: return 6;
        case SCREENSAVER_MENU: return 4;
        case SET_TIME: return 3;
        default: return 1;
    }
}

/**
 * Handle menu navigation when button is pressed
 */
void updateMenu() {
    switch (menu) {
        case TOP:
            menu = SETTINGS1;
            break;

        case SCREENSAVER:
            setDisplayBacklight(true);
            encoderPos = 0;
            encoderPosPrev = 0;
            menu = TOP;
            topStaticDrawn = false;
            invalidateTopClock();
            updateSelection();
            break;

        case SETTINGS1:
            switch (mod(encoderPos, n_set1)) {
                case 0:
                    menu = SET_UTC_OFFSET;
                    break;
                case 1:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = DST_MENU;
                    break;
                case 2:
                    menu = SET_12_24;
                    break;
#ifdef CLOCK_COLON
                case 3:
                    menu = BLINK_COLON;
                    break;
                case 4:
                    menu = SHOW_ZERO;
                    break;
                case 5:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SETTINGS2;
                    break;
                case 6:
                    menu = TOP;
                    topStaticDrawn = false;
                    invalidateTopClock();
                    break;
#else
                case 3:
                    menu = SHOW_ZERO;
                    break;
                case 4:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SETTINGS2;
                    break;
                case 5:
                    menu = TOP;
                    topStaticDrawn = false;
                    invalidateTopClock();
                    break;
#endif
            }
            break;

        case SETTINGS2:
            switch (mod(encoderPos, n_set2)) {
                case 0:
                    encoderPos = interval_indx;
                    encoderPosPrev = interval_indx;
                    menu = CATHODE_PROTECT;
                    break;
                case 1:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = AUTO_SHUTOFF;
                    break;
                case 2:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = LED_MENU;
                    break;
                case 3:
                    menu = ROLL_DOWN;
                    break;
                case 4:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SHOW_DATE_MENU;
                    break;
                case 5:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SETTINGS3;
                    break;
                case 6:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SETTINGS1;
                    break;
            }
            break;

        case SETTINGS3:
            switch (mod(encoderPos, n_set3)) {
                case 0:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = RING_MENU;
                    break;
                case 1:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SCREENSAVER_MENU;
                    break;
                case 2:
                    resetWifiReturnMenu = SETTINGS3;
                    resetWifiReturnPos = 2;
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = RESET_WIFI;
                    break;
                case 3:
                    setTimeReturnMenu = SETTINGS3;
                    setTimeReturnPos = 3;
                    menu = SET_TIME;
                    field = 0;
                    encoderButton.poll();
                    break;
                case 4:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SETTINGS2;
                    break;
            }
            break;

        case DST_MENU:
            switch (mod(encoderPos, 3)) {
                case 0:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = ENABLE_DST;
                    break;
                case 1:
                    menu = DST_RULE;
                    break;
                case 2:
                    encoderPos = 1;
                    encoderPosPrev = 1;
                    menu = SETTINGS1;
                    break;
            }
            break;

        case RESET_WIFI:
            switch (mod(encoderPos, 2)) {
                case 0:
                    encoderPos = resetWifiReturnPos;
                    encoderPosPrev = resetWifiReturnPos;
                    menu = resetWifiReturnMenu;
                    break;
                case 1:
                    resetWiFi();
                    menu = TOP;
                    break;
            }
            break;

        case SET_UTC_OFFSET: {
            EEPROM.write(EEPROM_addr_UTC_offset, (unsigned char)(mod(mySTD.offset / 60, 24)));
            EEPROM.commit();
            initProtectionTimer = false;
            time_t currentTime = timeClient.getEpochTime();
            time_t localTime = myTZ.toLocal(currentTime);
            setTime(localTime);
            menuLastActivityMs = millis();
            encoderPos = 0;
            encoderPosPrev = 0;
            menu = SETTINGS1;
            firstEntry = true;
            break;
        }

        case ENABLE_DST:
            EEPROM.write(EEPROM_addr_DST, (unsigned char)enableDST);
            EEPROM.commit();
            initProtectionTimer = false;
            encoderPos = 0;
            encoderPosPrev = 0;
            menu = DST_MENU;
            break;

        case DST_RULE:
            EEPROM.write(EEPROM_addr_DST_rule, currentTimeZone);
            EEPROM.commit();
            myDST = dstRules[currentTimeZone];
            mySTD = stdRules[currentTimeZone];
            myDST.offset = mySTD.offset;
            if (enableDST) myDST.offset += 60;
            myTZ = Timezone(myDST, mySTD);
            initProtectionTimer = false;
            encoderPos = 1;
            encoderPosPrev = 1;
            menu = DST_MENU;
            break;

        case SET_12_24:
            EEPROM.write(EEPROM_addr_12_24, (unsigned char)set12_24);
            EEPROM.commit();
            encoderPos = 2;
            encoderPosPrev = 2;
            menu = SETTINGS1;
            break;

        case BLINK_COLON:
            EEPROM.write(EEPROM_addr_blink_colon, (unsigned char)enableBlink);
            EEPROM.commit();
            encoderPos = 3;
            encoderPosPrev = 3;
            menu = SETTINGS1;
            break;

        case CATHODE_PROTECT:
            interval_indx = mod(encoderPos, num_intervals);
            EEPROM.write(EEPROM_addr_protect, (unsigned char)intervals[interval_indx]);
            EEPROM.commit();
            initProtectionTimer = false;
            protectTimer = 0;
            encoderPos = 0;
            encoderPosPrev = 0;
            menu = SETTINGS2;
            break;

        case AUTO_SHUTOFF:
            switch (mod(encoderPos, 4)) {
                case 0:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = AUTO_SHUTOFF_ENABLE;
                    break;
                case 1:
                    menu = AUTO_SHUTOFF_OFFTIME;
                    break;
                case 2:
                    menu = AUTO_SHUTOFF_ONTIME;
                    break;
                case 3:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SETTINGS2;
                    break;
            }
            break;

        case AUTO_SHUTOFF_ENABLE:
            EEPROM.write(EEPROM_addr_shutoff_en, (unsigned char)enableAutoShutoff);
            EEPROM.commit();
            encoderPos = 0;
            encoderPosPrev = 0;
            menu = AUTO_SHUTOFF;
            break;

        case AUTO_SHUTOFF_OFFTIME:
            EEPROM.write(EEPROM_addr_shutoff_off, (unsigned char)autoShutoffOfftime);
            EEPROM.commit();
            encoderPos = 1;
            encoderPosPrev = 1;
            menu = AUTO_SHUTOFF;
            break;

        case AUTO_SHUTOFF_ONTIME:
            EEPROM.write(EEPROM_addr_shutoff_on, (unsigned char)autoShutoffOntime);
            EEPROM.commit();
            encoderPos = 2;
            encoderPosPrev = 2;
            menu = AUTO_SHUTOFF;
            break;

        case ROLL_DOWN:
            EEPROM.write(EEPROM_addr_roll_down, (unsigned char)enableRollDown);
            EEPROM.commit();
            encoderPos = 3;
            encoderPosPrev = 3;
            menu = SETTINGS2;
            break;

        case SCREENSAVER_MENU: {
            int opt = mod(encoderPos, 4);
            if (opt < 3) {
                ssOption = (uint8_t)opt;
                EEPROM.write(EEPROM_addr_screensaver, (unsigned char)ssOption);
                EEPROM.commit();
            } else {
                encoderPos = 4;
                encoderPosPrev = 4;
                menu = SETTINGS2;
            }
            break;
        }

        case LED_MENU: {
            int opt = mod(encoderPos, 6);
            if (opt < 3) {
                LED_effect = (uint8_t)opt;
                ledRadioMark = (uint8_t)opt;
                static_color = 2; // Reset to default static color
                static_color_indx = 2;
                EEPROM.write(EEPROM_addr_led, LED_effect);
                EEPROM.write(EEPROM_addr_static_color, static_color);
                EEPROM.commit();
                updateLEDs();
            } else if (opt == 3) {
                // Seed from the active static color (row index 3 must not become color index 3/amber).
                if (LED_effect >= 3 && LED_effect <= 14) {
                    static_color_indx = (uint8_t)(LED_effect - 3);
                }
                encoderPos = static_color_indx;
                encoderPosPrev = static_color_indx;
                menu = STATIC_COLOR;
            } else if (opt == 4) {
                encoderPos = led_brightness_indx;
                encoderPosPrev = led_brightness_indx;
                menu = LED_BRIGHTNESS;
            } else {
                encoderPos = 2;
                encoderPosPrev = 2;
                menu = SETTINGS2;
            }
            break;
        }

        case STATIC_COLOR:
            static_color = static_color_indx;
            EEPROM.write(EEPROM_addr_led, LED_effect);
            EEPROM.write(EEPROM_addr_static_color, static_color_indx);
            EEPROM.commit();
            ledRadioMark = 0xFF;
            encoderPos = 3;
            encoderPosPrev = 3;
            menu = LED_MENU;
            break;

        case LED_BRIGHTNESS:
            EEPROM.write(EEPROM_addr_led_brightness, (unsigned char)led_brightness_indx);
            EEPROM.commit();
            encoderPos = 4;
            encoderPosPrev = 4;
            menu = LED_MENU;
            break;

        case SHOW_ZERO:
            EEPROM.write(EEPROM_addr_showzero, (unsigned char)showZero);
            EEPROM.commit();
#ifdef CLOCK_COLON
            encoderPos = 4;
            encoderPosPrev = 4;
#else
            encoderPos = 3;
            encoderPosPrev = 3;
#endif
            menu = SETTINGS1;
            break;

        case SHOW_DATE_MENU:
            switch (mod(encoderPos, 6)) {
                case 0:
                    menu = SHOW_DATE;
                    break;
                case 1:
                    clampShowdateSecond();
                    encoderPos = showdate_second - DATE_START_MIN_SEC;
                    encoderPosPrev = encoderPos;
                    menu = SHOW_DATE_SECOND;
                    break;
                case 2:
                    encoderPos = date_duration_indx;
                    encoderPosPrev = date_duration_indx;
                    menu = SHOW_DATE_DURATION;
                    break;
                case 3:
                    menu = SHOW_DATE_INTERVAL;
                    break;
                case 4:
                    menu = SHOW_DATE_FORMAT;
                    break;
                case 5:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SETTINGS3;
                    break;
            }
            break;

        case SHOW_DATE:
            EEPROM.write(EEPROM_addr_showdate, (unsigned char)showDate);
            EEPROM.commit();
            encoderPos = 0;
            encoderPosPrev = 0;
            menu = SHOW_DATE_MENU;
            break;

        case SHOW_DATE_SECOND:
            EEPROM.write(EEPROM_addr_showdate_second, (unsigned char)showdate_second);
            EEPROM.commit();
            encoderPos = 1;
            encoderPosPrev = 1;
            menu = SHOW_DATE_MENU;
            break;

        case SHOW_DATE_INTERVAL:
            EEPROM.write(EEPROM_addr_showdate_interval, showdate_interval);
            EEPROM.commit();
            encoderPos = 3;
            encoderPosPrev = 3;
            menu = SHOW_DATE_MENU;
            break;

        case SHOW_DATE_DURATION:
            EEPROM.write(EEPROM_addr_showdate_duration, showdate_duration);
            EEPROM.commit();
            encoderPos = 2;
            encoderPosPrev = 2;
            menu = SHOW_DATE_MENU;
            break;

        case SHOW_DATE_FORMAT:
            EEPROM.write(EEPROM_addr_date_format, dateFormat);
            EEPROM.commit();
            encoderPos = 4;
            encoderPosPrev = 4;
            menu = SHOW_DATE_MENU;
            break;

        case RING_MENU: {
            int opt = mod(encoderPos, 4);
            if (opt == 0) {
                encoderPos = ringMode;
                encoderPosPrev = ringMode;
                menu = RING_MODE;
            } else if (opt == 1) {
                encoderPos = ringBrightnessIndx;
                encoderPosPrev = ringBrightnessIndx;
                menu = RING_BRIGHTNESS;
            } else if (opt == 2) {
                encoderPos = ringColorIndx;
                encoderPosPrev = ringColorIndx;
                menu = RING_COLOR;
            } else {
                encoderPos = 1;
                encoderPosPrev = 1;
                menu = SETTINGS3;
            }
            break;
        }

        case RING_MODE:
            EEPROM.write(EEPROM_addr_ring_mode, ringMode);
            EEPROM.commit();
            encoderPos = 0;
            encoderPosPrev = 0;
            menu = RING_MENU;
            break;

        case RING_BRIGHTNESS:
            EEPROM.write(EEPROM_addr_ring_brightness, ringBrightnessIndx);
            EEPROM.commit();
            encoderPos = 1;
            encoderPosPrev = 1;
            menu = RING_MENU;
            break;

        case RING_COLOR:
            EEPROM.write(EEPROM_addr_ring_color, ringColorIndx);
            EEPROM.commit();
            encoderPos = 2;
            encoderPosPrev = 2;
            menu = RING_MENU;
            break;

        case SET_TIME:
            switch (mod(encoderPos, 3)) {
                case 0:
                    menu = SET_TIME_MANUALLY;
                    break;
                case 1:
                    menu = SET_TIME_WIFI;
                    break;
                case 2:
                    encoderPos = setTimeReturnPos;
                    encoderPosPrev = setTimeReturnPos;
                    menu = setTimeReturnMenu;
                    break;
            }
            break;

        case SET_TIME_WIFI: {
            if (!wifiProvisionConnected()) {
                wifiProvisionRequestReconnect();
            }
            timeClient.update();
            time_t currentTime = timeClient.getEpochTime();
            time_t localTime = myTZ.toLocal(currentTime);
            setTime(localTime);
            setHour = hour(localTime);
            setMinute = minute(localTime);
            setDay = day(localTime);
            setMonth = month(localTime);
            setYear = year(localTime);
            setAmPm = isPM(localTime) ? 1 : 0;
            encoderPos = 1;
            encoderPosPrev = 1;
            menu = SET_TIME;
            break;
        }

        case SET_TIME_MANUALLY:
            // Handled separately in updateSelection() to avoid jump issues
            break;
    }
    updateSelection();
}

/**
 * Update TFT display based on current menu selection
 */
void updateSelection() {
    // Declare variables outside switch to avoid jump issues
    int UTC_STD_Offset = 0;
    int dispOffset = 0;
    char timestr[12]; // AUTO_SHUTOFF: "23:45" or "12:45 PM"

    static Menu uiLastMenu = TOP;
    const Menu prevMenu = uiLastMenu;
    const bool menuChanged = (menu != prevMenu);
    uiLastMenu = menu;
    const bool sameScreen = (menuScreenRoot(prevMenu) == menuScreenRoot(menu));

    if (menu != TOP && menu != SET_UTC_OFFSET) {
        topStaticDrawn = false;
        invalidateTopClock();
    }

    if (menu != SET_UTC_OFFSET) {
        // Full clear only when switching to a different screen layout.
        if (menuChanged && !sameScreen) {
            displayClear();
        }
    }
    if (menu != SCREENSAVER || ssOption != 2) {
        setDisplayBacklight(true);
    }

    partialMenuRedraw = false;
    skipMenuLabels = false;
    skipMenuValues = false;
    valuesIgnoreRowFilter = false;

    // Compare against last committed UI marks; latch AFTER the switch so STATIC_COLOR
    // updates to ledRadioMark in this frame do not leave lastUi one tick behind.
    static uint8_t lastUiLedRadioMark = 0xFE;
    static uint8_t lastUiSsOption = 0xFF;
    const bool ledMarksChanged =
        (menuScreenRoot(menu) == LED_MENU) && (ledRadioMark != lastUiLedRadioMark);
    const bool ssMarksChanged =
        (menu == SCREENSAVER_MENU) && (ssOption != lastUiSsOption);
    if (ledMarksChanged) {
        forceLedRadioRedraw = true;
    }
    if (ssMarksChanged) {
        forceSsRadioRedraw = true;
    }

    if (!menuChanged && encoderPos != encoderPosPrev && isNavListMenu(menu)) {
        // Scroll within a nav list: refresh label highlights only.
        const int rows = navListRowCount(menu);
        const int prevRow = mod(encoderPosPrev, rows);
        const int curRow = mod(encoderPos, rows);
        partialMenuRedraw = true;
        skipMenuValues = true;
        partialRowY0 = menuRowY(prevRow);
        partialRowY1 = menuRowY(curRow);
        display.startWrite();
        clearMenuLabelArea(partialRowY0);
        if (curRow != prevRow) {
            clearMenuLabelArea(partialRowY1);
        }
        // If a radio "*" also changed (button confirm in same frame as residual encoder delta),
        // still allow mark redraw via drawLedRadioMarks / drawScreensaverRadioMarks.
        if (ledMarksChanged || ssMarksChanged) {
            skipMenuValues = false;
            valuesIgnoreRowFilter = true;
            skipMenuLabels = false; // labels already cleared for highlight rows
        }
    } else if (menuChanged && sameScreen) {
        // Enter/exit inline value editor: refresh affected row(s) only.
        int r0 = inlineEditRowIndex(prevMenu);
        int r1 = inlineEditRowIndex(menu);
        if (r0 < 0 && isNavListMenu(prevMenu)) {
            r0 = mod(encoderPos, navListRowCount(prevMenu));
        }
        if (r1 < 0 && isNavListMenu(menu)) {
            r1 = mod(encoderPos, navListRowCount(menu));
        }
        if (r0 < 0) {
            r0 = r1;
        }
        if (r1 < 0) {
            r1 = r0;
        }
        if (r0 >= 0) {
            partialMenuRedraw = true;
            partialRowY0 = menuRowY(r0);
            partialRowY1 = (r1 >= 0) ? menuRowY(r1) : partialRowY0;
            // LED/screensaver: also refresh all right-side marks (effect may change on enter).
            if (ledMarksChanged || ssMarksChanged
                || menuScreenRoot(menu) == LED_MENU || menu == SCREENSAVER_MENU) {
                valuesIgnoreRowFilter = true;
                forceLedRadioRedraw = true;
                forceSsRadioRedraw = (menu == SCREENSAVER_MENU) || ssMarksChanged;
            }
            display.startWrite();
            clearMenuRow(partialRowY0);
            if (partialRowY1 != partialRowY0) {
                clearMenuRow(partialRowY1);
            }
        }
    } else if (!menuChanged && encoderPos != encoderPosPrev && isInlineEditMenu(menu)) {
        // Adjusting an inline value: update that value only.
        const int row = inlineEditRowIndex(menu);
        if (row >= 0) {
            partialMenuRedraw = true;
            skipMenuLabels = true;
            partialRowY0 = menuRowY(row);
            partialRowY1 = partialRowY0;
            display.startWrite();
            // Static color edits change LED_effect â€” keep "*" rows in sync.
            if (ledMarksChanged) {
                forceLedRadioRedraw = true;
            }
        }
    } else if (!menuChanged && (ledMarksChanged || ssMarksChanged)) {
        // Confirmed a radio option on the same screen: move "*" without reloading labels.
        skipMenuLabels = true;
        display.startWrite();
        partialMenuRedraw = true; // reuse endWrite path
        partialRowY0 = -1;
        partialRowY1 = -1;
        valuesIgnoreRowFilter = true;
        forceLedRadioRedraw = ledMarksChanged;
        forceSsRadioRedraw = ssMarksChanged;
    }

    // Full screen entry onto LED/SS options: always paint marks once.
    if (menuChanged && !sameScreen) {
        if (menuScreenRoot(menu) == LED_MENU) {
            forceLedRadioRedraw = true;
        }
        if (menu == SCREENSAVER_MENU) {
            forceSsRadioRedraw = true;
        }
    }

    if (menu != TOP && menu != SET_UTC_OFFSET && menu != SCREENSAVER) {
        setMenuTextSize();
    }

    switch (menu) {
        case TOP:
            if (!topStaticDrawn) {
                if (!menuChanged) {
                    displayClear();
                }
                drawTopStatic();
                updateTopTftClock(true);
            }
            break;

        case SCREENSAVER:
            if (ssOption == 1) {
                renderScreensaverFrame();
            }
            break;

        case ENABLE_DST:
            if (encoderPos != encoderPosPrev) enableDST = !enableDST;
            myDST = dstRules[currentTimeZone];
            mySTD = stdRules[currentTimeZone];
            myDST.offset = mySTD.offset;
            if (enableDST) myDST.offset += 60;
            myTZ = Timezone(myDST, mySTD);
            // Fall through to DST_MENU

        case DST_MENU: {
            printMenuTitle("DST SETTINGS");
            const int16_t y0 = menuRowY(0), y1 = menuRowY(1), y2 = menuRowY(2);

            printMenuLabel("Auto DST", y0, menu == DST_MENU && mod(encoderPos, 3) == 0);
            printMenuValueRight(enableDST ? "On" : "Off", y0, menu == ENABLE_DST);

            printMenuLabel("DST Rule", y1, menu == DST_MENU && mod(encoderPos, 3) == 1);
            {
                const char* rule = "Europe";
                switch (currentTimeZone) {
                    case TZ_USA: rule = "USA"; break;
                    case TZ_AUSTRALIA: rule = "Australia"; break;
                    case TZ_NEWZEALAND: rule = "New Zealand"; break;
                    case TZ_CHILE: rule = "Chile"; break;
                    default: break;
                }
                printMenuValueRight(rule, y1, menu == DST_RULE);
            }

            printMenuLabel("Return", y2, menu == DST_MENU && mod(encoderPos, 3) == 2);
            break;
        }

        case DST_RULE:
            if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = currentTimeZone;
            currentTimeZone = mod(encoderPos, 5);
            {
                const char* rule = "Europe";
                switch (currentTimeZone) {
                    case TZ_USA: rule = "USA"; break;
                    case TZ_AUSTRALIA: rule = "Australia"; break;
                    case TZ_NEWZEALAND: rule = "New Zealand"; break;
                    case TZ_CHILE: rule = "Chile"; break;
                    default: break;
                }
                const int16_t dstTextW =
                    (int16_t)(SCREEN_WIDTH - 2 * DISP_MARGIN_X - GLOBE_BMP_W);
                // Header + hints once on screen entry; globe spares the header corner.
                if (menuChanged) {
                    composeMenuLineLeftColor(DISP_MARGIN_Y, "SELECT DST RULE", COLOR_ACCENT);
                    tftComposeTextBand(DISP_MARGIN_X, menuBottomRowY(2), dstTextW, TFT_MENU_ROW_STEP,
                                       menuLabelX(), "Press knob to", COLOR_FG, COLOR_BG, false, 0,
                                       tftMenuFontTopOffset(), &FreeSans9pt7b);
                    tftComposeTextBand(DISP_MARGIN_X, menuBottomRowY(1), dstTextW, TFT_MENU_ROW_STEP,
                                       menuLabelX(), "confirm DST Rule", COLOR_FG, COLOR_BG, false,
                                       0, tftMenuFontTopOffset(), &FreeSans9pt7b);
                }
                tftComposeTextBand(DISP_MARGIN_X, menuRowY(1), dstTextW, TFT_MENU_ROW_STEP,
                                   menuLabelX(), rule, COLOR_FG, COLOR_BG, false, 0,
                                   tftMenuFontTopOffset(), &FreeSans9pt7b);
                drawDstGlobe(currentTimeZone);
            }
            break;

        case SET_12_24:
            if (menu == SET_12_24 && encoderPos != encoderPosPrev) set12_24 = !set12_24;
            displayTime();
            // Fall through to SETTINGS1

        case BLINK_COLON:
            if (menu == BLINK_COLON) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = enableBlink_indx;
                enableBlink_indx = mod(encoderPos, enableBlink_num_state);
                enableBlink = enableBlink_state[enableBlink_indx];
            }
            // Fall through to SETTINGS1

        case SHOW_ZERO:
            if (menu == SHOW_ZERO && encoderPos != encoderPosPrev) {
                showZero = !showZero;
                displayTime();
            }
            // Fall through to SETTINGS1

        case SETTINGS1: {
            if (menu != SETTINGS1 && menu != SET_12_24 && menu != BLINK_COLON && menu != SHOW_ZERO) {
                break;
            }
            printMenuTitle("SETTINGS (1 of 3)");
            int16_t y = menuRowY(0);
            int row = 0;

            printMenuLabel("Set UTC Offset", y, menu == SETTINGS1 && mod(encoderPos, n_set1) == row);
            y += TFT_MENU_ROW_STEP; row++;

            printMenuLabel("Auto DST", y, menu == SETTINGS1 && mod(encoderPos, n_set1) == row);
            printMenuValueRight(enableDST ? "On" : "Off", y, false);
            y += TFT_MENU_ROW_STEP; row++;

            printMenuLabel("12/24 Hours", y, menu == SETTINGS1 && mod(encoderPos, n_set1) == row);
            printMenuValueRight(set12_24 ? "24" : "12", y, menu == SET_12_24);
            y += TFT_MENU_ROW_STEP; row++;

#ifdef CLOCK_COLON
            printMenuLabel("Colon", y, menu == SETTINGS1 && mod(encoderPos, n_set1) == row);
            {
                const char* colonStr = "Off";
                switch (enableBlink) {
                    case 1: colonStr = "Slow"; break;
                    case 2: colonStr = "Fast"; break;
                    case 3: colonStr = "On"; break;
                    default: break;
                }
                printMenuValueRight(colonStr, y, menu == BLINK_COLON);
            }
            y += TFT_MENU_ROW_STEP; row++;
#endif

            printMenuLabel("Show Zero", y, menu == SETTINGS1 && mod(encoderPos, n_set1) == row);
            printMenuValueRight(showZero ? "On" : "Off", y, menu == SHOW_ZERO);
            y += TFT_MENU_ROW_STEP; row++;

            printMenuLabel("More Options", y, menu == SETTINGS1 && mod(encoderPos, n_set1) == row);
            y += TFT_MENU_ROW_STEP; row++;
            printMenuLabel("Return", y, menu == SETTINGS1 && mod(encoderPos, n_set1) == row);
            break;
        }

        case CATHODE_PROTECT: {
            uint8_t preview = mod(encoderPos, num_intervals);
            if (menuChanged) {
                printMenuTitle("PROTECT CATHODE");
                composeMenuLineCentered(menuBottomRowY(2), "Press knob to");
                composeMenuLineCentered(menuBottomRowY(1), "confirm setting");
            }
            if (preview == 0) {
                composeMenuLineCentered(menuRowY(1), "Off");
                composeMenuLineCentered(menuRowY(2), "(not recommended)");
            } else {
                char buf[20];
                int mins = intervals[preview];
                if (mins == 1) {
                    snprintf(buf, sizeof(buf), "every minute");
                } else {
                    snprintf(buf, sizeof(buf), "every %d min", mins);
                }
                composeMenuLineCentered(menuRowY(1), buf);
                tftComposeFillBand(DISP_MARGIN_X, menuRowY(2), CONTENT_W, TFT_MENU_ROW_STEP, COLOR_BG);
            }
            break;
        }

        case ROLL_DOWN:
            if (encoderPos != encoderPosPrev) enableRollDown = !enableRollDown;
            // Fall through to SETTINGS2

        case SETTINGS2: {
            if (menu != SETTINGS2 && menu != ROLL_DOWN) {
                break;
            }
            printMenuTitle("SETTINGS (2 of 3)");
            char buf[8];

            printMenuLabel("Protect Cathode", menuRowY(0), menu == SETTINGS2 && mod(encoderPos, n_set2) == 0);
            if (interval_indx == 0) {
                printMenuValueRight("Off", menuRowY(0), false);
            } else {
                snprintf(buf, sizeof(buf), "%dmin", intervals[interval_indx]);
                printMenuValueRight(buf, menuRowY(0), false);
            }

            printMenuLabel("Auto Shut Off", menuRowY(1), menu == SETTINGS2 && mod(encoderPos, n_set2) == 1);
            printMenuValueRight(enableAutoShutoff ? "On" : "Off", menuRowY(1), false);

            printMenuLabel("LED Effect", menuRowY(2), menu == SETTINGS2 && mod(encoderPos, n_set2) == 2);
            printMenuValueRight((LED_effect > 0) ? "On" : "Off", menuRowY(2), false);

            printMenuLabel("Roll Down", menuRowY(3), menu == SETTINGS2 && mod(encoderPos, n_set2) == 3);
            printMenuValueRight(enableRollDown ? "On" : "Off", menuRowY(3), menu == ROLL_DOWN);

            printMenuLabel("Show Date", menuRowY(4), menu == SETTINGS2 && mod(encoderPos, n_set2) == 4);
            printMenuValueRight(showDate ? "On" : "Off", menuRowY(4), false);

            printMenuLabel("More Options", menuRowY(5), menu == SETTINGS2 && mod(encoderPos, n_set2) == 5);
            printMenuLabel("Return", menuRowY(6), menu == SETTINGS2 && mod(encoderPos, n_set2) == 6);
            break;
        }

        case SETTINGS3: {
            if (menu != SETTINGS3) {
                break;
            }
            printMenuTitle("SETTINGS (3 of 3)");

            printMenuLabel("Seconds Ring", menuRowY(0), menu == SETTINGS3 && mod(encoderPos, n_set3) == 0);

            printMenuLabel("Screensaver", menuRowY(1), menu == SETTINGS3 && mod(encoderPos, n_set3) == 1);
            printMenuValueRight((ssOption > 0) ? "On" : "Off", menuRowY(1), false);

            printMenuLabel("Reset Wifi", menuRowY(2), menu == SETTINGS3 && mod(encoderPos, n_set3) == 2);

            printMenuLabel("Set Time/Date", menuRowY(3), menu == SETTINGS3 && mod(encoderPos, n_set3) == 3);

            printMenuLabel("Return", menuRowY(4), menu == SETTINGS3 && mod(encoderPos, n_set3) == 4);
            break;
        }

        case SET_TIME:
            printMenuTitle("SET DATE/TIME");
            printMenuLabel("Set manually", menuRowY(0), menu == SET_TIME && mod(encoderPos, 3) == 0);
            printMenuLabel("WiFi sync", menuRowY(1), menu == SET_TIME && mod(encoderPos, 3) == 1);
            printMenuLabel("Return", menuRowY(2), menu == SET_TIME && mod(encoderPos, 3) == 2);
            break;

        case SET_TIME_MANUALLY: {
            static bool firstRun = true;
            const int maxFields = set12_24 ? 5 : 6; // Fields without confirmation
            static unsigned long lastPressTime = 0;
            const unsigned long debounceDelay = 200;

            if (!firstRun && encoderButton.pushed() && (millis() - lastPressTime > debounceDelay)) {
                lastPressTime = millis();
                field++;
                if (field > maxFields + 1) field = maxFields + 1;
            }
            firstRun = false;

            if (encoderPos != encoderPosPrev && field <= maxFields) {
                switch (field) {
                    case 0: // Hour
                        if (set12_24) setHour = mod(encoderPos, 24);
                        else setHour = constrain(mod(encoderPos, 12) + 1, 1, 12);
                        break;
                    case 1: // Minute
                        setMinute = mod(encoderPos, 60);
                        break;
                    case 2: // Day
                        setDay = constrain(mod(encoderPos, 32), 1, 31);
                        break;
                    case 3: // Month
                        setMonth = constrain(mod(encoderPos, 13), 1, 12);
                        break;
                    case 4: // Year
                        setYear = constrain(2000 + encoderPos, 2000, 2099);
                        break;
                    case 5: // AM/PM (only in 12-hour mode)
                        if (!set12_24) setAmPm = mod(encoderPos, 2);
                        break;
                }
                encoderPosPrev = encoderPos;
            }

            printMenuTitle("SET TIME/DATE");
            {
                char buf[8];
                const int16_t yTime = menuRowY(0);
                const int16_t yDate = menuRowY(2);
                const int16_t x0 = menuLabelX();
                const int16_t x1 = (int16_t)(DISP_MARGIN_X + CONTENT_W);

                display.startWrite();
                for (int16_t row = 0; row < TFT_MENU_ROW_STEP; row++) {
                    int16_t x = x0;
                    tftComposeFillRange(DISP_MARGIN_X, x1, COLOR_BG);
                    tftComposeStampTextRow(x, row, tftMenuFontTopOffset(), &FreeSans9pt7b,
                                           "Time: ", COLOR_FG, false, nullptr, nullptr);
                    x = (int16_t)(x + tftMenuTextWidth(display, "Time: "));
                    snprintf(buf, sizeof(buf), "%02d", setHour);
                    composeStampField(&x, row, buf, field == 0);
                    tftComposeStampTextRow(x, row, tftMenuFontTopOffset(), &FreeSans9pt7b,
                                           ":", COLOR_FG, false, nullptr, nullptr);
                    x = (int16_t)(x + tftMenuTextWidth(display, ":"));
                    snprintf(buf, sizeof(buf), "%02d", setMinute);
                    composeStampField(&x, row, buf, field == 1);
                    if (!set12_24) {
                        tftComposeStampTextRow(x, row, tftMenuFontTopOffset(), &FreeSans9pt7b,
                                               " ", COLOR_FG, false, nullptr, nullptr);
                        x = (int16_t)(x + tftMenuTextWidth(display, " "));
                        composeStampField(&x, row, amPmLabels[setAmPm], field == 5);
                    }
                    tftComposeBlitRange(DISP_MARGIN_X, (int16_t)(yTime + row), x1);
                }

                for (int16_t row = 0; row < TFT_MENU_ROW_STEP; row++) {
                    int16_t x = x0;
                    tftComposeFillRange(DISP_MARGIN_X, x1, COLOR_BG);
                    tftComposeStampTextRow(x, row, tftMenuFontTopOffset(), &FreeSans9pt7b,
                                           "Date (DD:MM:YYYY): ", COLOR_FG, false, nullptr, nullptr);
                    x = (int16_t)(x + tftMenuTextWidth(display, "Date (DD:MM:YYYY): "));
                    snprintf(buf, sizeof(buf), "%02d", setDay);
                    composeStampField(&x, row, buf, field == 2);
                    tftComposeStampTextRow(x, row, tftMenuFontTopOffset(), &FreeSans9pt7b,
                                           ".", COLOR_FG, false, nullptr, nullptr);
                    x = (int16_t)(x + tftMenuTextWidth(display, "."));
                    snprintf(buf, sizeof(buf), "%02d", setMonth);
                    composeStampField(&x, row, buf, field == 3);
                    tftComposeStampTextRow(x, row, tftMenuFontTopOffset(), &FreeSans9pt7b,
                                           ".", COLOR_FG, false, nullptr, nullptr);
                    x = (int16_t)(x + tftMenuTextWidth(display, "."));
                    snprintf(buf, sizeof(buf), "%04d", setYear);
                    composeStampField(&x, row, buf, field == 4);
                    tftComposeBlitRange(DISP_MARGIN_X, (int16_t)(yDate + row), x1);
                }
                display.endWrite();
            }

            if (field < maxFields) {
                composeMenuLineLeft(menuRowY(4), "Press to next");
            } else if (field == maxFields) {
                composeMenuLineLeft(menuRowY(4), "Press to confirm");
            } else { // field == maxFields + 1
                composeMenuLineLeft(menuRowY(4), "Saving...");
                if (set12_24) {
                    setTime(setHour, setMinute, 0, setDay, setMonth, setYear);
                } else {
                    int adjustedHour = setHour + (setAmPm ? 12 : 0);
                    if (setHour == 12) adjustedHour = setAmPm ? 12 : 0;
                    setTime(adjustedHour, setMinute, 0, setDay, setMonth, setYear);
                }
                delay(500);
                menu = SET_TIME;
                field = 0;
                firstRun = true;
                menuLastActivityMs = millis();
                encoderPos = 0;
                encoderPosPrev = 0;
                displayClear();
                updateSelection();
                return;
            }
            break;
        }

        case SET_TIME_WIFI:
            {
            printMenuTitle("SYNC VIA WIFI");
            composeMenuLineLeft(menuRowY(0), "Syncing...");
            timeClient.update();
            time_t currentTime = timeClient.getEpochTime();
            time_t localTime = myTZ.toLocal(currentTime);
            setTime(localTime);
            setHour = hour(localTime);
            setMinute = minute(localTime);
            setDay = day(localTime);
            setMonth = month(localTime);
            setYear = year(localTime);
            setAmPm = isPM(localTime) ? 1 : 0;
            displayClear();
            printMenuTitle("SYNC VIA WIFI");
            composeMenuLineLeft(menuRowY(0), "Time synced");
            delay(1000);
            menu = SET_TIME;
            menuLastActivityMs = millis();
            encoderPos = 1;
            encoderPosPrev = 1;
            updateSelection();
            }
            break;

        case RESET_WIFI:
            printMenuTitle("RESET WIFI?");
            printMenuLabel("No", menuRowY(0), mod(encoderPos, 2) == 0);
            printMenuLabel("Yes", menuRowY(1), mod(encoderPos, 2) == 1);
            break;

        case SET_UTC_OFFSET:
            {
            UTC_STD_Offset = mySTD.offset / 60;
            static int lastUtcPreviewSec = -1;

            bool timeChanged = false;
            if (firstEntry) {
                displayClear();
                printMenuTitle("SET TIMEZONE OFFSET");
                composeMenuLineCentered(menuBottomRowY(2), "Press knob to");
                composeMenuLineCentered(menuBottomRowY(1), "confirm offset");
                firstEntry = false;
                timeChanged = true;
                lastUtcPreviewSec = -1;
            } else if (encoderPos > encoderPosPrev) {
                UTC_STD_Offset++;
                if (UTC_STD_Offset > 11) UTC_STD_Offset = -12;
                timeChanged = true;
            } else if (encoderPos < encoderPosPrev) {
                UTC_STD_Offset--;
                if (UTC_STD_Offset < -12) UTC_STD_Offset = 12;
                timeChanged = true;
            }

            mySTD = stdRules[currentTimeZone];
            myDST = dstRules[currentTimeZone];
            mySTD.offset = UTC_STD_Offset * 60;
            myDST.offset = mySTD.offset;
            if (enableDST) myDST.offset += 60;
            myTZ = Timezone(myDST, mySTD);

            // Preview local wall time including DST when enabled for this rule set.
            time_t baseTime = timeClient.getEpochTime();
            setTime(myTZ.toLocal(baseTime));

            menuLastActivityMs = millis();

            if (timeChanged) {
                char utcStr[24];
                snprintf(utcStr, sizeof(utcStr), "UTC %c %d hours",
                         UTC_STD_Offset >= 0 ? '+' : '-', abs(UTC_STD_Offset));
                composeMenuLineCentered(menuRowY(1), utcStr);

                drawUtcOffsetPreview();
                lastUtcPreviewSec = second();
                displayTime(true);
                encoderPosPrev = encoderPos;
            } else if (second() != lastUtcPreviewSec) {
                drawUtcOffsetPreview();
                lastUtcPreviewSec = second();
            }
            }
            break;

        case AUTO_SHUTOFF_ENABLE:
            if (encoderPos != encoderPosPrev) enableAutoShutoff = !enableAutoShutoff;
            // Fall through to AUTO_SHUTOFF

        case AUTO_SHUTOFF_OFFTIME:
            if (menu == AUTO_SHUTOFF_OFFTIME) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = autoShutoffOfftime;
                autoShutoffOfftime = mod(encoderPos, 96);
            }
            // Fall through to AUTO_SHUTOFF

        case AUTO_SHUTOFF_ONTIME:
            if (menu == AUTO_SHUTOFF_ONTIME) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = autoShutoffOntime;
                autoShutoffOntime = mod(encoderPos, 96);
            }
            // Fall through to AUTO_SHUTOFF

        case AUTO_SHUTOFF: {
            printMenuTitle("AUTO SHUT-OFF");

            printMenuLabel("Enable", menuRowY(0), menu == AUTO_SHUTOFF && mod(encoderPos, 4) == 0);
            printMenuValueRight(enableAutoShutoff ? "On" : "Off", menuRowY(0), menu == AUTO_SHUTOFF_ENABLE);

            printMenuLabel("Turn Off Time", menuRowY(1), menu == AUTO_SHUTOFF && mod(encoderPos, 4) == 1);
            formatShutoffTime(timestr, sizeof(timestr), autoShutoffOfftime);
            printMenuValueRight(timestr, menuRowY(1), menu == AUTO_SHUTOFF_OFFTIME);

            printMenuLabel("Turn On Time", menuRowY(2), menu == AUTO_SHUTOFF && mod(encoderPos, 4) == 2);
            formatShutoffTime(timestr, sizeof(timestr), autoShutoffOntime);
            printMenuValueRight(timestr, menuRowY(2), menu == AUTO_SHUTOFF_ONTIME);

            printMenuLabel("Return", menuRowY(3), menu == AUTO_SHUTOFF && mod(encoderPos, 4) == 3);
            break;
        }

        case SHOW_DATE:
            if (encoderPos != encoderPosPrev) showDate = !showDate;
            // Fall through to SHOW_DATE_MENU

        case SHOW_DATE_INTERVAL:
            if (menu == SHOW_DATE_INTERVAL) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = date_interval_indx;
                date_interval_indx = mod(encoderPos, date_num_intervals);
                showdate_interval = date_intervals[date_interval_indx];
            }
            // Fall through to SHOW_DATE_MENU

        case SHOW_DATE_SECOND:
            if (menu == SHOW_DATE_SECOND) {
                int maxS = dateStartMaxSec();
                int range = maxS - DATE_START_MIN_SEC + 1;
                if (range < 1) range = 1;
                if (encoderPos == 0 && encoderPosPrev == 0) {
                    encoderPos = showdate_second - DATE_START_MIN_SEC;
                }
                showdate_second = (uint8_t)(DATE_START_MIN_SEC + mod(encoderPos, range));
            }
            // Fall through to SHOW_DATE_MENU

        case SHOW_DATE_DURATION:
            if (menu == SHOW_DATE_DURATION) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = date_duration_indx;
                date_duration_indx = mod(encoderPos, date_duration_num_intervals);
                showdate_duration = date_duration_intervals[date_duration_indx];
                clampShowdateSecond();
            }
            // Fall through to SHOW_DATE_MENU

        case SHOW_DATE_FORMAT:
            if (menu == SHOW_DATE_FORMAT) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = dateFormat;
                date_format_indx = mod(encoderPos, date_format_num_intervals);
                dateFormat = date_format_intervals[date_format_indx];
            }
            // Fall through to SHOW_DATE_MENU

        case SHOW_DATE_MENU: {
            printMenuTitle("DATE OPTIONS");
            char buf[12];

            printMenuLabel("Show Date", menuRowY(0), menu == SHOW_DATE_MENU && mod(encoderPos, 6) == 0);
            printMenuValueRight(showDate ? "On" : "Off", menuRowY(0), menu == SHOW_DATE);

            printMenuLabel("Start at Second", menuRowY(1), menu == SHOW_DATE_MENU && mod(encoderPos, 6) == 1);
            snprintf(buf, sizeof(buf), "%d", showdate_second);
            printMenuValueRight(buf, menuRowY(1), menu == SHOW_DATE_SECOND);

            printMenuLabel("Sec/frame", menuRowY(2), menu == SHOW_DATE_MENU && mod(encoderPos, 6) == 2);
            snprintf(buf, sizeof(buf), "%ds", showdate_duration);
            printMenuValueRight(buf, menuRowY(2), menu == SHOW_DATE_DURATION);

            printMenuLabel("Interval", menuRowY(3), menu == SHOW_DATE_MENU && mod(encoderPos, 6) == 3);
            snprintf(buf, sizeof(buf), "%dmin", showdate_interval);
            printMenuValueRight(buf, menuRowY(3), menu == SHOW_DATE_INTERVAL);

            printMenuLabel("Format", menuRowY(4), menu == SHOW_DATE_MENU && mod(encoderPos, 6) == 4);
            printMenuValueRight(dateFormat == 0 ? "DD:MM" : "MM:DD", menuRowY(4), menu == SHOW_DATE_FORMAT);

            printMenuLabel("Return", menuRowY(5), menu == SHOW_DATE_MENU && mod(encoderPos, 6) == 5);
            break;
        }

        case RING_MODE:
            if (menu == RING_MODE) {
                ringMode = mod(encoderPos, 3);
            }
            // Fall through to RING_MENU

        case RING_BRIGHTNESS:
            if (menu == RING_BRIGHTNESS) {
                ringBrightnessIndx = mod(encoderPos, led_brightness_num_intervals);
                ringBrightnessPercentage = led_brightness_intervals[ringBrightnessIndx];
            }
            // Fall through to RING_MENU

        case RING_COLOR:
            if (menu == RING_COLOR) {
                ringColorIndx = mod(encoderPos, ring_color_num_options);
                updateLEDs();
            }
            // Fall through to RING_MENU (inline color like Static Color)

        case RING_MENU: {
            printMenuTitle("SECONDS RING");

            printMenuLabel("Mode", menuRowY(0), menu == RING_MENU && mod(encoderPos, 4) == 0);
            {
                const char* modeStr = "Off";
                if (ringMode == RING_MODE_FILL) modeStr = "Fill";
                else if (ringMode == RING_MODE_DOT) modeStr = "Dot";
                printMenuValueRight(modeStr, menuRowY(0), menu == RING_MODE);
            }

            printMenuLabel("Brightness", menuRowY(1), menu == RING_MENU && mod(encoderPos, 4) == 1);
            {
                char brightStr[8];
                snprintf(brightStr, sizeof(brightStr), "%d%%", ringBrightnessPercentage);
                printMenuValueRight(brightStr, menuRowY(1), menu == RING_BRIGHTNESS);
            }

            printMenuLabel("Color", menuRowY(2), menu == RING_MENU && mod(encoderPos, 4) == 2);
            if (shouldDrawMenuValue(menuRowY(2))) {
                printColorNameRight(ringColorIndx % ring_color_num_options, menuRowY(2),
                                    menu == RING_COLOR);
            }

            printMenuLabel("Return", menuRowY(3), menu == RING_MENU && mod(encoderPos, 4) == 3);
            break;
        }

        case SCREENSAVER_MENU: {
            printMenuTitle("SCREENSAVER OPTIONS");

            printMenuLabel("Disable", menuRowY(0), mod(encoderPos, 4) == 0);
            printMenuLabel("Bouncing Logo", menuRowY(1), mod(encoderPos, 4) == 1);
            printMenuLabel("Turn Off Display", menuRowY(2), mod(encoderPos, 4) == 2);
            drawScreensaverRadioMarks();

            printMenuLabel("Return", menuRowY(3), mod(encoderPos, 4) == 3);
            break;
        }

        case STATIC_COLOR:
            if (menu == STATIC_COLOR) {
                static_color_indx = mod(encoderPos, static_color_num_colors);
                LED_effect = static_color_indx + 3;
                ledRadioMark = 0xFF;
                updateLEDs();
            }
            // Fall through to LED_MENU

        case LED_BRIGHTNESS:
            if (menu == LED_BRIGHTNESS) {
                led_brightness_indx = mod(encoderPos, led_brightness_num_intervals);
                LedBrightnessPercentage = led_brightness_intervals[led_brightness_indx];
            }
            // Fall through to LED_MENU

        case LED_MENU: {
            printMenuTitle("LED OPTIONS");

            const int ledSel = (menu == LED_MENU) ? mod(encoderPos, 6) : -1;
            printMenuLabelColor("Disable", menuRowY(0), ledSel == 0, COLOR_DIM);
            printMenuLabelRainbow("Rainbow", menuRowY(1), ledSel == 1, 0);
            printMenuLabelRainbow("Color Cycle", menuRowY(2), ledSel == 2,
                                  (uint8_t)((millis() / 40) & 255));
            {
                uint16_t staticFg = COLOR_ACCENT;
                if (LED_effect >= 3 && LED_effect <= 14) {
                    staticFg = kStaticColorTft565[LED_effect - 3];
                } else if (static_color_indx < (uint8_t)static_color_num_colors) {
                    staticFg = kStaticColorTft565[static_color_indx];
                }
                printMenuLabelColor("Static Color", menuRowY(3),
                                    ledSel == 3 || menu == STATIC_COLOR, staticFg);
            }
            drawLedRadioMarks();

            if (shouldDrawMenuValue(menuRowY(3))) {
                if (LED_effect >= 3 && LED_effect <= 14) {
                    printColorNameRight((uint8_t)(LED_effect - 3), menuRowY(3), menu == STATIC_COLOR);
                } else {
                    printMenuValueRightAlways("", menuRowY(3), false);
                }
            }

            printMenuLabel("Brightness", menuRowY(4), ledSel == 4 || menu == LED_BRIGHTNESS);
            {
                char brightStr[8];
                snprintf(brightStr, sizeof(brightStr), "%d%%", LedBrightnessPercentage);
                printMenuValueRight(brightStr, menuRowY(4), menu == LED_BRIGHTNESS);
            }

            printMenuLabel("Return", menuRowY(5), ledSel == 5);
            break;
        }
    }

    if (partialMenuRedraw) {
        display.endWrite();
        partialMenuRedraw = false;
        skipMenuLabels = false;
        skipMenuValues = false;
        valuesIgnoreRowFilter = false;
    }
    // Latch after drawing so mid-frame ledRadioMark updates (STATIC_COLOR) stay in sync.
    lastUiLedRadioMark = ledRadioMark;
    lastUiSsOption = ssOption;
    // Latch encoder after UI handling so a later button press is not treated as a scroll.
    if (isNavListMenu(menu)) {
        encoderPosPrev = encoderPos;
    }
}

/**
 * Convert 15-minute intervals to hours and minutes
 */
void fifteenMinToHM(int& hours, int& minutes, int fifteenMin) {
    hours = fifteenMin / 4;
    minutes = (fifteenMin % 4) * 15;
}

/**
 * Format a 15-minute slot for the auto shut-off menu.
 * Uses 24h or 12h+AM/PM according to set12_24 (storage stays 0..95).
 */
void formatShutoffTime(char* buf, size_t buflen, int fifteenMin) {
    int hr, mn;
    fifteenMinToHM(hr, mn, fifteenMin);
    if (set12_24) {
        snprintf(buf, buflen, "%d%s%d", hr, colonDigit(mn), mn);
    } else {
        int h12 = hr % 12;
        if (h12 == 0) h12 = 12;
        snprintf(buf, buflen, "%d%s%d %s", h12, colonDigit(mn), mn, hr < 12 ? "AM" : "PM");
    }
}

/**
 * Reset MultiWiFi credentials (FFat + ESP flash) and restart.
 * After reboot the captive portal opens so new networks can be saved.
 */
void resetWiFi() {
    displayClear();
    composeMenuLineLeft(DISP_MARGIN_Y, "Resetting Wifi...");
    composeMenuLineLeft(menuRowY(1), "Device will restart");
    composeMenuLineLeft(menuRowY(2), "and open portal.");
    delay(800);
    wifiProvisionReset();
}

/**
 * Centered menu title on row y=0.
 */
void printMenuTitle(const char* title) {
    if (partialMenuRedraw) {
        return;
    }
    composeMenuLineCenteredColor(DISP_MARGIN_Y, title, COLOR_ACCENT);
}

static void printMenuLabelColor(const char* label, int16_t y, bool highlight, uint16_t fg) {
    if (skipMenuLabels || !menuRowVisible(y)) {
        return;
    }
    tftPrintMenuAtInBand(display, DISP_MARGIN_X, MENU_LABEL_CLEAR_W, menuLabelX(), y, label,
                         highlight, fg, COLOR_BG);
}

static void printMenuLabelRainbowAlways(const char* label, int16_t y, bool highlight,
                                        uint8_t hueOffset) {
    if (highlight) {
        tftPrintMenuAtInBand(display, DISP_MARGIN_X, MENU_LABEL_CLEAR_W, menuLabelX(), y, label,
                             true, COLOR_FG, COLOR_BG);
        return;
    }
    const int len = (int)strlen(label);
    int colored = 0;
    for (int i = 0; i < len; i++) {
        if (label[i] != ' ') {
            colored++;
        }
    }
    if (colored < 1) {
        colored = 1;
    }
    RainbowColorCtx ctx = {hueOffset, colored};
    tftComposeTextBandColored(DISP_MARGIN_X, y, MENU_LABEL_CLEAR_W, TFT_MENU_ROW_STEP,
                              menuLabelX(), label, COLOR_FG, COLOR_BG, false, 0,
                              tftMenuFontTopOffset(), &FreeSans9pt7b, rainbowGlyphColor, &ctx);
}

static void printMenuLabelRainbow(const char* label, int16_t y, bool highlight, uint8_t hueOffset) {
    if (skipMenuLabels || !menuRowVisible(y)) {
        return;
    }
    printMenuLabelRainbowAlways(label, y, highlight, hueOffset);
}

/**
 * Left-aligned menu label on the given row.
 */
void printMenuLabel(const char* label, int16_t y, bool highlight) {
    printMenuLabelColor(label, y, highlight, COLOR_FG);
}

/**
 * Print a menu value right-aligned on the given row.
 * Skipped during highlight-only nav scrolls so values do not flicker.
 */
void printMenuValueRight(const char* value, int16_t y, bool highlight) {
    if (skipMenuValues) {
        return;
    }
    if (!valuesIgnoreRowFilter && !menuRowVisible(y)) {
        return;
    }
    uint16_t fg = COLOR_FG;
    if (!highlight && value != nullptr) {
        if (strcmp(value, "On") == 0) {
            fg = COLOR_ON;
        } else if (strcmp(value, "Off") == 0) {
            fg = COLOR_OFF;
        }
    }
    printMenuValueRightAlwaysColor(value, y, highlight, fg);
}

/**
 * Calculate modulo with positive result
 */
inline int mod(int a, int b) {
    int r = a % b;
    return r < 0 ? r + b : r;
}

/**
 * Callback when the MultiWiFi captive portal is active
 */
void configModeCallback(ESP_WiFiManager* myWiFiManager) {
    (void)myWiFiManager;
    displayClear();
    composeMenuLineLeft(DISP_MARGIN_Y, "To configure Wifi,");
    composeMenuLineLeft(menuRowY(0), "connect to Wifi");
    char line[40];
    snprintf(line, sizeof(line), "network: %s", AP_NAME);
    composeMenuLineLeft(menuRowY(1), line);
    snprintf(line, sizeof(line), "password: %s", AP_PASSWORD);
    composeMenuLineLeft(menuRowY(2), line);
    composeMenuLineLeft(menuRowY(3), "Open 192.168.4.1");
    composeMenuLineLeft(menuRowY(4), "in web browser");
    composeMenuLineLeft(menuRowY(5), "Times out in 3 min");
}
