// =============================================================================
//  mitm_engine.cpp
// =============================================================================
#include "mitm_engine.h"
#include "can_interfaces.h"

VehicleState g_veh;
TcmState     g_tcm;
FrameRecord  g_sniff[MAX_SNIFF_IDS];
uint32_t     g_forwardedCount = 0;
uint32_t     g_blockedCount   = 0;
uint32_t     g_overrideCount  = 0;
uint32_t     g_diagToTcm      = 0;
uint32_t     g_diagToVeh      = 0;

// Bench-idle simulation defaults (used only when generateMissing is on AND no
// live vehicle engine data is arriving - i.e. the rig is on the bench).
#define BENCH_IDLE_RPM   750.0f
#define BENCH_IDLE_TQ    30        // Nm

// -----------------------------------------------------------------------------
//  Factory-default forwarding rules.
//
//  Everything the TCM needs to run is forwarded straight from the live car
//  (RULE_PASS).  The single driveline-config message 0x640 is rewritten
//  (RULE_OVERRIDE) so the auto TCM enables in this manual car.  Everything the
//  TCM emits is, by construction, never a forwarding candidate; the BLOCK rows
//  below are documentation/UI clarity only.
// -----------------------------------------------------------------------------
void loadDefaultRules(MitmConfig &cfg) {
  cfg.ruleCount = 0;
  auto add = [&](uint32_t id, uint8_t act, uint16_t per, const char *nm) {
    if (cfg.ruleCount >= MAX_RULES) return;
    ForwardRule &r = cfg.rules[cfg.ruleCount++];
    r.id = id; r.action = act; r.periodMs = per;
    strncpy(r.name, nm, sizeof(r.name) - 1);
    r.name[sizeof(r.name) - 1] = 0;
  };

  // ---- vehicle -> TCM : forward the engine / brake / wheel inputs verbatim ----
  add(ID_PCM_MSG_1,  RULE_PASS, 0, "PCM_MSG_1 torque");
  add(ID_PCM_MSG_2,  RULE_PASS, 0, "PCM_MSG_2 tcs");
  add(ID_PCM_MSG_3,  RULE_PASS, 0, "PCM_MSG_3 torque");
  add(ID_PCM_MSG_4,  RULE_PASS, 0, "PCM_MSG_4 rpm/pedal");
  add(ID_PCM_MSG_5,  RULE_PASS, 0, "PCM_MSG_5 avail tq");
  add(ID_PCM_MSG_6,  RULE_PASS, 0, "PCM_MSG_6 rpm/vss");
  add(ID_ABS_MSG_1,  RULE_PASS, 0, "ABS_MSG_1 tq req");
  add(ID_TCS_MSG_1,  RULE_PASS, 0, "TCS_MSG_1 wheels");
  add(ID_PCM_MSG_11, RULE_PASS, 0, "PCM_MSG_11 eng cfg");
  add(ID_PCM_MSG_9,  RULE_PASS, 0, "PCM_MSG_9 temps");
  add(ID_PCM_MSG_8,  RULE_PASS, 0, "PCM_MSG_8 coolant");

  // ---- the keystone: rewrite driveline config to "automatic / TCM control" ----
  add(ID_PCM_MSG_12, RULE_OVERRIDE, 100, "PCM_MSG_12 cfg*");

  // ---- TCM-origin IDs: explicitly blocked from the vehicle (belt & braces) ----
  add(ID_TCM_MSG_1,   RULE_BLOCK, 0, "TCM_MSG_1 (out)");
  add(ID_TCM_MSG_2,   RULE_BLOCK, 0, "TCM_MSG_2 (out)");
  add(ID_PCM_MSG_7,   RULE_BLOCK, 0, "PCM_MSG_7 gear");
  add(ID_TCM_DIAG_TX, RULE_BLOCK, 0, "TCM_Diag_Tx");
}

ForwardRule *mitmFindRule(uint32_t id) {
  for (uint16_t i = 0; i < g_cfg.ruleCount; i++)
    if (g_cfg.rules[i].id == id) return &g_cfg.rules[i];
  return nullptr;
}

// -----------------------------------------------------------------------------
//  Sniffer table maintenance
// -----------------------------------------------------------------------------
static FrameRecord *sniffSlot(uint32_t id, uint8_t bus) {
  FrameRecord *freeSlot = nullptr;
  for (int i = 0; i < MAX_SNIFF_IDS; i++) {
    if (g_sniff[i].used && g_sniff[i].id == id && g_sniff[i].bus == bus)
      return &g_sniff[i];
    if (!g_sniff[i].used && !freeSlot) freeSlot = &g_sniff[i];
  }
  if (freeSlot) {
    freeSlot->used = true; freeSlot->id = id; freeSlot->bus = bus;
    freeSlot->count = 0; freeSlot->periodMs = 0; freeSlot->lastTs = 0;
  }
  return freeSlot;  // may be null if the table is full
}

static void sniffRecord(uint32_t id, uint8_t bus, const uint8_t *data,
                        uint8_t len, bool forwarded) {
  FrameRecord *r = sniffSlot(id, bus);
  if (!r) return;
  uint32_t now = millis();
  if (r->lastTs) {
    uint32_t dt = now - r->lastTs;
    // simple EMA so the displayed period is stable
    r->periodMs = r->periodMs ? (uint16_t)((r->periodMs * 3 + dt) / 4) : (uint16_t)dt;
  }
  r->lastTs = now;
  r->len = len > 8 ? 8 : len;
  memcpy(r->data, data, r->len);
  r->count++;
  r->forwarded = forwarded;
}

// -----------------------------------------------------------------------------
//  Vehicle-bus frame handler  (the gate)
// -----------------------------------------------------------------------------
void mitmOnVehicleFrame(const CanFrame &f) {
  // 1) Always decode for the dashboard / simulated-vehicle state.
  fgDecodeVehicleFrame(f, g_veh);

  // 1a) Diagnostic bridge: relay a tester's request straight to the TCM. This
  // is the ordinary, always-safe vehicle -> TCM direction, handled before the
  // rule table so a stray BLOCK rule can never break a diagnostic session.
  if (g_cfg.diagBridge &&
      (f.id == g_cfg.diagReqId || (g_cfg.diagReqFunc && f.id == g_cfg.diagReqFunc))) {
    tcmSend(f);
    g_diagToTcm++;
    sniffRecord(f.id, /*bus=*/0, f.data, f.len, /*forwarded=*/true);
    return;
  }

  // 2) Decide whether (and how) to forward to the TCM bus.
  bool forwarded = false;
  if (g_cfg.simEnabled) {
    ForwardRule *rule = mitmFindRule(f.id);
    uint8_t action = rule ? rule->action : RULE_BLOCK;   // default: do NOT leak

    if (action == RULE_PASS) {
      tcmSend(f);
      g_forwardedCount++;
      forwarded = true;
    } else if (action == RULE_OVERRIDE) {
      CanFrame o = f;
      if (f.id == ID_PCM_MSG_12 && g_cfg.forceAutoConfig) {
        fgBuildAutoConfig(o.data, g_cfg, /*seed=*/false);  // rewrite live frame
      }
      tcmSend(o);
      g_forwardedCount++;
      g_overrideCount++;
      forwarded = true;
    } else {
      g_blockedCount++;   // BLOCK or GENERATE -> not forwarded here
    }
  }

  sniffRecord(f.id, /*bus=*/0, f.data, f.len, forwarded);
}

// -----------------------------------------------------------------------------
//  TCM-bus frame handler.
//
//  Telemetry is decoded for the dashboard. The ONLY frame ever relayed back to
//  the vehicle bus is the diagnostic response (diagRespId), and only when the
//  diagnostic bridge is enabled (which is also what put the vehicle bus into a
//  transmit-capable mode). Every other TCM frame stays on the TCM side.
// -----------------------------------------------------------------------------
void mitmOnTcmFrame(const CanFrame &f) {
  fgDecodeTcmFrame(f, g_tcm);

  bool relayed = false;
  if (g_cfg.diagBridge && f.id == g_cfg.diagRespId) {
    relayed = vehSend(f);            // reverse bridge: TCM response -> tester
    if (relayed) g_diagToVeh++;
  }
  sniffRecord(f.id, /*bus=*/1, f.data, f.len, relayed);
}

// -----------------------------------------------------------------------------
//  Encoders used by the bench-idle simulator
// -----------------------------------------------------------------------------
static void encEngineRpmPedal(uint8_t *d, float rpm) {       // 0x12D
  memset(d, 0, 8);
  fgSetMotorola(d, 39, 16, (uint64_t)(rpm / 0.25f));          // Engine_Speed
}
static void encRpmVss(uint8_t *d, float rpm, float kph) {    // 0x207
  memset(d, 0, 8);
  fgSetMotorola(d,  7, 16, (uint64_t)(rpm / 0.25f));          // Engine_Speed
  fgSetMotorola(d, 39, 16, (uint64_t)(kph / 0.0078125f));     // Vehicle_Speed
}
static void encTorque(uint8_t *d, int16_t nm) {              // 0x097
  memset(d, 0, 8);
  fgSetMotorola(d,  7, 16, (uint16_t)nm);                     // Indicated_Eng_Torque
  fgSetMotorola(d, 39, 16, (uint16_t)nm);                     // Actual_Eng_Torque
  fgSetMotorola(d, 55, 16, (uint16_t)nm);                     // Driver_Demand_Torque
}
static void encWheels(uint8_t *d, float kph) {              // 0x4B0
  memset(d, 0, 8);
  uint16_t raw = (uint16_t)(kph / 0.01f);
  fgSetMotorola(d,  7, 16, raw);  fgSetMotorola(d, 23, 16, raw);
  fgSetMotorola(d, 39, 16, raw);  fgSetMotorola(d, 55, 16, raw);
}

static void sendSynth(uint32_t id, const uint8_t *d, uint8_t len) {
  CanFrame f; f.id = id; f.len = len; f.extd = false;
  memcpy(f.data, d, len);
  tcmSend(f);
  g_forwardedCount++;
  sniffRecord(id, /*bus=*/1, d, len, true);   // show synthesised frames on TCM side
}

// -----------------------------------------------------------------------------
//  Periodic simulated-vehicle work
// -----------------------------------------------------------------------------
void mitmPeriodic() {
  if (!g_cfg.simEnabled) return;
  uint32_t now = millis();

  // ---- (A) keep a valid auto driveline-config on the TCM bus ----------------
  // If the car sends 0x640 we already rewrote+forwarded it in the gate; here we
  // top it up if it has gone stale (or the car never sends it at all).
  static uint32_t lastCfg = 0;
  ForwardRule *cfgRule = mitmFindRule(ID_PCM_MSG_12);
  uint16_t cfgPer = cfgRule && cfgRule->periodMs ? cfgRule->periodMs : 100;
  FrameRecord *cfgRec = nullptr;
  for (int i = 0; i < MAX_SNIFF_IDS; i++)
    if (g_sniff[i].used && g_sniff[i].id == ID_PCM_MSG_12 && g_sniff[i].bus == 0)
      cfgRec = &g_sniff[i];
  bool carSendsCfg = cfgRec && (now - cfgRec->lastTs < 250);
  if (g_cfg.forceAutoConfig && !carSendsCfg && (now - lastCfg >= cfgPer)) {
    lastCfg = now;
    uint8_t d[8];
    fgBuildAutoConfig(d, g_cfg, /*seed=*/true);   // fabricate from scratch
    sendSynth(ID_PCM_MSG_12, d, 8);
  }

  // ---- (B) optional bench-idle vehicle simulation ---------------------------
  // Only when explicitly enabled AND the live car's engine data is absent.
  bool liveEngine = (now - g_veh.lastEngineMs) < 500 && g_veh.lastEngineMs != 0;
  if (g_cfg.generateMissing && !liveEngine) {
    static uint32_t t20 = 0;
    if (now - t20 >= 20) {                  // 50 Hz block of core inputs
      t20 = now;
      uint8_t d[8];
      encEngineRpmPedal(d, BENCH_IDLE_RPM);          sendSynth(ID_PCM_MSG_4, d, 8);
      encRpmVss(d, BENCH_IDLE_RPM, 0);               sendSynth(ID_PCM_MSG_6, d, 8);
      encTorque(d, BENCH_IDLE_TQ);                   sendSynth(ID_PCM_MSG_1, d, 8);
      encWheels(d, 0);                               sendSynth(ID_TCS_MSG_1, d, 8);
    }
  }
}

void mitmInit() {
  memset(g_sniff, 0, sizeof(g_sniff));
}
