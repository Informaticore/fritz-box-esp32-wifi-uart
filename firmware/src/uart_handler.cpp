/**
 * @file uart_handler.cpp
 * @brief FRITZ!Box UART driver implementation.
 *
 * See uart_handler.h for a full description of the design.
 *
 * This file is only compiled when UART_BRIDGE_MODE = 0.
 */
#include "uart_handler.h"
#include "config.h"

// Convenience alias so the code compiles for any board using Serial2
#define FRITZ_UART_PORT Serial2

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

#if FRITZ_LOG_ECHO_USB
    // Mirror FRITZ!Box UART output to the USB debug serial so it is visible
    // in a serial monitor without needing to open the web interface.
    Serial.print(F("[FRITZ] "));
    Serial.println(line);
#endif
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

    // Discard any stale bytes in the hardware RX buffer.
    // We read byte-by-byte here because Arduino's HardwareSerial does not
    // expose a direct "flush RX" method; this loop is bounded by whatever
    // the UART FIFO currently holds and completes in microseconds.
    while (FRITZ_UART_PORT.available()) {
        (void)FRITZ_UART_PORT.read();
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
// UART connectivity check
// ---------------------------------------------------------------------------

/**
 * Heuristically decide whether a string looks like the output of `ls -la /`
 * on a Linux system.  We accept the output if it:
 *   a) starts with "total" (standard ls -la header), OR
 *   b) contains at least one line with Unix permission bits (e.g. "drwxr-xr-x"),
 *      OR
 *   c) contains one of the well-known FRITZ!Box root directories.
 */
static bool looksLikeDirectoryListing(const String& output) {
    if (output.indexOf(F("total "))  >= 0) return true;
    if (output.indexOf(F("drwx"))    >= 0) return true;
    if (output.indexOf(F("lrwx"))    >= 0) return true;
    if (output.indexOf(F("-rwx"))    >= 0) return true;
    // Check for well-known root directory names present on every FRITZ!Box.
    // We don't require a leading space so entries at line-start are matched too.
    if (output.indexOf(F("bin"))     >= 0) return true;
    if (output.indexOf(F("proc"))    >= 0) return true;
    if (output.indexOf(F("tmp"))     >= 0) return true;
    if (output.indexOf(F("usr"))     >= 0) return true;
    return false;
}

UartCheckResult UartHandler::checkConnection() {
    Serial.println(F("[UART] Checking FRITZ!Box serial connection..."));

    // Use a shorter timeout for the connectivity probe (5 s is enough)
    CommandResult res = sendCommand(F("ls -la /"), 5000);

    _lastCheck.rawOutput = res.output;

    if (!res.success) {
        _lastCheck.online  = false;
        _lastCheck.summary = res.timedOut
            ? F("OFFLINE – no response (timed out)")
            : F("OFFLINE – command failed");
        Serial.printf("[UART] Connection check FAILED: %s\n",
                      _lastCheck.summary.c_str());
        return _lastCheck;
    }

    if (looksLikeDirectoryListing(res.output)) {
        // Count non-empty lines in the output as a rough entry count
        int lines = 0;
        for (int i = 0; i < (int)res.output.length(); ++i) {
            if (res.output[i] == '\n') ++lines;
        }
        _lastCheck.online  = true;
        _lastCheck.summary = String(F("OK – ls -la / returned ")) + lines
                           + F(" lines");
        Serial.printf("[UART] Connection check OK: %s\n",
                      _lastCheck.summary.c_str());
    } else {
        // Got a response but it doesn't look like a directory listing –
        // the UART is alive but something unexpected was returned.
        _lastCheck.online  = false;
        _lastCheck.summary = F("UNCERTAIN – response does not look like a "
                               "directory listing");
        Serial.printf("[UART] Connection check UNCERTAIN. Output: %.80s\n",
                      res.output.c_str());
    }
    return _lastCheck;
}

const UartCheckResult& UartHandler::lastCheckResult() const {
    return _lastCheck;
}

