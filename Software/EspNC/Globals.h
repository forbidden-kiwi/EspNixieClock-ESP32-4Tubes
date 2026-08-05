#ifndef Globals_h
#define Globals_h

// Waveshare ESP32-C6-LCD-1.47 pin map (onboard ST7789 uses GPIO6/7/14/15/21/22)
// Avoid: GPIO0 (boot), GPIO4/5 (TF), GPIO6/7/14/15/21/22 (LCD), GPIO8 (RGB), GPIO12/13 (USB)

// Onboard TFT (ST7789) — fixed on Waveshare board
#define PIN_TFT_CS   14
#define PIN_TFT_DC   15
#define PIN_TFT_RST  21
#define PIN_TFT_MOSI 6
#define PIN_TFT_SCLK 7
#define PIN_TFT_BL   22

// TFT backlight PWM (~50%; Waveshare recommends <=50%, full HIGH runs hot)
#define BL_PWM_FREQ_HZ 1000
#define BL_PWM_RES_BITS 10
#define BL_PWM_DUTY_ON 512

// Pin definitions for HV5622 shift registers
#define PIN_HV_LE   3
#define PIN_HV_BL   5
#define PIN_HV_DATA 2
#define PIN_HV_CLK  4

// Pin definition for DC/DC 170V enable
#define PIN_HV_EN 1

// WS2812 strip: 4 tube backlights, then 60-LED seconds ring (same data line)
#define PIN_WS2812 9
#define TUBE_LED_COUNT 4
#define RING_LED_COUNT 60
#define LED_COUNT (TUBE_LED_COUNT + RING_LED_COUNT)

// Rotary encoder
#define PIN_ENCODER_A      18
#define PIN_ENCODER_B      19
#define PIN_ENCODER_BUTTON 20

// How much of the 0–255 color wheel the tube rainbow spans (open gradient).
#ifndef RAINBOW_SPAN
#define RAINBOW_SPAN 64
#endif

#endif
