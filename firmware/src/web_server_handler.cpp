/**
 * @file web_server_handler.cpp
 * @brief HTTP web interface implementation.
 *
 * The single-page HTML is stored in PROGMEM to keep it out of the data
 * SRAM.  JavaScript inside the page polls /api/log every 2 s and renders
 * the results so the user always sees live output.
 */
#include "web_server_handler.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Embedded HTML page (stored in flash via PROGMEM)
// ---------------------------------------------------------------------------

static const char HTML_PAGE[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>FRITZ!Box UART Bridge</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:'Courier New',monospace;background:#1a1a2e;color:#e0e0e0;
       display:flex;flex-direction:column;height:100vh;padding:12px;gap:10px}
  h1{font-size:1.2rem;color:#00d4ff;text-align:center;letter-spacing:2px}
  #status{font-size:.75rem;color:#888;text-align:center}
  #log-panel{flex:1;overflow-y:auto;background:#0f0f1a;border:1px solid #333;
             border-radius:6px;padding:8px;font-size:.78rem;line-height:1.5}
  .log-line{white-space:pre-wrap;word-break:break-all}
  .log-line:nth-child(even){color:#b0c4de}
  #cmd-form{display:flex;gap:8px}
  #cmd-input{flex:1;background:#0f0f1a;color:#e0e0e0;border:1px solid #444;
             border-radius:4px;padding:6px 10px;font-family:inherit;font-size:.9rem}
  #cmd-input:focus{outline:none;border-color:#00d4ff}
  button{background:#00d4ff;color:#0f0f1a;border:none;border-radius:4px;
         padding:6px 16px;font-weight:bold;cursor:pointer;font-size:.9rem}
  button:hover{background:#00b8d9}
  button:disabled{background:#555;cursor:not-allowed}
  #response-panel{background:#0f0f1a;border:1px solid #444;border-radius:6px;
                  padding:8px;font-size:.78rem;min-height:60px;max-height:180px;
                  overflow-y:auto;white-space:pre-wrap;word-break:break-all}
  .ok{color:#4caf50} .err{color:#f44336} .info{color:#00d4ff}
</style>
</head>
<body>
<h1>&#x1F4E1; FRITZ!Box UART Bridge</h1>
<div id="status">Connecting…</div>
<div id="log-panel"><span class="info">Waiting for log data…</span></div>
<form id="cmd-form" onsubmit="sendCmd(event)">
  <input id="cmd-input" type="text" placeholder="Enter FRITZ!Box shell command…" autocomplete="off"/>
  <button id="send-btn" type="submit">Send</button>
</form>
<div id="response-panel"><span class="info">Command output will appear here.</span></div>

<script>
const logPanel  = document.getElementById('log-panel');
const respPanel = document.getElementById('response-panel');
const statusEl  = document.getElementById('status');
const cmdInput  = document.getElementById('cmd-input');
const sendBtn   = document.getElementById('send-btn');

let autoScroll = true;

// Auto-scroll detection: if the user scrolls up, pause auto-scroll
logPanel.addEventListener('scroll', () => {
  autoScroll = (logPanel.scrollTop + logPanel.clientHeight) >= (logPanel.scrollHeight - 20);
});

// Poll the log every 2 seconds
async function fetchLog() {
  try {
    const r = await fetch('/api/log');
    if (!r.ok) return;
    const data = await r.json();
    logPanel.innerHTML = '';
    (data.lines || []).forEach(line => {
      const el = document.createElement('div');
      el.className = 'log-line';
      el.textContent = line;
      logPanel.appendChild(el);
    });
    if (autoScroll) logPanel.scrollTop = logPanel.scrollHeight;
  } catch(e) { /* network blip */ }
}

// Poll the status bar
async function fetchStatus() {
  try {
    const r = await fetch('/api/status');
    if (!r.ok) return;
    const d = await r.json();
    statusEl.textContent =
      (d.connected ? '✅ Connected' : '📡 AP mode') +
      '  |  SSID: ' + d.ssid +
      '  |  IP: '   + d.ip +
      '  |  Log lines: ' + d.logLines;
  } catch(e) {}
}

// Send a command
async function sendCmd(evt) {
  evt.preventDefault();
  const cmd = cmdInput.value.trim();
  if (!cmd) return;

  sendBtn.disabled = true;
  respPanel.innerHTML = '<span class="info">Sending command, waiting for response…</span>';

  try {
    const r = await fetch('/api/cmd', {
      method: 'POST',
      headers: {'Content-Type':'application/x-www-form-urlencoded'},
      body: 'cmd=' + encodeURIComponent(cmd)
    });
    const d = await r.json();
    if (d.timedOut) {
      respPanel.innerHTML = '<span class="err">⚠ Timed out – no CMD_END marker received.</span>\n'
                          + escHtml(d.output || '');
    } else if (d.success) {
      respPanel.innerHTML = '<span class="ok">✓ Success</span>\n' + escHtml(d.output);
    } else {
      respPanel.innerHTML = '<span class="err">✗ Error</span>\n' + escHtml(d.output);
    }
  } catch(e) {
    respPanel.innerHTML = '<span class="err">Network error: ' + e.message + '</span>';
  } finally {
    sendBtn.disabled = false;
    cmdInput.select();
  }
}

function escHtml(s) {
  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

// Kick off polling
fetchLog();
fetchStatus();
setInterval(fetchLog,    2000);
setInterval(fetchStatus, 5000);
</script>
</body>
</html>
)rawhtml";

// ---------------------------------------------------------------------------
// JSON helpers (no external library needed for these simple responses)
// ---------------------------------------------------------------------------

/** Escape a string for safe embedding in a JSON value. */
static String jsonEscape(const String& s) {
    String out;
    out.reserve(s.length() + 16);
    for (int i = 0; i < (int)s.length(); ++i) {
        char c = s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((uint8_t)c < 0x20) {
                    // Control character: emit \uXXXX
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (uint8_t)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

/** Build a JSON object string for the /api/status response. */
static String buildStatusJson(bool connected,
                               const String& ssid,
                               const String& ip,
                               size_t        logLines) {
    String j = "{\"connected\":";
    j += connected ? "true" : "false";
    j += ",\"ssid\":\"";
    j += jsonEscape(ssid);
    j += "\",\"ip\":\"";
    j += jsonEscape(ip);
    j += "\",\"logLines\":";
    j += (unsigned long)logLines;
    j += "}";
    return j;
}

// ---------------------------------------------------------------------------
// WebServerHandler
// ---------------------------------------------------------------------------

WebServerHandler::WebServerHandler(UartHandler& uart, WiFiManager& wifiMgr)
    : _server(WEB_SERVER_PORT)
    , _uart(uart)
    , _wifiMgr(wifiMgr)
{}

void WebServerHandler::begin() {
    _server.on("/",          [this]() { handleRoot();       });
    _server.on("/api/log",   [this]() { handleApiLog();     });
    _server.on("/api/cmd",   HTTP_POST,
                             [this]() { handleApiCmd();     });
    _server.on("/api/status",[this]() { handleApiStatus();  });
    _server.onNotFound(      [this]() { handleNotFound();   });

    _server.begin();
    Serial.printf("[Web] HTTP server started on port %d\n", WEB_SERVER_PORT);
}

void WebServerHandler::loop() {
    _server.handleClient();
}

// ---------------------------------------------------------------------------
// Route handlers
// ---------------------------------------------------------------------------

void WebServerHandler::handleRoot() {
    // PROGMEM content: copy to a String so WebServer can send it
    _server.sendHeader("Cache-Control", "no-cache");
    _server.send_P(200, "text/html", HTML_PAGE);
}

void WebServerHandler::handleApiLog() {
    // Return all buffered log lines (up to LOG_BUFFER_LINES as configured)
    auto lines = _uart.getLogLines(LOG_BUFFER_LINES);

    // Build a JSON array: {"lines":["line1","line2",...]}
    String json = "{\"lines\":[";
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) json += ',';
        json += '"';
        json += jsonEscape(lines[i]);
        json += '"';
    }
    json += "]}";

    _server.sendHeader("Cache-Control", "no-cache");
    _server.send(200, "application/json", json);
}

void WebServerHandler::handleApiCmd() {
    if (!_server.hasArg("cmd") || _server.arg("cmd").isEmpty()) {
        _server.send(400, "application/json",
                     "{\"error\":\"Missing 'cmd' parameter\"}");
        return;
    }

    String command = _server.arg("cmd");
    Serial.printf("[Web] Command request: %s\n", command.c_str());

    CommandResult res = _uart.sendCommand(command);

    String json = "{\"success\":";
    json += res.success ? "true" : "false";
    json += ",\"timedOut\":";
    json += res.timedOut ? "true" : "false";
    json += ",\"output\":\"";
    json += jsonEscape(res.output);
    json += "\"}";

    _server.sendHeader("Cache-Control", "no-cache");
    _server.send(200, "application/json", json);
}

void WebServerHandler::handleApiStatus() {
    auto lines = _uart.getLogLines();
    String json = buildStatusJson(
        _wifiMgr.isConnected(),
        _wifiMgr.connectedSSID(),
        _wifiMgr.ipAddress(),
        lines.size()
    );
    _server.sendHeader("Cache-Control", "no-cache");
    _server.send(200, "application/json", json);
}

void WebServerHandler::handleNotFound() {
    _server.send(404, "text/plain", "Not found");
}
