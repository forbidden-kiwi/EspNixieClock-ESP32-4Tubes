#ifndef WifiProvision_h
#define WifiProvision_h

#include <Arduino.h>

class ESP_WiFiManager;

typedef void (*WifiPortalCallback)(ESP_WiFiManager* wifiManager);

/**
 * Start MultiWiFi provisioning with captive portal (ESP_WiFiManager).
 * Tries saved networks first (~15 s); opens portal if none connect.
 * Returns true if connected after setup.
 */
bool wifiProvisionBegin(const char* apName, const char* apPassword,
                        unsigned long portalTimeoutSec,
                        WifiPortalCallback onPortal);

/** True while station is associated. */
bool wifiProvisionConnected();

/**
 * Non-blocking WiFi maintenance for loop().
 * Advances an in-progress reconnect; never waits on scans/delays.
 */
void wifiProvisionLoop();

/**
 * Kick off a non-blocking reconnect using saved MultiWiFi credentials.
 * Safe to call repeatedly; ignored if already connected or connecting.
 * Intended when an NTP sync is due and the link is down.
 */
void wifiProvisionRequestReconnect();

/**
 * True if no reconnect is in progress (idle: connected, or gave up for now).
 */
bool wifiProvisionReconnectIdle();

/**
 * Clear FFat MultiWiFi credentials and ESP flash WiFi settings, then restart.
 * Used by the "Reset Wifi" menu item.
 */
void wifiProvisionReset();

#endif
