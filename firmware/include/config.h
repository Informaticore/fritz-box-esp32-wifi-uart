/**
 * @file config.h
 * @brief Central configuration for the FRITZ!Box ESP32 UART Bridge.
 *
 * All tuneable parameters are kept here so that the rest of the code
 * stays free of magic numbers.  Adjust the values to match your
 * hardware and FRITZ!Box firmware version.
 */
#pragma once

// ---------------------------------------------------------------------------
// UART Bridge / Debug mode
// ---------------------------------------------------------------------------
/**
 * When UART_BRIDGE_MODE is 1 the firmware acts as a **transparent
 * UART-to-USB bridge**.  Every byte arriving on Serial2 (from the
 * FRITZ!Box) is forwarded to the USB serial port (Serial) and vice versa.
 * No WiFi, no web server – just a clean wire-level passthrough so you can
 * inspect the raw FRITZ!Box console output in a terminal.
 *
 * This default can be overridden at build time via the PlatformIO
 * build_flags (see platformio.ini):
 *   -DUART_BRIDGE_MODE=0  →  full WiFi + web UI firmware
 *   -DUART_BRIDGE_MODE=1  →  transparent UART-USB bridge (default)
 */
#define UART_BRIDGE_MODE 1

// ---------------------------------------------------------------------------
// UART pins (FRITZ!Box serial console → ESP32 Serial2)
// ---------------------------------------------------------------------------
/**
 * Wire the FRITZ!Box UART like this:
 *   FRITZ!Box TX  →  ESP32 GPIO16 (RX2)
 *   FRITZ!Box RX  →  ESP32 GPIO17 (TX2)
 *   FRITZ!Box GND →  ESP32 GND
 *
 * The FRITZ!Box runs the console at 3.3 V logic, which matches the ESP32.
 * Verify this for your specific model before connecting!
 */
#define FRITZ_UART_RX_PIN   16
#define FRITZ_UART_TX_PIN   17
#define FRITZ_UART_BAUD     115200
#define FRITZ_UART_CONFIG   SERIAL_8N1

// ---------------------------------------------------------------------------
// Command / response marker protocol
// ---------------------------------------------------------------------------
/**
 * Every command sent to the FRITZ!Box is wrapped like this:
 *
 *   echo "~~FBCMD_START~~"; <your command>; echo "~~FBCMD_END~~"
 *
 * This lets us extract the command output even when the FRITZ!Box syslog
 * is writing to the same UART at the same time.  Characters that arrive
 * outside the markers are stored in the log ring-buffer.
 *
 * The strings must be unlikely to appear in normal log output.
 */
#define CMD_START_MARKER    "~~FBCMD_START~~"
#define CMD_END_MARKER      "~~FBCMD_END~~"

/** How long (ms) to wait for CMD_END_MARKER before giving up. */
#define CMD_TIMEOUT_MS      10000

// ---------------------------------------------------------------------------
// Log ring-buffer
// ---------------------------------------------------------------------------
/** Number of UART lines to keep in the ring-buffer (oldest are dropped). */
#define LOG_BUFFER_LINES    200

/** Maximum characters stored per log line (longer lines are truncated). */
#define LOG_LINE_MAX_LEN    512

// ---------------------------------------------------------------------------
// Web server
// ---------------------------------------------------------------------------
#define WEB_SERVER_PORT     80

/**
 * If no credentials are stored in NVS, the ESP32 opens its own access
 * point with these settings so that you can still reach the web interface.
 */
#define FALLBACK_AP_SSID        "FritzBridge-Setup"
#define FALLBACK_AP_PASSWORD    "fritzbridge"

/** How long (ms) to wait for a STA WiFi association before giving up. */
#define WIFI_CONNECT_TIMEOUT_MS 20000

// ---------------------------------------------------------------------------
// WiFi credential file (LittleFS)
// ---------------------------------------------------------------------------
/**
 * Path inside the LittleFS filesystem where manual WiFi credentials can be
 * stored.  From the repository root, upload the file with:
 *
 *   pio run --project-dir firmware --target uploadfs
 *
 * Or from inside the firmware/ directory:
 *
 *   pio run --target uploadfs
 *
 * File format (plain text, one entry per line, '#' starts a comment):
 *
 *   ssid=MyNetworkName
 *   password=MySecret
 *
 * If the file is present and contains a non-empty SSID the credentials are
 * tried at boot before falling back to NVS-stored or AP mode.
 * Delete the file (or clear its contents) to disable.
 */
#define WIFI_CREDS_FILE     "/wifi.txt"

// ---------------------------------------------------------------------------
// FRITZ!Box UART log mirroring
// ---------------------------------------------------------------------------
/**
 * When set to 1 every log line received from the FRITZ!Box on Serial2 is
 * also printed to the USB debug serial (Serial) with a "[FRITZ] " prefix.
 * Set to 0 to silence it (e.g. if the USB monitor output becomes too noisy).
 */
#define FRITZ_LOG_ECHO_USB  1
