# FRITZ!Box ESP32 UART Bridge

[![Build Firmware](https://github.com/Informaticore/fritz-box-esp32-wifi-uart/actions/workflows/build.yml/badge.svg)](https://github.com/Informaticore/fritz-box-esp32-wifi-uart/actions/workflows/build.yml)

An ESP32 firmware that bridges the **FRITZ!Box serial (UART) console** to a
**web browser** over WiFi.

- Reads the FRITZ!Box system log in real time via UART.
- Sends interactive shell commands to the FRITZ!Box and shows the responses.
- **Performs a UART connection check at boot** – runs `ls -la /` and verifies
  a plausible directory listing is returned; result shown in the web UI.
- **Automatically extracts the WiFi SSID and password from the FRITZ!Box** – no
  manual WiFi configuration needed on first boot.
- **Manual WiFi credentials** – a settings panel in the web UI lets you enter
  credentials at any time; they are stored in NVS and used on every subsequent
  boot.
- Falls back to AP mode if credential extraction fails.
- Firmware binary is built by the CI pipeline and can be downloaded and flashed
  without any toolchain installed locally.

---

## Table of Contents

1. [Hardware](#hardware)
2. [How it works](#how-it-works)
3. [Getting a firmware binary (no build needed)](#getting-a-firmware-binary)
4. [Flashing](#flashing)
5. [Building locally](#building-locally)
6. [First boot](#first-boot)
7. [Web interface](#web-interface)
8. [Command / response protocol](#command--response-protocol)
9. [WiFi credential extraction](#wifi-credential-extraction)
10. [Fallback AP mode](#fallback-ap-mode)
11. [Project structure](#project-structure)
12. [Configuration](#configuration)
13. [Tested hardware](#tested-hardware)

---

## Hardware

### Parts

| Item | Notes |
|------|-------|
| ESP32 dev board | Any ESP32 board with at least 2 hardware UARTs |
| FRITZ!Box with UART header | See AVM developer documentation for your model |
| 3 × jumper wires | |
| USB cable | Powers the ESP32 from the FRITZ!Box USB port |

### Wiring

```
FRITZ!Box UART header         ESP32
─────────────────────         ─────
TX   ──────────────────────▶  GPIO 16  (Serial2 RX)
RX   ◀──────────────────────  GPIO 17  (Serial2 TX)
GND  ──────────────────────── GND
```

> **Voltage levels:** The FRITZ!Box UART is 3.3 V logic.  Most ESP32 dev boards
> are also 3.3 V – verify this for your specific model before connecting.  Do
> **not** connect a 5 V UART directly to an ESP32 without a level shifter.

Power the ESP32 via the USB port of the FRITZ!Box.

---

## How it works

```
FRITZ!Box
  │
  │  3.3 V UART (115200 8N1)
  │
ESP32 GPIO 16/17 (Serial2)
  │
  ├─ Log ring-buffer (last 200 lines)
  │
  ├─ Marker-based command protocol
  │     echo "~~FBCMD_START~~"; <cmd>; echo "~~FBCMD_END~~"
  │
  └─ WiFi (STA mode, credentials read from FRITZ!Box)
       │
       └─ HTTP web server :80
            GET  /                – HTML single-page app
            GET  /api/log         – JSON log lines
            POST /api/cmd         – Send command, get response
            GET  /api/status      – Connection + UART check info
            POST /api/wifi        – Save manual WiFi credentials & reconnect
            POST /api/uart-check  – Re-run UART connectivity check
```

### Language & framework

The firmware is written in **C++** using the **Arduino framework** and built
with **PlatformIO**.  This combination provides:

- A rich, well-documented ecosystem for ESP32 peripherals.
- `WebServer`, `WiFi`, and `Preferences` (NVS) libraries out of the box.
- Straightforward CI integration via `pio run`.

---

## Getting a firmware binary

Every push to this repository triggers a GitHub Actions build.  The compiled
binary is uploaded as a workflow artifact.

1. Go to **Actions** → latest successful **Build Firmware** run.
2. Click **esp32-firmware** under *Artifacts*.
3. Extract the downloaded `.zip` – you will find:
   - `firmware.bin` – the application (flash at `0x10000`)
   - `bootloader.bin` – the bootloader (flash at `0x1000`)
   - `partitions.bin` – the partition table (flash at `0x8000`)

---

## Flashing

### Option A – esptool.py (recommended)

```bash
pip install esptool

# Full flash (recommended for first-time flashing)
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  write_flash \
  0x1000  bootloader.bin \
  0x8000  partitions.bin \
  0x10000 firmware.bin

# Application only (if bootloader is already flashed)
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  write_flash -z 0x10000 firmware.bin
```

Replace `/dev/ttyUSB0` with the correct serial port for your system
(`COMx` on Windows, `/dev/cu.usbserialXXXX` on macOS).

### Option B – ESP32 Flash Download Tool (Windows GUI)

Use the [Espressif Flash Download Tool](https://www.espressif.com/en/support/download/other-tools)
and set the three addresses as above.

---

## Building locally

```bash
# Install PlatformIO (once)
pip install platformio

# Build
cd firmware
pio run

# Build and flash
pio run --target upload

# Open serial monitor (debug output)
pio device monitor
```

---

## First boot

1. Connect the hardware as shown in the [Wiring](#wiring) section.
2. Power on the ESP32.
3. Watch the debug output on the USB serial port (115200 baud):
   ```
   ========================================
    FRITZ!Box ESP32 UART Bridge
   ========================================
   [UART] Serial2 initialised for FRITZ!Box console
   [Main] Waiting 3 s for FRITZ!Box to settle...
   [UART] Checking FRITZ!Box serial connection...
   [UART] Connection check OK: OK – ls -la / returned 14 lines
   [Main] UART connection OK: OK – ls -la / returned 14 lines
   [UART] Probing FRITZ!Box for WiFi credentials...
   [UART]  Probe 0: echo SSID=$(nv get wlan_ssid ...
   [UART] Credentials extracted – SSID: MyFritzBox
   [WiFi] Connecting to "MyFritzBox" ...........
   [WiFi] Connected!  IP: 192.168.178.42
   [Web]  HTTP server started on port 80
   [Main] Ready!  Open  http://192.168.178.42  in your browser.
   ```
4. Open the IP address in a browser to see the live log and command interface.

---

## Web interface

The single-page app auto-refreshes the log every 2 seconds.

```
┌─────────────────────────────────────────────────────────────┐
│  📡 FRITZ!Box UART Bridge                                    │
│  ✅ STA | SSID: MyFritzBox | IP: 192.168.178.42 | 🔌 UART OK│
├─────────────────────────────────────────────────────────────┤
│  [log panel – scrolling, auto-updated every 2 s]            │
│  Dec  1 12:34:56 FritzBox kernel: some log message          │
│  Dec  1 12:34:57 FritzBox daemon[123]: another message      │
│  …                                                           │
├─────────────────────────────────────────────────────────────┤
│  [ command input box                          ] [ Send ]     │
├─────────────────────────────────────────────────────────────┤
│  ✓ Success                                                   │
│  <command output>                                            │
├─────────────────────────────────────────────────────────────┤
│  ⚙ Settings  ▼                                              │
│  ┌──────────────────────┐  ┌──────────────────────────────┐ │
│  │ UART Connection Check│  │ Manual WiFi Credentials      │ │
│  │ [🔍 Check UART]      │  │ SSID: [ __________________ ] │ │
│  │ 🟢 OK – 14 lines     │  │ Pass: [ __________________ ] │ │
│  └──────────────────────┘  │ [💾 Save & Connect]          │ │
│                             └──────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### UART Connection Check

The **⚙ Settings** panel contains a **Check UART** button that runs
`ls -la /` on the FRITZ!Box via the marker protocol and verifies the output
looks like a valid Linux root directory listing.  The same check runs
automatically at startup.  The result badge (`🔌 UART OK` / `⚠ UART ?`) is
shown in the status bar and updated live.

### Manual WiFi Credentials

The settings panel also exposes a WiFi credentials form.  Entering an SSID
and (optional) password and clicking **Save & Connect** will:

1. Persist the credentials in the ESP32's NVS flash.
2. Attempt to connect immediately.  The status bar updates to reflect the
   new connection state.
3. If the connection fails, the device returns to (or stays in) AP mode while
   keeping the credentials in NVS for the next boot.

---

## Command / response protocol

The FRITZ!Box UART carries the system log continuously.  When a command is
sent, log messages may be interleaved with the command output.  To reliably
extract the command response, every command is wrapped with unique markers:

```bash
echo "~~FBCMD_START~~"; <your command>; echo "~~FBCMD_END~~"
```

The ESP32 then:

1. Waits for `~~FBCMD_START~~` to appear in the UART stream.
2. Collects all bytes until `~~FBCMD_END~~` appears.
3. Returns everything captured between the two markers as the command output.
4. Feeds all bytes (including those captured during the command) into the log
   ring-buffer so the log remains complete.

The marker strings are unlikely to appear in normal FRITZ!Box log output.
You can change them in `firmware/include/config.h` if needed.

---

## WiFi credential extraction

On boot the ESP32 tries a series of known FRITZ!Box shell commands in order:

| # | Method | Command |
|---|--------|---------|
| 1 | AVM nvram tool | `nv get wlan_ssid` / `nv get wlan_psk` |
| 2 | wlancfg CLI | `wlancfg WLAN_NETWORK_NAME` / `wlancfg WLAN_NETWORK_KEY` |
| 3 | Flash settings file (path A) | `grep` from `/var/flash/wlan_settings` |
| 4 | Flash settings file (path B) | `grep` from `/var/tmp/wlan_settings` |

The first method that returns a non-empty SSID **and** PSK wins.  The
credentials are then stored in the ESP32's NVS (non-volatile storage) so that
the device reconnects automatically on subsequent boots even if the extraction
fails temporarily.

> **Model-specific note:** The exact commands available depend on the FRITZ!Box
> model and firmware version.  Add new probe entries to the `CRED_PROBES` table
> in `firmware/src/uart_handler.cpp` if your model requires different commands.

---

## Fallback AP mode

If all credential-extraction probes fail **and** no credentials are stored in
NVS, the ESP32 opens its own Access Point:

| Setting | Value |
|---------|-------|
| SSID | `FritzBridge-Setup` |
| Password | `fritzbridge` |
| IP | `192.168.4.1` |

Connect to this AP and open `http://192.168.4.1` to use the web interface.
You can still send commands to the FRITZ!Box from there (UART works
independently of WiFi).

---

## Project structure

```
fritz-box-esp32-wifi-uart/
├── firmware/
│   ├── platformio.ini            # PlatformIO build configuration
│   ├── include/
│   │   ├── config.h              # All tuneable constants
│   │   ├── uart_handler.h        # UART driver interface
│   │   ├── wifi_manager.h        # WiFi manager interface
│   │   └── web_server_handler.h  # Web server interface
│   └── src/
│       ├── main.cpp              # Entry point (setup / loop)
│       ├── uart_handler.cpp      # UART driver + credential extraction
│       ├── wifi_manager.cpp      # WiFi connection + NVS caching
│       └── web_server_handler.cpp# HTTP server + embedded HTML/JS
├── .github/
│   └── workflows/
│       └── build.yml             # CI: build + upload firmware artifact
├── .gitignore
├── LICENSE
└── README.md
```

---

## Configuration

All parameters are in `firmware/include/config.h`:

| Constant | Default | Description |
|----------|---------|-------------|
| `FRITZ_UART_RX_PIN` | 16 | ESP32 GPIO for Serial2 RX |
| `FRITZ_UART_TX_PIN` | 17 | ESP32 GPIO for Serial2 TX |
| `FRITZ_UART_BAUD` | 115200 | FRITZ!Box UART baud rate |
| `CMD_START_MARKER` | `~~FBCMD_START~~` | Begin-of-response marker |
| `CMD_END_MARKER` | `~~FBCMD_END~~` | End-of-response marker |
| `CMD_TIMEOUT_MS` | 10000 | Max wait for CMD_END (ms) |
| `LOG_BUFFER_LINES` | 200 | Log lines kept in RAM |
| `LOG_LINE_MAX_LEN` | 512 | Max chars per log line |
| `WEB_SERVER_PORT` | 80 | HTTP port |
| `WIFI_CRED_TIMEOUT_MS` | 30000 | Per-probe credential timeout (ms) |
| `FALLBACK_AP_SSID` | `FritzBridge-Setup` | AP mode SSID |
| `FALLBACK_AP_PASSWORD` | `fritzbridge` | AP mode password |
| `WIFI_CONNECT_TIMEOUT_MS` | 20000 | STA connect timeout (ms) |

---

## Tested hardware

| FRITZ!Box model | Firmware | Credential method |
|-----------------|----------|-------------------|
| *(contributions welcome)* | | |

Please open a PR or issue to add your device to the compatibility table.

---

## License

MIT – see [LICENSE](LICENSE).

