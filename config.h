// =============================================================================
//  config.h  -  Build-time configuration, hardware pin map, shared types
//
//  FG Falcon HS-CAN  Transmission-Control-Module  Man-In-The-Middle gateway
//  Target board: Autosport Labs ESP32-CAN-X2 (ESP32-S3-WROOM-1)
// =============================================================================
#pragma once
#include <Arduino.h>

// -----------------------------------------------------------------------------
//  Firmware identity
// -----------------------------------------------------------------------------
#define FW_NAME      "FG-TCM-MITM"
#define FW_VERSION   "1.0.0"

// -----------------------------------------------------------------------------
//  HARDWARE PIN MAP  (Autosport Labs ESP32-CAN-X2 - verified from ASL wiki)
//
//  CAN1 = ESP32-S3 internal TWAI controller  -> wired to the VEHICLE bus
//  CAN2 = on-board MCP2515 (SPI, 16 MHz xtal) -> wired to the TCM bus
//
//  >>> SAFETY ASSIGNMENT <<<
//  The VEHICLE bus is placed on the internal TWAI controller specifically so it
//  can be put into TWAI_MODE_LISTEN_ONLY - a hardware mode in which the
//  controller physically cannot emit dominant bits (no frames, no ACK, no error
//  frames). This is the strongest possible guarantee that we never disturb the
//  car. The firmware additionally contains NO code path that transmits on CAN1.
// -----------------------------------------------------------------------------

// ---- CAN1 / internal TWAI  (VEHICLE bus, listen-only) ----
#define CAN1_TX_GPIO        7
#define CAN1_RX_GPIO        6

// ---- CAN2 / MCP2515 over SPI  (TCM bus, active) ----
#define MCP2515_CS_GPIO     10
#define MCP2515_MOSI_GPIO   11
#define MCP2515_SCK_GPIO    12
#define MCP2515_MISO_GPIO   13
#define MCP2515_INT_GPIO    3      // not strictly required (we poll) but broken out
// MCP2515 crystal is 16 MHz on this board -> Longan mcp_canbus CAN_500KBPS is correct.

// ---- Misc ----
#define STATUS_LED_GPIO     2      // on-board LED1

// -----------------------------------------------------------------------------
//  CAN bitrates  (FG Falcon High-Speed CAN = 500 kbps)
// -----------------------------------------------------------------------------
#define DEFAULT_VEH_BITRATE 500000UL
#define DEFAULT_TCM_BITRATE 500000UL

// -----------------------------------------------------------------------------
//  WiFi defaults  (Access-Point mode is the default: a car has no network)
// -----------------------------------------------------------------------------
#define DEFAULT_AP_SSID     "FG-TCM-MITM"
#define DEFAULT_AP_PASS     "falcon500"   // >= 8 chars required by WPA2
#define WIFI_MODE_AP        0
#define WIFI_MODE_STA       1

// -----------------------------------------------------------------------------
//  Limits / sizing
// -----------------------------------------------------------------------------
#define MAX_RULES           40     // forwarding rules stored / editable
#define MAX_SNIFF_IDS       80     // distinct CAN IDs tracked for the live table
#define WS_BROADCAST_MS     100    // live telemetry / sniffer push period (10 Hz)

// =============================================================================
//  Shared types
// =============================================================================

// A generic CAN frame used internally by both bus drivers.
struct CanFrame {
  uint32_t id = 0;
  uint8_t  len = 0;
  uint8_t  data[8] = {0};
  bool     extd = false;     // FG HS-CAN is all 11-bit standard, but kept general
  uint32_t ts = 0;           // millis() timestamp when seen
};

// What the MITM engine does with a VEHICLE-bus frame (direction is always
// vehicle -> TCM; there is never a TCM -> vehicle path).
enum RuleAction : uint8_t {
  RULE_PASS     = 0,   // forward the raw frame to the TCM bus unchanged
  RULE_BLOCK    = 1,   // drop - do not forward to the TCM bus
  RULE_OVERRIDE = 2,   // rewrite selected signals, then forward to the TCM bus
  RULE_GENERATE = 3    // do not forward; the sim engine synthesises this ID locally
};

struct ForwardRule {
  uint32_t   id      = 0;
  uint8_t    action  = RULE_BLOCK;
  uint16_t   periodMs = 0;        // for GENERATE / stale-fallback synthesis (0 = event only)
  char       name[24] = {0};      // human label (from the DBC)
};

// Persisted configuration (NVS).  Kept POD/flat so it serialises trivially.
struct MitmConfig {
  uint32_t vehBitrate  = DEFAULT_VEH_BITRATE;
  uint32_t tcmBitrate  = DEFAULT_TCM_BITRATE;

  bool     simEnabled      = true;   // master enable for transmitting onto TCM bus
  bool     forceAutoConfig = true;   // override 0x640 so the TCM thinks it's an auto car
  bool     generateMissing = true;   // synthesise consumed msgs if absent on veh bus

  // ---- Diagnostic bridge (OBD tester on the vehicle <-> TCM) ----------------
  // >>> SAFETY <<< When this is enabled the VEHICLE bus leaves hardware
  // LISTEN_ONLY and runs in NORMAL mode so the TCM's diagnostic response can be
  // transmitted back to the tester. The bus then also ACKs all traffic. The
  // ONLY frame ID we ever transmit on the vehicle bus is diagRespId. Applied at
  // boot from this persisted flag; changing it requires a restart.
  bool     diagBridge  = false;      // master enable for the OBD<->TCM diag bridge
  uint32_t diagReqId   = 0x7E1;      // tester physical request   (vehicle -> TCM)
  uint32_t diagReqFunc = 0x7DF;      // tester functional request (vehicle -> TCM); 0 = off
  uint32_t diagRespId  = 0x7E9;      // TCM response (TCM -> vehicle) - the ONLY veh-bus TX

  // 0x640 (PCM_MSG_12) override values used when forceAutoConfig is on
  uint8_t  forwardGearCount = 6;     // ZF 6HP26 = 6 forward gears
  uint8_t  transConfig      = 0;     // 0 = Barra_E265, 1 = Copperhead
  float    axleRatio        = 2.73f; // typical FG axle ratio

  uint16_t    ruleCount = 0;
  ForwardRule rules[MAX_RULES];

  // WiFi
  uint8_t  wifiMode = WIFI_MODE_AP;
  char     apSsid[32]  = DEFAULT_AP_SSID;
  char     apPass[64]  = DEFAULT_AP_PASS;
  char     staSsid[32] = "";
  char     staPass[64] = "";
};

// Global config instance (defined in FG_TCM_MITM.ino)
extern MitmConfig g_cfg;

// Apply factory-default forwarding rules (defined in mitm_engine.cpp)
void loadDefaultRules(MitmConfig &cfg);
