# EspNixieClock-ESP32-4Tubes

[![GPL license](https://img.shields.io/badge/license-GPL--3.0-blue)](https://github.com/forbidden-kiwi/EspNixieClock-ESP32-4Tubes?tab=GPL-3.0-1-ov-file#)
[![Maintenance](https://img.shields.io/badge/maintained-yes-green)](https://github.com/forbidden-kiwi/EspNixieClock-ESP32-4Tubes/graphs/commit-activity)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-green)](https://github.com/forbidden-kiwi/EspNixieClock-ESP32-4Tubes/pulls)
[![GitHub release](https://img.shields.io/github/v/release/forbidden-kiwi/EspNixieClock-ESP32-4Tubes?color=blue)](https://github.com/forbidden-kiwi/EspNixieClock-ESP32-4Tubes/releases)
[![GitHub contributors](https://img.shields.io/github/contributors/forbidden-kiwi/EspNixieClock-ESP32-4Tubes?color=yellow)](https://github.com/forbidden-kiwi/EspNixieClock-ESP32-4Tubes/graphs/contributors)

![EspNixieClock](Images/Nixie%20Clock.JPEG)
![EspNixieClock](Images/Nixie%20Clock%20Front.JPEG)

WiFi Nixie NTP clock firmware for a **4-tube** display, running on **Waveshare ESP32-C6-LCD-1.47** (onboard ST7789 TFT), with WS2812 tube backlight, seconds ring, and rotary encoder.

Companion project: [EspNixieClock-ESP8266-6Tubes](https://github.com/forbidden-kiwi/EspNixieClock-ESP8266-6Tubes) (6 tubes, OLED).

# Features

- ESP32-based (Arduino IDE), board: **Waveshare ESP32-C6-LCD-1.47** (ST7789 320×172)
- NTP time synchronisation
- Captive-portal WiFi setup — **store up to 2 WiFi credentials** (MultiWiFi / ESP_WiFiManager)
- TFT + encoder menus: timezone, Auto DST (regional rules), 12/24h, colon blink, leading zero
- WS2812 tube backlight: rainbow, color cycle, static colors, brightness
- Independent 60-LED seconds ring: off / fill / dot, color and brightness
- Auto shutoff (night mode), cathode protection (slot machine), optional roll-down digits
- Date on the tubes (two frames) with configurable interval and format
- TFT screensaver (bouncing logo or display off); first rotate/press returns to TOP
- Manual set time/date or sync via WiFi; settings stored in EEPROM

### Getting started

1. Download the latest [release ZIP](https://github.com/forbidden-kiwi/EspNixieClock-ESP32-4Tubes/releases) (includes libraries).
2. Copy `Software/libraries/*` into your Arduino sketchbook `libraries` folder.
3. Open `Software/EspNC/EspNC.ino` in Arduino IDE.
4. Select the Arduino IDE **Tools** settings for the Waveshare ESP32-C6 LCD board (see below), then upload.
5. Connect to the WiFi portal shown on the TFT and save up to two networks.

Pin map: `Software/EspNC/Globals.h`.

#### Arduino IDE — board settings (Waveshare ESP32-C6-LCD-1.47)

![Arduino IDE Tools settings](Images/Arduino_IDE_ESP32-C6_LCD_1.47_Tools_Settings.webp)

Use at least:

| Setting | Value |
|--------|--------|
| Board | **ESP32C6 Dev Module** |
| USB CDC On Boot | **Enabled** |
| Flash Size | **8MB (64Mb)** (as on Waveshare) |

WiFi credentials in this firmware are stored on **FFat**. Prefer a partition scheme that includes FATFS (for example **No OTA (2MB APP / 2MB FATFS)**). The Waveshare demo screenshot above shows an SPIFFS scheme — that works for their demos, but EspNC needs FFat for MultiWiFi.

Reference image from the [Waveshare ESP32-C6 LCD documentation](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47).

### License

GNU General Public License v3.0

### Menu

![TOP](Images/Display%20TOP%20Menu.JPEG)
![Settings](Images/Settings%20Page.JPEG)
![DST](Images/DST%20Menu%20Screen.JPEG)
![Backlight](Images/Nixie%20Backlight%20Option%20Screen.JPEG)
![Seconds ring](Images/Seconds%20Ring%20Options%20Screen.JPEG)
![Date](Images/Date%20Options%20Sreen.JPEG)
![Screensaver](Images/Screensaver%20Options%20Menu.JPEG)
