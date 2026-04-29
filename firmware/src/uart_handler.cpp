/**
 * @file uart_handler.cpp
 * @brief FRITZ!Box UART driver implementation.
 *
 * See uart_handler.h for a full description of the design.
 */
#include "uart_handler.h"
#include "config.h"

// Convenience alias so the code compiles for any board using Serial2
#define FRITZ_UART_PORT Serial2

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

/**
 * Parse a line of the form  KEY=value  or  KEY:value  and return the value
 * (trimmed).  Returns true when the key was found and the value is
 * non-empty.
 */
static bool extractValue(const String& line,
                          const String& key,
                          String&       value) {
    // Case-insensitive search for the key token
    String lineLower = line;
    lineLower.toLowerCase();
    String keyLower = key;
    keyLower.toLowerCase();

    int idx = lineLower.indexOf(keyLower);
    if (idx < 0) return false;

    // Find the separator ('=' or ':') that follows the key token
    int sep = -1;
    for (int i = idx + (int)key.length(); i < (int)line.length(); ++i) {
        if (line[i] == '=' || line[i] == ':') {
            sep = i;
            break;
        }
    }
    if (sep < 0) return false;

    value = line.substring(sep + 1);
    value.trim();
    return !value.isEmpty();
}

// ---------------------------------------------------------------------------
// UartHandler::begin
// ---------------------------------------------------------------------------

void UartHandler::begin() {
    FRITZ_UART_PORT.begin(FRITZ_UART_BAUD, FRITZ_UART_CONFIG,
                          FRITZ_UART_RX_PIN, FRITZ_UART_TX_PIN);
    Serial.println(F("[UART] Serial2 initialised for FRITZ!Box console"));
}

// ---------------------------------------------------------------------------
// UartHandler::loop  –  non-blocking byte pump
// ---------------------------------------------------------------------------

void UartHandler::loop() {
    while (FRITZ_UART_PORT.available()) {
        char c = static_cast<char>(FRITZ_UART_PORT.read());
        if (c == '\n') {
            if (_lineBuf.length() > 0) {
                addLogLine(_lineBuf);
                _lineBuf.clear();
            }
        } else if (c != '\r') {
            if (_lineBuf.length() < LOG_LINE_MAX_LEN) {
                _lineBuf += c;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Log ring-buffer management
// ---------------------------------------------------------------------------

void UartHandler::addLogLine(const String& line) {
    if (_logBuffer.size() >= LOG_BUFFER_LINES) {
        _logBuffer.pop_front();
    }
    _logBuffer.push_back(line);
}

std::vector<String> UartHandler::getLogLines(size_t maxLines) const {
    std::vector<String> result;
    size_t start = (_logBuffer.size() > maxLines)
                   ? (_logBuffer.size() - maxLines)
                   : 0;
    result.reserve(_logBuffer.size() - start);
    for (size_t i = start; i < _logBuffer.size(); ++i) {
        result.push_back(_logBuffer[i]);
    }
    return result;
}

String UartHandler::getLogAsString(size_t maxLines) const {
    String out;
    auto lines = getLogLines(maxLines);
    for (const auto& l : lines) {
        out += l;
        out += '\n';
    }
    return out;
}

// ---------------------------------------------------------------------------
// Marker-based command / response protocol
// ---------------------------------------------------------------------------

/**
 * Reads from Serial2 until @p endMarker appears in the accumulated text or
 * the timeout fires.  Bytes are also fed into the normal log-line buffer so
 * that the ring-buffer stays up to date even during a command wait.
 */
String UartHandler::collectUntilMarker(const String& endMarker,
                                       uint32_t      timeoutMs) {
    String   captured;
    String   window;        // rolling window used for marker detection
    uint32_t start     = millis();
    uint32_t markerLen = endMarker.length();

    while ((millis() - start) < timeoutMs) {
        while (FRITZ_UART_PORT.available()) {
            char c = static_cast<char>(FRITZ_UART_PORT.read());

            // Feed into log line accumulator
            if (c == '\n') {
                if (_lineBuf.length() > 0) {
                    addLogLine(_lineBuf);
                    _lineBuf.clear();
                }
            } else if (c != '\r') {
                if (_lineBuf.length() < LOG_LINE_MAX_LEN) {
                    _lineBuf += c;
                }
            }

            // Append to captured output
            captured += c;

            // Keep a rolling window just long enough to detect the marker
            window += c;
            if (window.length() > markerLen + 4) {
                window.remove(0, window.length() - (markerLen + 4));
            }
            if (window.indexOf(endMarker) >= 0) {
                return captured;    // found – done
            }
        }
        delay(1);   // yield to the RTOS scheduler while waiting
    }
    return captured;    // timed out
}

CommandResult UartHandler::sendCommand(const String& command,
                                       uint32_t      timeoutMs) {
    CommandResult result;
    result.success  = false;
    result.timedOut = false;

    // Discard any stale bytes in the hardware RX buffer
    while (FRITZ_UART_PORT.available()) {
        FRITZ_UART_PORT.read();
    }

    // Build the marker-wrapped command
    // Result on the console will look like:
    //   ~~FBCMD_START~~
    //   <command output lines>
    //   ~~FBCMD_END~~
    String wrapped = String(F("echo \"")) + CMD_START_MARKER + F("\"; ")
                   + command
                   + F("; echo \"") + CMD_END_MARKER + F("\"\n");

    Serial.printf("[UART] Sending: %s", wrapped.c_str());
    FRITZ_UART_PORT.print(wrapped);

    // Wait for the START marker
    String beforeStart = collectUntilMarker(CMD_START_MARKER, timeoutMs);
    if (beforeStart.indexOf(CMD_START_MARKER) < 0) {
        result.timedOut = true;
        Serial.println(F("[UART] Timed out waiting for CMD_START marker"));
        return result;
    }

    // Collect everything until the END marker
    String afterStart = collectUntilMarker(CMD_END_MARKER, timeoutMs);
    int endIdx = afterStart.indexOf(CMD_END_MARKER);
    if (endIdx < 0) {
        result.timedOut = true;
        result.output   = afterStart;
        Serial.println(F("[UART] Timed out waiting for CMD_END marker"));
        return result;
    }

    // Strip the marker itself and any leading/trailing whitespace
    result.output = afterStart.substring(0, endIdx);
    result.output.trim();
    result.success = true;

    Serial.printf("[UART] Command output (%u bytes): %.80s%s\n",
                  result.output.length(),
                  result.output.c_str(),
                  result.output.length() > 80 ? "..." : "");
    return result;
}

// ---------------------------------------------------------------------------
// WiFi credential extraction
// ---------------------------------------------------------------------------

/**
 * Table of probe commands to try.  Each entry contains:
 *  - cmd       : shell command(s) that print the SSID and PSK
 *  - ssidToken : keyword used to identify the SSID line
 *  - pskToken  : keyword used to identify the PSK line
 *
 * The parser looks for  KEY=value  or  KEY:value  patterns (case-insensitive).
 * Add or reorder entries to match your FRITZ!Box firmware version.
 */
static const struct {
    const char* cmd;
    const char* ssidToken;
    const char* pskToken;
} CRED_PROBES[] = {
    // --- Method 1: AVM nvram tool (most common on FRITZ!Box) ---
    // Outputs:  SSID=MyNetwork   PSK=MyPassword
    {
        "echo SSID=$(nv get wlan_ssid 2>/dev/null); "
        "echo PSK=$(nv get wlan_psk 2>/dev/null)",
        "SSID", "PSK"
    },
    // --- Method 2: wlancfg CLI (present on some models) ---
    {
        "echo SSID=$(wlancfg WLAN_NETWORK_NAME 2>/dev/null); "
        "echo PSK=$(wlancfg WLAN_NETWORK_KEY 2>/dev/null)",
        "SSID", "PSK"
    },
    // --- Method 3: grep from flash-resident settings files ---
    {
        "grep -m1 -iE '^(ssid|WLAN_NETWORK_NAME)' "
            "/var/flash/wlan_settings 2>/dev/null; "
        "grep -m1 -iE '^(psk|WLAN_NETWORK_KEY)' "
            "/var/flash/wlan_settings 2>/dev/null",
        "SSID", "PSK"
    },
    // --- Method 4: alternative flash path used by some firmware versions ---
    {
        "grep -m1 -iE '^(ssid|WLAN_NETWORK_NAME)' "
            "/var/tmp/wlan_settings 2>/dev/null; "
        "grep -m1 -iE '^(psk|WLAN_NETWORK_KEY)' "
            "/var/tmp/wlan_settings 2>/dev/null",
        "SSID", "PSK"
    },
    // --- Sentinel ---
    { nullptr, nullptr, nullptr }
};

WifiCredentials UartHandler::extractWifiCredentials() {
    WifiCredentials creds;

    Serial.println(F("[UART] Probing FRITZ!Box for WiFi credentials..."));

    for (int i = 0; CRED_PROBES[i].cmd != nullptr; ++i) {
        Serial.printf("[UART]  Probe %d: %s\n", i, CRED_PROBES[i].cmd);

        CommandResult res = sendCommand(CRED_PROBES[i].cmd,
                                        WIFI_CRED_TIMEOUT_MS);
        if (!res.success || res.output.isEmpty()) {
            Serial.println(F("[UART]  Probe returned no output, skipping"));
            continue;
        }

        // Parse the output line by line
        bool   foundSsid = false, foundPsk = false;
        String raw = res.output + '\n';

        for (int pos = 0; pos < (int)raw.length(); ) {
            int nl = raw.indexOf('\n', pos);
            if (nl < 0) nl = raw.length();

            String line = raw.substring(pos, nl);
            line.trim();
            pos = nl + 1;

            if (line.isEmpty()) continue;

            if (!foundSsid) {
                foundSsid = extractValue(line, CRED_PROBES[i].ssidToken,
                                         creds.ssid);
            }
            if (!foundPsk) {
                foundPsk = extractValue(line, CRED_PROBES[i].pskToken,
                                        creds.password);
            }
        }

        if (foundSsid && foundPsk) {
            creds.valid = true;
            Serial.printf("[UART] Credentials extracted – SSID: %s\n",
                          creds.ssid.c_str());
            return creds;
        }
        Serial.println(F("[UART]  Probe did not yield complete credentials"));
    }

    Serial.println(F("[UART] Could not extract WiFi credentials from FRITZ!Box"));
    return creds;   // valid == false
}
