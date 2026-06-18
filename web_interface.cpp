// =============================================================================
//  web_interface.cpp
// =============================================================================
#include "web_interface.h"
#include "config.h"
#include "mitm_engine.h"
#include "can_interfaces.h"
#include "fg_falcon_can.h"
#include "index_html.h"

#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static Preferences    prefs;

// ---- config staging (written by the async task, applied by the loop task) ---
static MitmConfig      s_staging;
static volatile bool   s_applyPending = false;
static SemaphoreHandle_t s_stageMutex;

static String s_ip = "0.0.0.0";

// -----------------------------------------------------------------------------
//  Persistence  (whole MitmConfig stored as one NVS blob)
// -----------------------------------------------------------------------------
void cfgSave() {
  prefs.begin("mitm", false);
  prefs.putBytes("cfg", &g_cfg, sizeof(g_cfg));
  prefs.end();
}

void cfgLoad() {
  prefs.begin("mitm", true);
  size_t n = prefs.getBytesLength("cfg");
  if (n == sizeof(g_cfg)) {
    prefs.getBytes("cfg", &g_cfg, sizeof(g_cfg));
    prefs.end();
  } else {
    prefs.end();
    // First boot / layout change: seed defaults.
    g_cfg = MitmConfig();
    loadDefaultRules(g_cfg);
    cfgSave();
  }
}

// -----------------------------------------------------------------------------
//  JSON helpers
// -----------------------------------------------------------------------------
static void buildConfigJson(JsonDocument &doc) {
  doc["fw"]  = FW_NAME;
  doc["ver"] = FW_VERSION;
  doc["ip"]  = s_ip;
  doc["vehBitrate"] = g_cfg.vehBitrate;
  doc["tcmBitrate"] = g_cfg.tcmBitrate;
  doc["simEnabled"] = g_cfg.simEnabled;
  doc["forceAutoConfig"] = g_cfg.forceAutoConfig;
  doc["generateMissing"] = g_cfg.generateMissing;
  doc["diagBridge"]  = g_cfg.diagBridge;
  doc["diagReqId"]   = g_cfg.diagReqId;
  doc["diagReqFunc"] = g_cfg.diagReqFunc;
  doc["diagRespId"]  = g_cfg.diagRespId;
  doc["vehListenOnly"] = g_vehListenOnly;   // live status (read-only)
  doc["forwardGearCount"] = g_cfg.forwardGearCount;
  doc["transConfig"] = g_cfg.transConfig;
  doc["axleRatio"] = g_cfg.axleRatio;
  doc["wifiMode"] = g_cfg.wifiMode;
  doc["apSsid"] = g_cfg.apSsid;
  doc["apPass"] = g_cfg.apPass;
  doc["staSsid"] = g_cfg.staSsid;
  doc["staPass"] = g_cfg.staPass;
  JsonArray ra = doc["rules"].to<JsonArray>();
  for (uint16_t i = 0; i < g_cfg.ruleCount; i++) {
    JsonObject o = ra.add<JsonObject>();
    o["id"] = g_cfg.rules[i].id;
    o["name"] = g_cfg.rules[i].name;
    o["action"] = g_cfg.rules[i].action;
    o["periodMs"] = g_cfg.rules[i].periodMs;
  }
}

// Parse a posted config document into the staging buffer, then flag for apply.
static bool stageConfigFromJson(JsonDocument &doc) {
  if (xSemaphoreTake(s_stageMutex, pdMS_TO_TICKS(200)) != pdTRUE) return false;
  s_staging = g_cfg;   // start from current, override provided fields

  if (!doc["vehBitrate"].isNull())      s_staging.vehBitrate = doc["vehBitrate"].as<uint32_t>();
  if (!doc["tcmBitrate"].isNull())      s_staging.tcmBitrate = doc["tcmBitrate"].as<uint32_t>();
  if (!doc["simEnabled"].isNull())      s_staging.simEnabled = doc["simEnabled"].as<bool>();
  if (!doc["forceAutoConfig"].isNull()) s_staging.forceAutoConfig = doc["forceAutoConfig"].as<bool>();
  if (!doc["generateMissing"].isNull()) s_staging.generateMissing = doc["generateMissing"].as<bool>();
  if (!doc["diagBridge"].isNull())      s_staging.diagBridge = doc["diagBridge"].as<bool>();
  if (!doc["diagReqId"].isNull())       s_staging.diagReqId = doc["diagReqId"].as<uint32_t>();
  if (!doc["diagReqFunc"].isNull())     s_staging.diagReqFunc = doc["diagReqFunc"].as<uint32_t>();
  if (!doc["diagRespId"].isNull())      s_staging.diagRespId = doc["diagRespId"].as<uint32_t>();
  if (!doc["forwardGearCount"].isNull())s_staging.forwardGearCount = doc["forwardGearCount"].as<uint8_t>();
  if (!doc["transConfig"].isNull())     s_staging.transConfig = doc["transConfig"].as<uint8_t>();
  if (!doc["axleRatio"].isNull())       s_staging.axleRatio = doc["axleRatio"].as<float>();
  if (!doc["wifiMode"].isNull())        s_staging.wifiMode = doc["wifiMode"].as<uint8_t>();
  auto cpy = [](char *dst, const char *src, size_t n){ if(src){ strncpy(dst,src,n-1); dst[n-1]=0; } };
  if (!doc["apSsid"].isNull())  cpy(s_staging.apSsid,  doc["apSsid"].as<const char*>(),  sizeof(s_staging.apSsid));
  if (!doc["apPass"].isNull())  cpy(s_staging.apPass,  doc["apPass"].as<const char*>(),  sizeof(s_staging.apPass));
  if (!doc["staSsid"].isNull()) cpy(s_staging.staSsid, doc["staSsid"].as<const char*>(), sizeof(s_staging.staSsid));
  if (!doc["staPass"].isNull()) cpy(s_staging.staPass, doc["staPass"].as<const char*>(), sizeof(s_staging.staPass));

  if (doc["rules"].is<JsonArray>()) {
    JsonArray ra = doc["rules"].as<JsonArray>();
    uint16_t n = 0;
    for (JsonObject o : ra) {
      if (n >= MAX_RULES) break;
      ForwardRule &r = s_staging.rules[n];
      r.id = o["id"] | 0;
      r.action = o["action"] | (uint8_t)RULE_BLOCK;
      r.periodMs = o["periodMs"] | 0;
      const char *nm = o["name"] | "";
      strncpy(r.name, nm, sizeof(r.name) - 1); r.name[sizeof(r.name) - 1] = 0;
      n++;
    }
    s_staging.ruleCount = n;
  }
  s_applyPending = true;
  xSemaphoreGive(s_stageMutex);
  return true;
}

// -----------------------------------------------------------------------------
//  Generic JSON body collector for POST handlers
// -----------------------------------------------------------------------------
typedef std::function<void(AsyncWebServerRequest *, JsonDocument &)> JsonHandler;

static void onJsonBody(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                       size_t index, size_t total, JsonHandler cb) {
  String *buf = (String *)req->_tempObject;
  if (index == 0) { buf = new String(); buf->reserve(total + 1); req->_tempObject = buf; }
  buf->concat((const char *)data, len);
  if (index + len == total) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, *buf);
    delete buf; req->_tempObject = nullptr;
    if (err) { req->send(400, "application/json", "{\"err\":\"bad json\"}"); return; }
    cb(req, doc);
  }
}

// -----------------------------------------------------------------------------
//  Live telemetry broadcast
// -----------------------------------------------------------------------------
static void buildLiveJson(String &out) {
  JsonDocument doc;
  doc["t"] = millis();

  JsonObject t = doc["tcm"].to<JsonObject>();
  bool seen = g_tcm.seen();
  t["seen"]   = seen;
  t["gearSel"]= fgGearSelectedName(g_tcm.gearSelected);
  t["gearAct"]= fgGearActualName(g_tcm.gearActual);
  t["gearTgt"]= fgGearTargetName(g_tcm.gearTarget);
  t["map"]    = fgShiftMapName(g_tcm.shiftMap);
  t["oil"]    = g_tcm.oilTempC;
  t["inRpm"]  = (int)g_tcm.inputRpm;
  t["outRpm"] = (int)g_tcm.outputRpm;
  t["slip"]   = g_tcm.tcSlip;
  t["tcLock"] = g_tcm.tcLocked;
  t["shift"]  = g_tcm.shiftInProgress;
  t["malf"]   = g_tcm.transMalfunction;
  t["mil"]    = g_tcm.mil;
  t["gsf"]    = g_tcm.gearSelFault;

  JsonObject v = doc["veh"].to<JsonObject>();
  v["rx"] = g_vehStats.rx; v["fps"] = g_vehStats.fps; v["err"] = g_vehStats.err;
  v["tx"] = g_vehStats.tx; v["listenOnly"] = g_vehListenOnly;

  JsonObject dg = doc["diag"].to<JsonObject>();
  dg["bridge"] = g_cfg.diagBridge;
  dg["toTcm"]  = g_diagToTcm;
  dg["toVeh"]  = g_diagToVeh;
  dg["reqId"]  = g_cfg.diagReqId;
  dg["respId"] = g_cfg.diagRespId;

  JsonObject b = doc["bus"].to<JsonObject>();
  b["rx"] = g_tcmStats.rx; b["tx"] = g_tcmStats.tx; b["fps"] = g_tcmStats.fps; b["err"] = g_tcmStats.err;

  JsonObject s = doc["sim"].to<JsonObject>();
  s["en"]   = g_cfg.simEnabled;
  s["auto"] = g_cfg.forceAutoConfig;
  bool liveEngine = (millis() - g_veh.lastEngineMs) < 500 && g_veh.lastEngineMs != 0;
  s["bench"]= g_cfg.generateMissing && !liveEngine;
  s["rpm"]  = (int)g_veh.engineRpm;
  s["kph"]  = g_veh.vehicleKph;
  s["pedal"]= g_veh.pedalPct;
  s["thr"]  = g_veh.throttlePct;

  doc["fwd"] = g_forwardedCount;
  doc["ovr"] = g_overrideCount;
  doc["blk"] = g_blockedCount;

  JsonArray fr = doc["frames"].to<JsonArray>();
  char hexbuf[32];
  for (int i = 0; i < MAX_SNIFF_IDS; i++) {
    if (!g_sniff[i].used) continue;
    FrameRecord &r = g_sniff[i];
    JsonObject o = fr.add<JsonObject>();
    o["id"]  = r.id;
    o["bus"] = r.bus;
    o["dlc"] = r.len;
    o["per"] = r.periodMs;
    o["cnt"] = r.count;
    o["fwd"] = r.forwarded;
    const char *nm = fgIdName(r.id);
    if (nm[0]) o["name"] = nm;
    int p = 0;
    for (int k = 0; k < r.len; k++) p += snprintf(hexbuf + p, sizeof(hexbuf) - p, "%02X ", r.data[k]);
    o["d"] = hexbuf;
  }
  serializeJson(doc, out);
}

// -----------------------------------------------------------------------------
//  WiFi + routes
// -----------------------------------------------------------------------------
static void startWifi() {
  if (g_cfg.wifiMode == WIFI_MODE_STA && g_cfg.staSsid[0]) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_cfg.staSsid, g_cfg.staPass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(200);
    if (WiFi.status() == WL_CONNECTED) { s_ip = WiFi.localIP().toString(); return; }
    // fall through to AP if STA failed
  }
  WiFi.mode(WIFI_AP);
  WiFi.softAP(g_cfg.apSsid, g_cfg.apPass);
  s_ip = WiFi.softAPIP().toString();
}

static void wsEvent(AsyncWebSocket *s, AsyncWebSocketClient *c, AwsEventType type,
                    void *arg, uint8_t *data, size_t len) {
  // Live data is pushed from webLoop(); nothing required on receive.
}

void webInit() {
  s_stageMutex = xSemaphoreCreateMutex();
  startWifi();

  ws.onEvent(wsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    // On ESP32, PROGMEM data is memory-mapped, so INDEX_HTML can be passed
    // directly. send() (rather than the deprecated send_P) is stable across
    // both the legacy and ESP32Async ESPAsyncWebServer forks.
    req->send(200, "text/html", INDEX_HTML);
  });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc; buildConfigJson(doc);
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
    [](AsyncWebServerRequest *req, uint8_t *d, size_t l, size_t i, size_t tot) {
      onJsonBody(req, d, l, i, tot, [](AsyncWebServerRequest *rq, JsonDocument &doc) {
        bool ok = stageConfigFromJson(doc);
        rq->send(ok ? 200 : 503, "application/json", ok ? "{\"ok\":1}" : "{\"err\":\"busy\"}");
      });
    });

  server.on("/api/defaults", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (xSemaphoreTake(s_stageMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      s_staging = g_cfg;
      loadDefaultRules(s_staging);
      s_applyPending = true;
      xSemaphoreGive(s_stageMutex);
      req->send(200, "application/json", "{\"ok\":1}");
    } else req->send(503, "application/json", "{\"err\":\"busy\"}");
  });

  // Manual frame injection - TCM BUS ONLY. There is deliberately no vehicle path.
  server.on("/api/inject", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
    [](AsyncWebServerRequest *req, uint8_t *d, size_t l, size_t i, size_t tot) {
      onJsonBody(req, d, l, i, tot, [](AsyncWebServerRequest *rq, JsonDocument &doc) {
        CanFrame f;
        f.id  = doc["id"] | 0;
        f.len = doc["len"] | 0; if (f.len > 8) f.len = 8;
        f.extd = f.id > 0x7FF;
        JsonArray da = doc["data"].as<JsonArray>();
        uint8_t k = 0;
        for (JsonVariant b : da) { if (k >= f.len) break; f.data[k++] = (uint8_t)(b.as<int>() & 0xFF); }
        bool ok = tcmSend(f);
        rq->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":1}" : "{\"err\":\"tx\"}");
      });
    });

  server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", "{\"ok\":1}");
    delay(150);
    ESP.restart();
  });

  server.onNotFound([](AsyncWebServerRequest *req) { req->send(404, "text/plain", "not found"); });
  server.begin();
}

// -----------------------------------------------------------------------------
//  Loop-side work: apply staged config, prune stale sniffer rows, broadcast
// -----------------------------------------------------------------------------
void webLoop() {
  // (1) apply a pending config edit in the loop task (keeps the CAN hot path lock-free)
  if (s_applyPending) {
    if (xSemaphoreTake(s_stageMutex, 0) == pdTRUE) {
      memcpy(&g_cfg, &s_staging, sizeof(g_cfg));
      s_applyPending = false;
      xSemaphoreGive(s_stageMutex);
      cfgSave();
    }
  }

  // (2) broadcast telemetry at a fixed rate
  static uint32_t lastWs = 0;
  uint32_t now = millis();
  if (now - lastWs >= WS_BROADCAST_MS && ws.count() > 0) {
    lastWs = now;
    ws.cleanupClients();
    String out; out.reserve(2048);
    buildLiveJson(out);
    ws.textAll(out);
  }
}
