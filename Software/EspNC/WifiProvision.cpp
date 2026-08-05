#include "WifiProvision.h"

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <FFat.h>

// ESP_WiFiManager compile-time options (must precede the include)
#define USE_LITTLEFS                  false
#define USE_AVAILABLE_PAGES           false
#define USE_ESP_WIFIMANAGER_NTP       false
#define USE_CLOUDFLARE_NTP            false
#define USING_CORS_FEATURE            false
#define USE_DHCP_IP                   true
#define USE_CONFIGURABLE_DNS          false
#define USE_CUSTOM_AP_IP              false
#define DISPLAY_STORED_CREDENTIALS_IN_CP true

#define USING_AFRICA                  false
#define USING_AMERICA                 false
#define USING_ANTARCTICA              false
#define USING_ASIA                    false
#define USING_ATLANTIC                false
#define USING_AUSTRALIA               false
#define USING_EUROPE                  false
#define USING_INDIAN                  false
#define USING_PACIFIC                 false
#define USING_ETC_GMT                 false

// ESP32 Arduino Core 3.x removed WiFi.get/setAutoConnect. ESP_WiFiManager
// (header-only) still calls them — map to AutoReconnect without patching the library.
#if defined(ESP32)
#define getAutoConnect getAutoReconnect
#define setAutoConnect setAutoReconnect
#endif
#include <ESP_WiFiManager.h>
#if defined(ESP32)
#undef getAutoConnect
#undef setAutoConnect
#endif

// ESP_WiFiManager portal exposes two equal MultiWiFi credential slots
#define NUM_WIFI_CREDENTIALS  2
#define SSID_MAX_LEN          32
#define PASS_MAX_LEN          64
#define CONFIG_FILENAME       "/wifi_cred.dat"
#define WIFI_BOOT_CONNECT_MS  15000UL
#define WIFI_POST_PORTAL_CONNECT_MS  25000UL
#define WIFI_MULTI_1ST_WAIT_MS  2200UL
#define WIFI_MULTI_RETRY_MS     500UL
// Per-credential budget while non-blocking reconnect runs in loop()
#define WIFI_RECONNECT_TIMEOUT_MS  12000UL

typedef struct {
    char wifi_ssid[SSID_MAX_LEN];
    char wifi_pw[PASS_MAX_LEN];
} WiFi_Credentials;

typedef struct {
    WiFi_Credentials WiFi_Creds[NUM_WIFI_CREDENTIALS];
    uint16_t checksum;
} WM_Config;

enum WifiReconnectState : uint8_t {
    WIFI_RECON_IDLE = 0,
    WIFI_RECON_CONNECTING
};

static WiFiMulti wifiMulti;
static WM_Config WM_config;
static WifiReconnectState reconState = WIFI_RECON_IDLE;
static uint8_t reconCredIndex = 0;
static unsigned long reconStartMs = 0;

static uint16_t calcChecksum(const uint8_t* data, uint16_t len) {
    uint16_t sum = 0;
    while (len--) {
        sum += *data++;
    }
    return sum;
}

static bool loadConfigData() {
    memset(&WM_config, 0, sizeof(WM_config));

    File file = FFat.open(CONFIG_FILENAME, "r");
    if (!file) {
        return false;
    }

    size_t n = file.readBytes((char*)&WM_config, sizeof(WM_config));
    file.close();

    if (n != sizeof(WM_config)) {
        memset(&WM_config, 0, sizeof(WM_config));
        return false;
    }

    uint16_t expected = calcChecksum((uint8_t*)&WM_config,
                                     sizeof(WM_config) - sizeof(WM_config.checksum));
    if (WM_config.checksum != expected) {
        memset(&WM_config, 0, sizeof(WM_config));
        return false;
    }

    return true;
}

static void saveConfigData() {
    WM_config.checksum = calcChecksum((uint8_t*)&WM_config,
                                      sizeof(WM_config) - sizeof(WM_config.checksum));

    File file = FFat.open(CONFIG_FILENAME, "w");
    if (!file) {
        return;
    }
    file.write((uint8_t*)&WM_config, sizeof(WM_config));
    file.close();
}

static void clearConfigData() {
    memset(&WM_config, 0, sizeof(WM_config));
    if (FFat.exists(CONFIG_FILENAME)) {
        FFat.remove(CONFIG_FILENAME);
    }
}

static bool credentialUsable(const WiFi_Credentials& cred) {
    return cred.wifi_ssid[0] != '\0';
}

static bool anyCredentialUsable() {
    for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        if (credentialUsable(WM_config.WiFi_Creds[i])) {
            return true;
        }
    }
    return false;
}

static int addStoredAPs() {
    int count = 0;
    for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        if (credentialUsable(WM_config.WiFi_Creds[i])) {
            wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid,
                            WM_config.WiFi_Creds[i].wifi_pw);
            count++;
        }
    }
    return count;
}

/** Drop any prior MultiWiFi entries and reload from WM_config only. */
static int rebuildStoredAPs() {
    wifiMulti.APlistClean();
    return addStoredAPs();
}

/**
 * Leave SoftAP / AP_STA leftovers from the captive portal and run STA-only.
 */
static void leavePortalToSta() {
    WiFi.softAPdisconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    delay(100);
}

static void enableBackgroundReconnect() {
    WiFi.persistent(true);
    WiFi.setAutoReconnect(true);
}

/**
 * Start async STA connect to credential index (WiFi.begin is non-blocking).
 * Returns false if index is past the last usable credential.
 */
static bool beginCredentialAsync(uint8_t index) {
    while (index < NUM_WIFI_CREDENTIALS) {
        if (credentialUsable(WM_config.WiFi_Creds[index])) {
            WiFi.mode(WIFI_STA);
            WiFi.begin(WM_config.WiFi_Creds[index].wifi_ssid,
                       WM_config.WiFi_Creds[index].wifi_pw);
            reconCredIndex = index;
            reconStartMs = millis();
            reconState = WIFI_RECON_CONNECTING;
            return true;
        }
        index++;
    }
    reconState = WIFI_RECON_IDLE;
    return false;
}

/** Blocking MultiWiFi connect for setup only. */
static uint8_t connectMultiWiFi(unsigned long budgetMs) {
    const unsigned long deadline = millis() + budgetMs;

    wifiMulti.run();
    delay(WIFI_MULTI_1ST_WAIT_MS);

    uint8_t status = wifiMulti.run();
    while ((status != WL_CONNECTED) && ((long)(deadline - millis()) > 0)) {
        delay(WIFI_MULTI_RETRY_MS);
        status = wifiMulti.run();
    }
    if (status == WL_CONNECTED) {
        enableBackgroundReconnect();
    }
    return status;
}

static void copyCredentialsFromManager(ESP_WiFiManager& wm) {
    memset(&WM_config, 0, sizeof(WM_config));

    for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        String ssid = wm.getSSID(i);
        String pw = wm.getPW(i);

        strncpy(WM_config.WiFi_Creds[i].wifi_ssid, ssid.c_str(),
                sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1);
        strncpy(WM_config.WiFi_Creds[i].wifi_pw, pw.c_str(),
                sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1);
    }
}

/**
 * After captive portal: persist credentials, drop SoftAP, rebuild MultiWiFi,
 * then always attempt a clean STA connect (portal AP_STA can be flaky).
 */
static void finishAfterPortal(ESP_WiFiManager& wifiManager) {
    copyCredentialsFromManager(wifiManager);
    if (!anyCredentialUsable()) {
        leavePortalToSta();
        return;
    }

    saveConfigData();
    leavePortalToSta();
    WiFi.disconnect(false, false);
    delay(100);
    rebuildStoredAPs();
    connectMultiWiFi(WIFI_POST_PORTAL_CONNECT_MS);
}

bool wifiProvisionBegin(const char* apName, const char* apPassword,
                        unsigned long portalTimeoutSec,
                        WifiPortalCallback onPortal) {
    if (!FFat.begin(true)) {
        return false;
    }

    bool configLoaded = loadConfigData();
    int storedCount = 0;
    if (configLoaded) {
        storedCount = addStoredAPs();
    }

    ESP_WiFiManager wifiManager(apName);
    wifiManager.setDebugOutput(false);
    wifiManager.setConfigPortalChannel(0);
    wifiManager.setMinimumSignalQuality(-1);

    if (onPortal) {
        wifiManager.setAPCallback(onPortal);
    }

    if (configLoaded) {
        wifiManager.setCredentials(
            WM_config.WiFi_Creds[0].wifi_ssid, WM_config.WiFi_Creds[0].wifi_pw,
            WM_config.WiFi_Creds[1].wifi_ssid, WM_config.WiFi_Creds[1].wifi_pw);
    }

    // Prefer saved MultiWiFi networks before opening the portal
    if (storedCount > 0) {
        if (connectMultiWiFi(WIFI_BOOT_CONNECT_MS) == WL_CONNECTED) {
            return true;
        }
    }

    // Also try ESP flash-stored credentials (migration from classic WiFiManager)
    String flashSsid = wifiManager.WiFi_SSID();
    String flashPass = wifiManager.WiFi_Pass();
    if (flashSsid.length() > 0) {
        wifiMulti.APlistClean();
        wifiMulti.addAP(flashSsid.c_str(), flashPass.c_str());
        if (connectMultiWiFi(WIFI_BOOT_CONNECT_MS) == WL_CONNECTED) {
            if (storedCount == 0) {
                strncpy(WM_config.WiFi_Creds[0].wifi_ssid, flashSsid.c_str(),
                        sizeof(WM_config.WiFi_Creds[0].wifi_ssid) - 1);
                strncpy(WM_config.WiFi_Creds[0].wifi_pw, flashPass.c_str(),
                        sizeof(WM_config.WiFi_Creds[0].wifi_pw) - 1);
                saveConfigData();
            }
            return true;
        }
    }

    wifiManager.setConfigPortalTimeout(portalTimeoutSec);
    wifiManager.startConfigPortal(apName, apPassword);
    finishAfterPortal(wifiManager);

    if (WiFi.status() == WL_CONNECTED) {
        enableBackgroundReconnect();
    }
    return WiFi.status() == WL_CONNECTED;
}

bool wifiProvisionConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool wifiProvisionReconnectIdle() {
    return reconState == WIFI_RECON_IDLE;
}

void wifiProvisionRequestReconnect() {
    if (WiFi.status() == WL_CONNECTED) {
        reconState = WIFI_RECON_IDLE;
        return;
    }
    if (reconState == WIFI_RECON_CONNECTING) {
        return;
    }
    beginCredentialAsync(0);
}

void wifiProvisionLoop() {
    if (WiFi.status() == WL_CONNECTED) {
        reconState = WIFI_RECON_IDLE;
        return;
    }

    if (reconState != WIFI_RECON_CONNECTING) {
        return;
    }

    // Still waiting on WiFi.begin() — do not call wifiMulti.run() (blocking scan)
    if ((millis() - reconStartMs) < WIFI_RECONNECT_TIMEOUT_MS) {
        return;
    }

    // This credential timed out; try the next saved SSID
    if (!beginCredentialAsync(reconCredIndex + 1)) {
        reconState = WIFI_RECON_IDLE;
    }
}

void wifiProvisionReset() {
    if (!FFat.begin(true)) {
        FFat.begin(true);
    }
    clearConfigData();

    ESP_WiFiManager wifiManager;
    wifiManager.resetSettings();

    delay(1000);
    ESP.restart();
}
