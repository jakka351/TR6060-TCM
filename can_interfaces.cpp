#include <mcp_can.h>
#include <mcp_can_dfs.h>

// =============================================================================
//  can_interfaces.cpp
// =============================================================================
#include "can_interfaces.h"
#include <SPI.h>
//#include "mcp_canbus.h"      // Autosport Labs / Longan Labs MCP2515 library
#include "driver/twai.h"     // ESP-IDF TWAI (built into the ESP32 Arduino core)
#include "freertos/FreeRTOS.h"   // pdMS_TO_TICKS, TickType_t

BusStats g_vehStats;
BusStats g_tcmStats;
bool     g_vehOnline = false;
bool     g_tcmOnline = false;
bool     g_vehListenOnly = true;   // resolved in initVehicleBus()

// MCP2515 object on the board's CS pin (TCM bus).
static MCP_CAN s_mcp(MCP2515_CS_GPIO);

// ---- map firmware bitrate -> library constants -----------------------------
static twai_timing_config_t twaiTiming(uint32_t br) {
  switch (br) {
    case 1000000: return TWAI_TIMING_CONFIG_1MBITS();
    case 800000:  return TWAI_TIMING_CONFIG_800KBITS();
    case 500000:  return TWAI_TIMING_CONFIG_500KBITS();
    case 250000:  return TWAI_TIMING_CONFIG_250KBITS();
    case 125000:  return TWAI_TIMING_CONFIG_125KBITS();
    case 100000:  return TWAI_TIMING_CONFIG_100KBITS();
    default:      return TWAI_TIMING_CONFIG_500KBITS();
  }
}
static byte mcpBitrate(uint32_t br) {
  switch (br) {
    case 1000000: return CAN_1000KBPS;
    case 500000:  return CAN_500KBPS;
    case 250000:  return CAN_250KBPS;
    case 125000:  return CAN_125KBPS;
    case 100000:  return CAN_100KBPS;
    default:      return CAN_500KBPS;
  }
}

// -----------------------------------------------------------------------------
//  VEHICLE bus = internal TWAI.
//
//  Default (diagBridge OFF): TWAI_MODE_LISTEN_ONLY - the controller never sends
//  a dominant bit (no frames, no ACK, no error frames); a pure observer.
//
//  Diagnostic bridge ON: TWAI_MODE_NORMAL so the TCM's diagnostic response can
//  be transmitted back to the tester. In NORMAL mode the node also ACKs all bus
//  traffic. Software still only ever calls vehSend() for the response ID.
// -----------------------------------------------------------------------------
static bool initVehicleBus() {
  g_vehListenOnly = !g_cfg.diagBridge;
  twai_mode_t mode = g_vehListenOnly ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL;

  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN1_TX_GPIO, (gpio_num_t)CAN1_RX_GPIO, mode);
  g.rx_queue_len = 64;
  g.tx_queue_len = g_vehListenOnly ? 0 : 16;   // no TX queue at all when silent
  twai_timing_config_t t = twaiTiming(g_cfg.vehBitrate);
  twai_filter_config_t fcfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g, &t, &fcfg) != ESP_OK) return false;
  if (twai_start() != ESP_OK) return false;
  twai_reconfigure_alerts(TWAI_ALERT_RX_DATA | TWAI_ALERT_BUS_ERROR, NULL);
  return true;
}

// -----------------------------------------------------------------------------
//  TCM bus = MCP2515 in normal mode.
// -----------------------------------------------------------------------------
static bool initTcmBus() {
  // ESP32-S3 SPI must be told which pins to use before the MCP2515 library runs.
  SPI.begin(MCP2515_SCK_GPIO, MCP2515_MISO_GPIO, MCP2515_MOSI_GPIO, MCP2515_CS_GPIO);
  pinMode(MCP2515_INT_GPIO, INPUT);

  byte br = mcpBitrate(g_cfg.tcmBitrate);
  for (int attempt = 0; attempt < 10; attempt++) {
    if (s_mcp.begin(br) == CAN_OK) return true;   // 16 MHz crystal assumed by lib
    delay(100);
  }
  return false;
}

bool canInit() {
  g_vehOnline = initVehicleBus();
  g_tcmOnline = initTcmBus();
  return g_vehOnline && g_tcmOnline;
}

// -----------------------------------------------------------------------------
//  Receive / transmit
// -----------------------------------------------------------------------------
bool vehReceive(CanFrame &f) {
  twai_message_t m;
  if (twai_receive(&m, 0) != ESP_OK) return false;   // 0 ticks = non-blocking
  f.id   = m.identifier;
  f.extd = m.extd;
  f.len  = m.data_length_code > 8 ? 8 : m.data_length_code;
  memcpy(f.data, m.data, f.len);
  f.ts   = millis();
  g_vehStats.rx++;
  g_vehStats._fpsAccum++;
  return true;
}

// GUARDED vehicle-bus transmit. Refuses unless the diagnostic bridge has put
// the bus in NORMAL mode. This is the only path that can transmit on the car.
bool vehSend(const CanFrame &f) {
  if (g_vehListenOnly) return false;          // hard guard: silent bus, never TX
  twai_message_t m = {};
  m.identifier = f.id;
  m.extd = f.extd ? 1 : 0;
  m.data_length_code = f.len > 8 ? 8 : f.len;
  memcpy(m.data, f.data, m.data_length_code);
  if (twai_transmit(&m, pdMS_TO_TICKS(20)) == ESP_OK) { g_vehStats.tx++; return true; }
  g_vehStats.err++;
  return false;
}

bool tcmReceive(CanFrame &f) {
  if (s_mcp.checkReceive() != CAN_MSGAVAIL) return false;
  uint8_t len = 0;
  if (s_mcp.readMsgBuf(&len, f.data) != CAN_OK) return false;
  f.id   = s_mcp.getCanId();
  f.extd = s_mcp.isExtendedFrame();
  f.len  = len > 8 ? 8 : len;
  f.ts   = millis();
  g_tcmStats.rx++;
  g_tcmStats._fpsAccum++;
  return true;
}

bool tcmSend(const CanFrame &f) {
  byte ext = f.extd ? 1 : 0;
  if (s_mcp.sendMsgBuf(f.id, ext, f.len, (byte *)f.data) == CAN_OK) {
    g_tcmStats.tx++;
    return true;
  }
  g_tcmStats.err++;
  return false;
}

void canTickStats() {
  g_vehStats.fps = g_vehStats._fpsAccum; g_vehStats._fpsAccum = 0;
  g_tcmStats.fps = g_tcmStats._fpsAccum; g_tcmStats._fpsAccum = 0;

  // Surface TWAI bus errors on the (passive) vehicle side.
  twai_status_info_t st;
  if (twai_get_status_info(&st) == ESP_OK) {
    g_vehStats.err = st.bus_error_count;
    // Listen-only nodes can drift to BUS-OFF on a noisy bus; auto-recover.
    if (st.state == TWAI_STATE_BUS_OFF) {
      twai_initiate_recovery();
    } else if (st.state == TWAI_STATE_STOPPED) {
      twai_start();
    }
  }
}
