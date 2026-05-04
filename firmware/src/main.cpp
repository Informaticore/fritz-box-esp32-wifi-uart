/**
 * @file main.cpp
 * @brief FRITZ!Box ESP32 UART Bridge – main entry point.
 *
 * Two build modes are available (controlled by UART_BRIDGE_MODE in config.h):
 *
 * ── UART_BRIDGE_MODE = 1  (default) ────────────────────────────────────────
 *   Transparent UART-to-USB bridge.  Every byte received on Serial2 (from
 *   the FRITZ!Box) is forwarded to the USB serial port and vice versa.
 *   No WiFi, no web server.  Use this mode to verify UART wiring and
 *   inspect the raw FRITZ!Box console with any terminal at 115200 baud.
 *
 * ── UART_BRIDGE_MODE = 0 ────────────────────────────────────────────────────
 *   Full firmware: WiFi connection (manual credentials via web UI or
 *   wifi.txt on LittleFS), HTTP web interface with live log, command
 *   execution, and UART connection check.
 *
 *   Boot sequence
 *   -------------
 *    1. Initialise the UART driver on Serial2 (GPIO 16/17).
 *    2. Connect to WiFi using stored NVS credentials, or fall back to AP.
 *    3. Start the HTTP web server (port 80).
 *
 * Hardware connections
 * --------------------
 *   FRITZ!Box UART TX  →  ESP32 GPIO 16  (Serial2 RX)
 *   FRITZ!Box UART RX  →  ESP32 GPIO 17  (Serial2 TX)
 *   FRITZ!Box GND      →  ESP32 GND
 *   FRITZ!Box USB 5 V  →  ESP32 USB (power)
 *
 * See config.h to change pin assignments, baud rate or other parameters.
 */

#include <Arduino.h>
#include "config.h"

// ===========================================================================
// UART Bridge mode – transparent passthrough, no WiFi/web server
// ===========================================================================
#if UART_BRIDGE_MODE

void setup() {
    // USB serial (to PC terminal)
    Serial.begin(FRITZ_UART_BAUD);
    delay(200);

    // FRITZ!Box serial console
    Serial2.begin(FRITZ_UART_BAUD, FRITZ_UART_CONFIG,
                  FRITZ_UART_RX_PIN, FRITZ_UART_TX_PIN);

    // Brief banner – flushed before bridging starts
    Serial.println(F("\r\n--- FRITZ!Box UART Bridge (passthrough mode) ---"));
    Serial.printf( "    Baud: %d | RX pin: %d | TX pin: %d\r\n",
                   FRITZ_UART_BAUD, FRITZ_UART_RX_PIN, FRITZ_UART_TX_PIN);
    Serial.println(F("    Type into this terminal to send to the FRITZ!Box."));
    Serial.println(F("-----------------------------------------------\r\n"));
}

void loop() {
    // FRITZ!Box → PC (forward every byte as-is)
    while (Serial2.available()) {
        Serial.write(Serial2.read());
    }
    // PC → FRITZ!Box (forward every byte as-is)
    while (Serial.available()) {
        Serial2.write(Serial.read());
    }
}

// ===========================================================================
// Full firmware – WiFi + web interface
// ===========================================================================
#else  // !UART_BRIDGE_MODE

#include "uart_handler.h"
#include "wifi_manager.h"
#include "web_server_handler.h"

// ---------------------------------------------------------------------------
// Module instances (constructed once, reused throughout)
// ---------------------------------------------------------------------------

static UartHandler        g_uart;
static WiFiManager        g_wifi;
static WebServerHandler*  g_web = nullptr;

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------

void setup() {
    // USB/debug serial – separate from the FRITZ!Box UART
    Serial.begin(115200);
    delay(500);
    Serial.println(F("\n========================================"));
    Serial.println(F(" FRITZ!Box ESP32 UART Bridge"));
    Serial.println(F("========================================"));

    // --- Step 1: Start FRITZ!Box UART ---
    g_uart.begin();

    // --- Step 2: Connect to WiFi (manual credentials via NVS / wifi.txt) ---
    g_wifi.connect();

    // --- Step 3: Start web server ---
    g_web = new WebServerHandler(g_uart, g_wifi);
    g_web->begin();

    Serial.printf("\n[Main] Ready!  Open  http://%s  in your browser.\n",
                  g_wifi.ipAddress().c_str());
    Serial.println(F("[Main] Entering main loop..."));
}

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------

void loop() {
    // Pump UART bytes into the log ring-buffer
    g_uart.loop();

    // Handle any pending HTTP requests
    if (g_web) {
        g_web->loop();
    }
}

#endif  // UART_BRIDGE_MODE
