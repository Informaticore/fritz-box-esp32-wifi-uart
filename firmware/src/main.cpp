/**
 * @file main.cpp
 * @brief FRITZ!Box ESP32 UART Bridge – main entry point.
 *
 * Boot sequence
 * -------------
 *  1. Initialise the UART driver on Serial2 (GPIO 16/17) to talk to the
 *     FRITZ!Box serial console.
 *  2. Send probe commands to the FRITZ!Box to extract the WiFi SSID/PSK.
 *  3. Connect the ESP32 to the FRITZ!Box WiFi network.
 *     – Falls back to NVS-stored credentials from a previous boot.
 *     – Opens its own Access Point (SSID: FritzBridge-Setup) as a last
 *       resort so the web interface is always reachable.
 *  4. Start the HTTP web server (port 80).
 *
 * During normal operation loop() feeds new UART bytes into the log
 * ring-buffer and lets the web server handle incoming HTTP requests.
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

    // Give the FRITZ!Box time to finish booting / printing boot messages
    // before we start querying it.  Adjust if necessary.
    Serial.println(F("[Main] Waiting 3 s for FRITZ!Box to settle..."));
    delay(3000);

    // --- Step 2: Extract WiFi credentials via UART ---
    WifiCredentials creds = g_uart.extractWifiCredentials();

    // --- Step 3: Connect to WiFi ---
    g_wifi.connect(creds);

    // --- Step 4: Start web server ---
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
