/**
 * @file uart_handler.h
 * @brief FRITZ!Box UART driver with marker-based command/response protocol.
 *
 * The FRITZ!Box serial console (UART) is a Linux shell that continuously
 * prints system-log messages.  When we send a command, log messages may be
 * interleaved with the command output.  To deal with this cleanly we wrap
 * every command with unique marker strings:
 *
 *   echo "~~FBCMD_START~~"; <command>; echo "~~FBCMD_END~~"
 *
 * Everything received outside the markers goes to the log ring-buffer.
 * Everything between the markers is returned as the command response.
 */
#pragma once

#include <Arduino.h>
#include <deque>
#include <vector>
#include "config.h"

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

/** WiFi credentials extracted from the FRITZ!Box via UART. */
struct WifiCredentials {
    String ssid;
    String password;
    bool   valid = false;   ///< true only when both fields are non-empty
};

/**
 * Result of a command sent to the FRITZ!Box.
 * @see UartHandler::sendCommand()
 */
struct CommandResult {
    String output;      ///< Text captured between the CMD markers
    bool   success;     ///< true when CMD_END_MARKER was received
    bool   timedOut;    ///< true when we gave up waiting for a marker
};

/**
 * Result of a UART connectivity check.
 * @see UartHandler::checkConnection()
 */
struct UartCheckResult {
    bool   online;      ///< true when UART is alive and the shell responded
    String summary;     ///< Short human-readable status (e.g. "OK – 12 entries")
    String rawOutput;   ///< Raw output of the probe command (trimmed)
};

// ---------------------------------------------------------------------------
// UartHandler
// ---------------------------------------------------------------------------

/**
 * @class UartHandler
 *
 * Owns Serial2 which is wired to the FRITZ!Box serial console.
 *
 * Responsibilities:
 *  - Continuously accumulate incoming characters into a log ring-buffer.
 *  - Send shell commands using the marker protocol and return their output.
 *  - Probe the FRITZ!Box for the WiFi SSID and PSK at startup.
 */
class UartHandler {
public:
    UartHandler() = default;

    /**
     * Initialise Serial2 using the pins defined in config.h.
     * Call once from setup().
     */
    void begin();

    /**
     * Pump incoming UART bytes into the log ring-buffer.
     * Must be called on every iteration of loop().
     */
    void loop();

    /**
     * Send a shell command to the FRITZ!Box and collect its output.
     *
     * Internally the command is wrapped with CMD_START_MARKER /
     * CMD_END_MARKER so that interleaved syslog lines do not corrupt the
     * result:
     *
     *   echo "~~FBCMD_START~~"; <command>; echo "~~FBCMD_END~~"
     *
     * @param command   Shell command string (will be sent verbatim inside
     *                  the wrapper; do not add a trailing newline).
     * @param timeoutMs Maximum wait time for the CMD_END marker (ms).
     * @return          CommandResult with the captured output.
     */
    CommandResult sendCommand(const String& command,
                              uint32_t timeoutMs = CMD_TIMEOUT_MS);

    /**
     * Perform a UART connectivity check by running `ls -la /` on the
     * FRITZ!Box.  Verifies the UART wiring is correct and the FRITZ!Box
     * shell is responding with plausible output (i.e. a Linux directory
     * listing containing expected entries such as "bin", "proc", or "tmp").
     *
     * The result is cached internally; call lastCheckResult() to retrieve it
     * without re-running the command.
     *
     * @return UartCheckResult indicating whether the connection is healthy.
     */
    UartCheckResult checkConnection();

    /** Return the result of the most recent checkConnection() call.
     *  Returns an "unchecked" entry (online=false) before the first call.
     */
    const UartCheckResult& lastCheckResult() const;

    /**
     * Attempt to read the FRITZ!Box WLAN SSID and PSK via the console.
     * Tries several known probe commands in order and returns on the first
     * successful match.  Returns a WifiCredentials with valid=false when
     * all probes fail.
     */
    WifiCredentials extractWifiCredentials();

    /**
     * Return up to maxLines recent log lines (oldest first, newest last).
     */
    std::vector<String> getLogLines(size_t maxLines = 100) const;

    /**
     * Return the log buffer as a single newline-separated string.
     */
    String getLogAsString(size_t maxLines = 100) const;

private:
    std::deque<String> _logBuffer;   ///< Ring-buffer of received log lines
    String             _lineBuf;     ///< Accumulator for the current line
    UartCheckResult    _lastCheck;   ///< Cached result of the last checkConnection()

    /** Append a completed line to the ring-buffer (evicts oldest if full). */
    void addLogLine(const String& line);

    /**
     * Read from Serial2 until @p endMarker appears in the stream or the
     * timeout expires.  All received bytes are appended to the log buffer
     * as usual and also returned as a raw string for further processing.
     *
     * @return Everything received up to and including the end marker (or
     *         everything received before the timeout when it fires).
     */
    String collectUntilMarker(const String& endMarker, uint32_t timeoutMs);
};
