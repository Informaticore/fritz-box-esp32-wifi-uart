/**
 * @file wifi_manager.h
 * @brief WiFi connection manager with NVS credential caching and AP fallback.
 *
 * Connection strategy (in order):
 *  1. Use credentials extracted from the FRITZ!Box via UART.
 *  2. If that fails, try credentials previously stored in NVS (flash).
 *  3. If that also fails, open a fallback Access Point so the web interface
 *     can still be reached and UART commands can still be issued.
 */
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "uart_handler.h"

/**
 * @class WiFiManager
 *
 * Manages the ESP32 WiFi connection.  Credentials are persisted in NVS so
 * that the device reconnects automatically after a power cycle even if the
 * FRITZ!Box UART extraction happens to fail temporarily.
 */
class WiFiManager {
public:
    WiFiManager() = default;

    /**
     * Attempt to connect to the FRITZ!Box WiFi network.
     *
     * @param creds  Credentials obtained by UartHandler::extractWifiCredentials().
     *               If creds.valid is false the method skips straight to the
     *               NVS-stored fallback.
     * @return true  – connected in Station mode.
     *         false – running in AP mode (fallback).
     */
    bool connect(const WifiCredentials& creds);

    /** Returns true when the ESP32 is connected as a Station. */
    bool isConnected() const;

    /** IP address: STA address when connected, AP address in AP mode. */
    String ipAddress() const;

    /** SSID of the network the ESP32 is associated with (or AP SSID). */
    String connectedSSID() const;

    /**
     * Save manually-supplied credentials to NVS and attempt to connect.
     *
     * Can be called at any time via the web API (e.g. when the device is in
     * AP mode and the user fills in the WiFi settings form).  On success the
     * device switches to Station mode; on failure it stays in its current
     * mode.
     *
     * @param ssid      Network name (must be non-empty).
     * @param password  Network password (may be empty for open networks).
     * @return true when the connection succeeded, false otherwise.
     */
    bool setManualCredentials(const String& ssid, const String& password);

private:
    bool _apMode = false;

    /**
     * Try to associate with the given SSID/password within timeoutMs.
     * @return true on success, false on timeout.
     */
    bool tryConnect(const String& ssid, const String& password,
                    uint32_t timeoutMs = WIFI_CONNECT_TIMEOUT_MS);

    /** Load previously saved credentials from NVS. Returns false if none. */
    bool loadStoredCredentials(String& ssid, String& password);

    /** Persist credentials to NVS for future boots. */
    void saveCredentials(const String& ssid, const String& password);

    /** Open a softAP so the user can still reach the web interface. */
    void startAPMode();
};
