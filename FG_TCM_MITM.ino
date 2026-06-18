// =============================================================================
//  FG_TCM_MITM.ino
//
//  FG Falcon HS-CAN  Transmission-Control-Module  Man-In-The-Middle gateway
//  Board: Autosport Labs ESP32-CAN-X2 (ESP32-S3-WROOM-1)
//
//  Purpose
//  -------
//  An FG Falcon fitted with a MANUAL transmission is being used to develop an
//  AUTOMATIC transmission control module (TCM) on the bench / in-car. This
//  gateway sits between two physically separate CAN segments:
//
//     VEHICLE bus  -> CAN1 (internal TWAI), HARDWARE LISTEN-ONLY / SILENT
//     TCM bus      -> CAN2 (MCP2515),       ACTIVE  (we are the "vehicle")
//
//  It passively reads the real car, forwards the gated subset of messages the
//  TCM needs onto the TCM bus, rewrites the driveline-config message (0x640) so
//  the auto TCM enables itself, and decodes everything the TCM emits. By default
//  data only ever flows VEHICLE -> TCM and the TWAI controller is in listen-only
//  mode (a hardware backstop), so the car's bus cannot be disturbed.
//
//  One optional, tightly-scoped reverse path exists: the DIAGNOSTIC BRIDGE. When
//  enabled it puts the vehicle bus in NORMAL mode and relays the TCM's 0x7E9
//  diagnostic response back to an OBD tester (and the tester's 0x7E1 request to
//  the TCM). Even then, 0x7E9 is the ONLY ID ever transmitted on the car. The
//  bridge is off by default and changing it requires a restart.
//
//  A WiFi web dashboard (default AP "FG-TCM-MITM" / "falcon500", http://192.168.4.1)
//  gives a live CAN sniffer, decoded TCM telemetry, editable forwarding rules,
//  a TCM-bus frame injector and all configuration.
//
//  Libraries required (see README.md for versions/sources):
//    - ESP32 Arduino core 3.x       (WiFi, Preferences, driver/twai.h)
//    - ESPAsyncWebServer + AsyncTCP (ESP32Async maintained forks)
//    - ArduinoJson 7.x
//  The MCP2515 (TCM bus) is driven by our own mcp_jakka.* (class McpJakka) - NO
//  external CAN library is needed (we dropped the buggy Longan/Seeed mcp_can lib).
// =============================================================================
#include "config.h"
#include "can_interfaces.h"
#include "mitm_engine.h"
#include "web_interface.h"

// The one and only configuration instance.
MitmConfig g_cfg;

static void blink(int n, int ms) {
  for (int i = 0; i < n; i++) { digitalWrite(STATUS_LED_GPIO, HIGH); delay(ms);
                                digitalWrite(STATUS_LED_GPIO, LOW);  delay(ms); }
}

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED_GPIO, OUTPUT);

  Serial.printf("\n%s v%s  booting...\n", FW_NAME, FW_VERSION);

  cfgLoad();          // load saved config from NVS (seeds defaults on first boot)
  mitmInit();

  if (!canInit()) {
    Serial.printf("CAN init: vehicle=%s  tcm=%s\n",
                  g_vehOnline ? "ok" : "FAIL", g_tcmOnline ? "ok" : "FAIL");
    // Keep running so the dashboard is still reachable for diagnosis.
  } else {
    Serial.println("CAN: vehicle bus = LISTEN-ONLY, TCM bus = active");
  }

  webInit();
  Serial.printf("Web UI ready. Connect WiFi and browse to the device IP.\n");
  blink(3, 80);
}

void loop() {
  CanFrame f;

  // ---- drain the vehicle bus (listen-only) and run the gate -----------------
  for (int i = 0; i < 24 && vehReceive(f); i++) {
    mitmOnVehicleFrame(f);     // decode + (gated) forward to the TCM bus
  }

  // ---- drain the TCM bus (decode only; never forwarded to the vehicle) ------
  for (int i = 0; i < 24 && tcmReceive(f); i++) {
    mitmOnTcmFrame(f);
  }

  // ---- simulated-vehicle periodic work (0x640 auto-config, bench idle sim) --
  mitmPeriodic();

  // ---- 1 Hz housekeeping ----------------------------------------------------
  static uint32_t lastSec = 0;
  uint32_t now = millis();
  if (now - lastSec >= 1000) {
    lastSec = now;
    canTickStats();
    digitalWrite(STATUS_LED_GPIO, !digitalRead(STATUS_LED_GPIO));  // heartbeat
  }

  // ---- web: apply pending config edits + push live telemetry ----------------
  webLoop();
}
