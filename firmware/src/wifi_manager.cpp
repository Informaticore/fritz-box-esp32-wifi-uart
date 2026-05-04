/**
 * @file wifi_manager.cpp
 * @brief WiFi connection manager implementation.
 *
 * See wifi_manager.h for the full strategy description.
 */
#include "wifi_manager.h"
#include "config.h"
#include <Preferences.h>
#include <LittleFS.h>

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

    // Bring the radio to a clean off state first.  This is necessary because
    // tryConnect() calls WiFi.disconnect(true) which powers the radio off via
    // WIFI_OFF.  Switching directly from WIFI_OFF (or a failed STA attempt) to
    // WIFI_AP without a settling delay causes softAP() to silently fail, so
    // the network never actually broadcasts.
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_AP);
    delay(100);

    bool ok = WiFi.softAP(FALLBACK_AP_SSID, FALLBACK_AP_PASSWORD);
    if (!ok) {
        Serial.println(F("[WiFi] ERROR: softAP() failed – AP was not started!"));
        return;
    }

    Serial.printf("[WiFi] AP mode started – SSID: %s  IP: %s\n",
                  FALLBACK_AP_SSID,
                  WiFi.softAPIP().toString().c_str());
    Serial.println(F("[WiFi] Connect to the AP and open the IP in a browser"));
}

bool WiFiManager::loadFileCredentials(String& ssid, String& password) {
    // Mount LittleFS (false = don't format if mount fails).
    // If there is no LittleFS partition the call returns false and we simply
    // skip the file-based credentials without any side-effects.
    if (!LittleFS.begin(false)) {
        return false;
    }

    File f = LittleFS.open(WIFI_CREDS_FILE, "r");
    if (!f) {
        LittleFS.end();
        return false;
    }

    // Read the entire file at once to avoid repeated String allocations from
    // calling readStringUntil() in a tight loop.
    String contents = f.readString();
    f.close();
    LittleFS.end();

    bool foundSsid = false;
    int  pos = 0;
    while (pos < (int)contents.length()) {
        int nl = contents.indexOf('\n', pos);
        if (nl < 0) nl = contents.length();

        String line = contents.substring(pos, nl);
        line.trim();
        pos = nl + 1;

        // Skip blank lines and comment lines
        if (line.isEmpty() || line.startsWith("#")) continue;

        int eq = line.indexOf('=');
        if (eq < 0) continue;

        String key = line.substring(0, eq);
        String val = line.substring(eq + 1);
        key.trim();
        val.trim();

        if (key.equalsIgnoreCase("ssid")) {
            ssid = val;
            foundSsid = true;
        } else if (key.equalsIgnoreCase("password")) {
            password = val;
        }
    }

    if (foundSsid && !ssid.isEmpty()) {
        Serial.printf("[WiFi] Loaded credentials from file for \"%s\"\n",
                      ssid.c_str());
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

bool WiFiManager::connect() {
    // --- Step 1: Use NVS-stored credentials ---
    String storedSsid, storedPass;
    if (loadStoredCredentials(storedSsid, storedPass)) {
        if (tryConnect(storedSsid, storedPass)) {
            return true;
        }
        Serial.println(F("[WiFi] Stored credentials did not work"));
    }

    // --- Step 2: Use LittleFS wifi.txt file credentials ---
    // File credentials are intentionally NOT saved to NVS; they remain a
    // separate, file-only source read fresh on every boot.
    {
        String fileSsid, filePass;
        if (loadFileCredentials(fileSsid, filePass)) {
            if (tryConnect(fileSsid, filePass)) {
                return true;
            }
            Serial.println(F("[WiFi] File credentials did not work"));
        }
    }

    // --- Step 3: Open fallback AP ---
    Serial.println(F("[WiFi] All connection attempts failed; starting AP mode"));
    startAPMode();
    return false;
}

bool WiFiManager::isConnected() const {
    return !_apMode && (WiFi.status() == WL_CONNECTED);
}

bool WiFiManager::isApMode() const {
    return _apMode;
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
    // tear it down so we can switch to STA mode cleanly.
    if (_apMode) {
        WiFi.softAPdisconnect(true);
        _apMode = false;
        delay(100);
    }
    WiFi.mode(WIFI_OFF);
    delay(100);
    if (tryConnect(ssid, password)) {
        return true;
    }
    // Connection failed – go back to AP mode so the web UI stays reachable
    Serial.println(F("[WiFi] Manual credentials did not work; returning to AP mode"));
    startAPMode();
    return false;
}
