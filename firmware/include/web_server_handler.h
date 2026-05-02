/**
 * @file web_server_handler.h
 * @brief HTTP web interface for the FRITZ!Box ESP32 UART Bridge.
 *
 * Serves a single-page application that displays the FRITZ!Box UART log
 * in real time and lets the user send shell commands.
 *
 * HTTP endpoints
 * --------------
 *   GET  /           – Main HTML/JS/CSS page (log viewer + command form)
 *   GET  /api/log    – Latest log lines as JSON array
 *   POST /api/cmd    – Send a command; body: "cmd=<command>"
 *                      Returns JSON: { "output": "...", "success": bool,
 *                                      "timedOut": bool }
 *   GET  /api/status – Connection info as JSON
 *   POST /api/wifi   – Save manual WiFi credentials and (re)connect;
 *                      body: "ssid=<ssid>&password=<password>"
 *                      Returns JSON: { "connected": bool, "ssid": "..." }
 *   POST /api/uart-check – Re-run UART connection probe and return result;
 *                          Returns JSON: { "online": bool, "summary": "...",
 *                                         "rawOutput": "..." }
 */
#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "config.h"
#include "uart_handler.h"
#include "wifi_manager.h"

/**
 * @class WebServerHandler
 *
 * Owns a WebServer instance (port 80 by default).  The HTML page embedded
 * in PROGMEM uses the Fetch API to poll /api/log every two seconds and
 * updates the log panel without a full page reload.
 */
class WebServerHandler {
public:
    /**
     * @param uart     Reference to the active UartHandler (used for log
     *                 access and command dispatch).
     * @param wifiMgr  Reference to the active WiFiManager (used for status).
     */
    explicit WebServerHandler(UartHandler& uart, WiFiManager& wifiMgr);

    /** Bind HTTP handlers and start the server. Call once from setup(). */
    void begin();

    /** Process pending HTTP requests. Call on every loop() iteration. */
    void loop();

private:
    WebServer   _server;
    UartHandler& _uart;
    WiFiManager& _wifiMgr;

    void handleRoot();
    void handleApiLog();
    void handleApiCmd();
    void handleApiStatus();
    void handleApiWifiSet();
    void handleApiUartCheck();
    void handleNotFound();
};
