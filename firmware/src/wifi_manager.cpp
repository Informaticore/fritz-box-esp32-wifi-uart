/**
 * @file wifi_manager.cpp
 * @brief WiFi connection manager implementation.
 *
 * See wifi_manager.h for the full strategy description.
 */
#include "wifi_manager.h"
#include "config.h"
#include <Preferences.h>

// NVS namespace and key names
static const char* NVS_NAMESPACE = "wificreds";
static const char* NVS_KEY_SSID  = "ssid";
static const char* NVS_KEY_PASS  = "pass";

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool WiFiManager::tryConnect(const String& ssid,
                              const String& password,
                              uint32_t      timeoutMs) {
    if (ssid.isEmpty()) {
        Serial.println(F("[WiFi] tryConnect: empty SSID – skipping"));
        return false;
    }

    Serial.printf("[WiFi] Connecting to \"%s\" ...\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if ((millis() - start) > timeoutMs) {
            Serial.println(F("\n[WiFi] Connection timed out"));
            WiFi.disconnect(true);
            return false;
        }
        delay(500);
        Serial.print('.');
    }
    Serial.printf("\n[WiFi] Connected!  IP: %s\n",
                  WiFi.localIP().toString().c_str());
    return true;
}

bool WiFiManager::loadStoredCredentials(String& ssid, String& password) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
    ssid     = prefs.getString(NVS_KEY_SSID, "");
    password = prefs.getString(NVS_KEY_PASS, "");
    prefs.end();

    if (!ssid.isEmpty()) {
        Serial.printf("[WiFi] Loaded stored credentials for \"%s\"\n",
                      ssid.c_str());
        return true;
    }
    Serial.println(F("[WiFi] No stored credentials found"));
    return false;
}

void WiFiManager::saveCredentials(const String& ssid, const String& password) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
    prefs.putString(NVS_KEY_SSID, ssid);
    prefs.putString(NVS_KEY_PASS, password);
    prefs.end();
    Serial.println(F("[WiFi] Credentials saved to NVS"));
}

void WiFiManager::startAPMode() {
    _apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(FALLBACK_AP_SSID, FALLBACK_AP_PASSWORD);
    Serial.printf("[WiFi] AP mode started – SSID: %s  IP: %s\n",
                  FALLBACK_AP_SSID,
                  WiFi.softAPIP().toString().c_str());
    Serial.println(F("[WiFi] Connect to the AP and open the IP in a browser"));
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

bool WiFiManager::connect(const WifiCredentials& creds) {
    // --- Step 1: Use UART-extracted credentials ---
    if (creds.valid) {
        if (tryConnect(creds.ssid, creds.password)) {
            saveCredentials(creds.ssid, creds.password);
            return true;
        }
        Serial.println(F("[WiFi] UART credentials did not work"));
    }

    // --- Step 2: Use NVS-stored credentials ---
    String storedSsid, storedPass;
    if (loadStoredCredentials(storedSsid, storedPass)) {
        if (tryConnect(storedSsid, storedPass)) {
            return true;
        }
        Serial.println(F("[WiFi] Stored credentials did not work"));
    }

    // --- Step 3: Open fallback AP ---
    Serial.println(F("[WiFi] All connection attempts failed; starting AP mode"));
    startAPMode();
    return false;
}

bool WiFiManager::isConnected() const {
    return !_apMode && (WiFi.status() == WL_CONNECTED);
}

String WiFiManager::ipAddress() const {
    if (_apMode) return WiFi.softAPIP().toString();
    return WiFi.localIP().toString();
}

String WiFiManager::connectedSSID() const {
    if (_apMode) return String(FALLBACK_AP_SSID);
    return WiFi.SSID();
}

bool WiFiManager::setManualCredentials(const String& ssid,
                                        const String& password) {
    if (ssid.isEmpty()) {
        Serial.println(F("[WiFi] setManualCredentials: empty SSID – rejected"));
        return false;
    }
    Serial.printf("[WiFi] Manual credentials received for \"%s\"\n",
                  ssid.c_str());
    saveCredentials(ssid, password);

    // Try to connect.  If we are currently in AP mode we first need to
    // tear it down so we can switch to STA mode.
    if (_apMode) {
        WiFi.softAPdisconnect(true);
        _apMode = false;
    }
    if (tryConnect(ssid, password)) {
        return true;
    }
    // Connection failed – go back to AP mode so the web UI stays reachable
    Serial.println(F("[WiFi] Manual credentials did not work; returning to AP mode"));
    startAPMode();
    return false;
}
