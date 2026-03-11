#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

/* ================= WIFI CONFIG ================= */
const char* sta_ssid     = "Motridox";
const char* sta_password = "Bassim@8371";

const char* ap_ssid      = "Westo_ESP32";
const char* ap_password  = "12345678";

/* ================= SERVER ====================== */
WebServer server(80);

/* ================= LED / TRIGGER =============== */
#define LED_PIN          2         // ESP32 built-in LED (GPIO 2)
#define BLINK_INTERVAL   300UL    // ms between blink toggles while active
#define TRIGGER_DURATION 30000UL  // auto-reset after 30 s (matches app cooldown)

bool          triggerActive  = false;
bool          ledState       = false;
unsigned long triggerStartMs = 0;
unsigned long lastBlinkMs    = 0;

/* ================= SYSTEM DATA ================= */
int           wasteLevel            = 50;
bool          lastUpdatedFromMobile = false;
unsigned long lastUpdateTime        = 0;

/* ================= HTML DASHBOARD ============== */
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Westo Inventory Dashboard</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
:root{--primary:#2563eb;--bg:#f4f6f9;--card:#ffffff;--success:#22c55e;--danger:#ef4444;--muted:#6b7280;}
body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);}
header{background:var(--primary);color:white;padding:18px;font-size:20px;font-weight:600;}
.container{padding:16px;display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px;}
.card{background:var(--card);border-radius:14px;padding:16px;box-shadow:0 10px 25px rgba(0,0,0,.08);}
.card h3{margin:0 0 12px;font-size:16px;}
.stat{display:flex;justify-content:space-between;margin-bottom:8px;color:var(--muted);}
.badge{padding:4px 10px;border-radius:12px;font-size:13px;color:white;}
.online{background:var(--success);}
.offline{background:var(--danger);}
.ip-box{font-family:monospace;font-size:13px;background:#f1f5f9;border-radius:8px;padding:6px 10px;color:#1e40af;font-weight:600;}
.progress{height:14px;background:#e5e7eb;border-radius:10px;overflow:hidden;}
.progress-bar{height:100%;background:linear-gradient(90deg,#22c55e,#16a34a);width:0%;transition:.4s;}
input[type=range]{width:100%;margin-top:12px;}
button{width:100%;padding:12px;background:var(--primary);border:none;color:white;border-radius:10px;font-size:15px;margin-top:12px;}
.notice{margin-top:10px;padding:10px;border-radius:10px;background:#ecfeff;color:#0369a1;display:none;font-size:14px;}
</style>
</head>
<body>
<header>Westo • Smart Waste Dashboard</header>
<div class="container">

  <!-- Network card -->
  <div class="card">
    <h3>Network Status</h3>
    <div class="stat"><span>Wi-Fi</span><span id="wifi" class="badge offline">Checking</span></div>
    <div class="stat"><span>Connected Devices</span><span id="clients">0</span></div>
    <div class="stat" style="margin-top:10px;"><span>AP IP Address</span></div>
    <div class="ip-box" id="apIp">---.---.---.---</div>
    <div class="stat" style="margin-top:10px;"><span>STA IP Address</span></div>
    <div class="ip-box" id="staIp">Not connected</div>
  </div>

  <!-- Waste level card -->
  <div class="card">
    <h3>Waste Level</h3>
    <div class="stat"><span>Current Level</span><span id="levelText">--%</span></div>
    <div class="progress"><div class="progress-bar" id="progress"></div></div>
    <input type="range" min="0" max="100" id="slider">
    <button onclick="updateWaste()">Update Waste Level</button>
    <div class="notice" id="notice">🔔 Waste level updated from mobile device</div>
  </div>

  <!-- Compressor card -->
  <div class="card">
    <h3>Compressor</h3>
    <div class="stat"><span>Status</span><span id="triggerBadge" class="badge offline">Idle</span></div>
    <button onclick="triggerCompress()">Trigger Compressor</button>
  </div>

</div>
<script>
function loadStatus(){
  fetch('/status').then(r=>r.json()).then(d=>{
    document.getElementById('clients').innerText   = d.connectedClients;
    document.getElementById('levelText').innerText = d.wasteLevel + "%";
    document.getElementById('progress').style.width = d.wasteLevel + "%";
    document.getElementById('slider').value        = d.wasteLevel;
    document.getElementById('apIp').innerText      = d.apIp;
    document.getElementById('staIp').innerText     = d.staIp || "Not connected";

    let wifi = document.getElementById('wifi');
    if(d.isConnected){ wifi.innerText="Connected"; wifi.className="badge online"; }
    else             { wifi.innerText="Not Connected"; wifi.className="badge offline"; }

    if(d.mobileUpdate){
      document.getElementById('notice').style.display="block";
      setTimeout(()=>{ document.getElementById('notice').style.display="none"; }, 3000);
    }

    let tb = document.getElementById('triggerBadge');
    if(d.triggerActive){ tb.innerText="Active"; tb.className="badge online"; }
    else               { tb.innerText="Idle";   tb.className="badge offline"; }
  });
}
function updateWaste(){
  let val = document.getElementById('slider').value;
  fetch('/update?level=' + val);
}
function triggerCompress(){
  fetch('/compress', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify({trigger: 1})
  });
}
setInterval(loadStatus, 2000);
</script>
</body>
</html>
)rawliteral";

/* ================= HELPERS ===================== */
bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

/* ================= SERIAL BANNER =============== */
void printDashboardURLs() {
  Serial.println();
  Serial.println("╔══════════════════════════════════════════╗");
  Serial.println("║       WESTO DASHBOARD - OPEN IN BROWSER  ║");
  Serial.println("╠══════════════════════════════════════════╣");

  Serial.print("║  📶 AP  (direct): http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/                  ║");

  if (isWiFiConnected()) {
    Serial.print("║  🌐 STA (wifi):   http://");
    Serial.print(WiFi.localIP());
    Serial.println("/                  ║");
  } else {
    Serial.println("║  🌐 STA (wifi):   Not connected               ║");
  }

  Serial.println("╠══════════════════════════════════════════╣");
  Serial.println("║  API ENDPOINTS:                          ║");
  Serial.print("║  /status       -> http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/status       ║");
  Serial.print("║  /update       -> http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/update       ║");
  Serial.print("║  /compress     -> http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/compress     ║");
  Serial.print("║  /device/info  -> http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/device/info  ║");
  Serial.println("╚══════════════════════════════════════════╝");
  Serial.println();
}

/* ================= LED HELPERS ================= */
void activateTrigger() {
  triggerActive  = true;
  triggerStartMs = millis();
  lastBlinkMs    = millis();
  ledState       = true;
  digitalWrite(LED_PIN, HIGH);
  Serial.println("[TRIGGER] Compressor activated — LED blinking");
}

void deactivateTrigger() {
  triggerActive = false;
  ledState      = false;
  digitalWrite(LED_PIN, LOW);
  Serial.println("[TRIGGER] Compressor cycle complete — LED off");
}

/* ================= API HANDLERS ================ */
void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

void handleStatus() {
  StaticJsonDocument<300> doc;
  doc["wasteLevel"]       = wasteLevel;
  doc["isConnected"]      = isWiFiConnected();
  doc["connectedClients"] = WiFi.softAPgetStationNum();
  doc["mobileUpdate"]     = lastUpdatedFromMobile;
  doc["apIp"]             = WiFi.softAPIP().toString();
  doc["staIp"]            = isWiFiConnected() ? WiFi.localIP().toString() : "";
  doc["triggerActive"]    = triggerActive;
  lastUpdatedFromMobile   = false;
  String res;
  serializeJson(doc, res);
  server.send(200, "application/json", res);
}

void handleUpdate() {
  if (server.hasArg("level")) {
    int newLevel = constrain(server.arg("level").toInt(), 0, 100);
    Serial.print("[UPDATE] Waste level changed: ");
    Serial.print(wasteLevel);
    Serial.print("% → ");
    Serial.print(newLevel);
    Serial.println("%");
    wasteLevel            = newLevel;
    lastUpdatedFromMobile = true;
    lastUpdateTime        = millis();
  }
  server.send(200, "text/plain", "OK");
}

void handleCompress() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  StaticJsonDocument<64> req;
  DeserializationError err = deserializeJson(req, server.arg("plain"));

  // Default to activate if body is empty or missing "trigger" key
  int triggerValue = 1;
  if (!err && req.containsKey("trigger")) {
    triggerValue = req["trigger"].as<int>();
  }

  StaticJsonDocument<64> res;

  if (triggerValue == 1) {
    if (!triggerActive) {
      activateTrigger();
      res["status"]  = "activated";
      res["message"] = "Compressor triggered. LED blinking.";
    } else {
      res["status"]  = "already_active";
      res["message"] = "Compressor already running.";
    }
  } else {
    // trigger == 0 — UI-only disable; hardware runs its own cycle
    res["status"]  = "acknowledged";
    res["message"] = "Disable noted (hardware runs its own cycle).";
  }

  String out;
  serializeJson(res, out);
  server.send(200, "application/json", out);
}

void handleDeviceInfo() {
  StaticJsonDocument<256> doc;
  doc["deviceName"]      = "WESTO Smart Bin";
  doc["firmwareVersion"] = "v1.1.0";
  doc["macAddress"]      = WiFi.softAPmacAddress();
  doc["ipAddress"]       = WiFi.softAPIP().toString();
  doc["mode"]            = "AP + STA";
  String res;
  serializeJson(doc, res);
  server.send(200, "application/json", res);
}

/* ================= SETUP ======================= */
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("\n\n========== WESTO ESP32 BOOTING ==========");

  // --- AP mode ---
  Serial.println("[WiFi] Starting AP mode...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("[WiFi] AP started  | SSID: ");
  Serial.print(ap_ssid);
  Serial.print("  | IP: ");
  Serial.println(WiFi.softAPIP());

  // --- STA mode ---
  Serial.print("[WiFi] Connecting to STA: ");
  Serial.println(sta_ssid);
  WiFi.begin(sta_ssid, sta_password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (isWiFiConnected()) {
    Serial.print("[WiFi] STA connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] STA failed — running AP-only mode");
  }

  // --- Routes ---
  server.on("/",            handleRoot);
  server.on("/status",      handleStatus);
  server.on("/update",      handleUpdate);
  server.on("/compress",    handleCompress);
  server.on("/device/info", handleDeviceInfo);
  server.begin();
  Serial.println("[Server] HTTP server started on port 80");

  printDashboardURLs();
}

/* ================= LOOP ======================== */
unsigned long lastStatusPrint = 0;

void loop() {
  server.handleClient();

  /* ---- Trigger / LED blink (non-blocking) ---- */
  if (triggerActive) {
    unsigned long now = millis();

    if (now - triggerStartMs >= TRIGGER_DURATION) {
      deactivateTrigger();
    } else if (now - lastBlinkMs >= BLINK_INTERVAL) {
      lastBlinkMs = now;
      ledState    = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
  }

  /* ---- Re-print URL banner every 30 s ---- */
  if (millis() - lastStatusPrint > 30000) {
    lastStatusPrint = millis();
    printDashboardURLs();
  }
}
