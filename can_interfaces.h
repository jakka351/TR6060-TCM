// =============================================================================
//  can_interfaces.h  -  dual CAN bus abstraction
//
//   VEHICLE bus : ESP32-S3 internal TWAI controller, TWAI_MODE_LISTEN_ONLY.
//                 RX only.  There is intentionally NO transmit function for it.
//   TCM bus     : on-board MCP2515 (SPI), normal mode.  RX + TX.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"

struct BusStats {
  uint32_t rx = 0;
  uint32_t tx = 0;
  uint32_t err = 0;
  uint32_t fps = 0;          // frames/sec (updated once per second)
  uint32_t _fpsAccum = 0;
  uint32_t _fpsWindow = 0;
};

extern BusStats g_vehStats;
extern BusStats g_tcmStats;
extern bool     g_vehOnline;
extern bool     g_tcmOnline;
extern bool     g_vehListenOnly;   // true = vehicle bus is in hardware LISTEN_ONLY (silent)

// Initialise both controllers.  Returns false if either failed to come up.
// The vehicle bus comes up LISTEN_ONLY unless g_cfg.diagBridge is set, in which
// case it comes up in NORMAL mode (so the diagnostic response can be sent back).
bool canInit();

// Vehicle bus receive.  Non-blocking; returns true when a frame was read.
bool vehReceive(CanFrame &f);

// Vehicle bus transmit.  GUARDED: only succeeds when the diagnostic bridge has
// put the bus in NORMAL mode (i.e. !g_vehListenOnly). Used solely to relay the
// TCM's diagnostic response (diagRespId) back to the tester. Returns false (and
// transmits nothing) whenever the bus is in listen-only mode.
bool vehSend(const CanFrame &f);

// TCM bus (active).  Non-blocking receive; returns true when a frame was read.
bool tcmReceive(CanFrame &f);

// TCM bus transmit.  This is the ONLY transmit path in the firmware and it can
// only ever reach the TCM bus - never the vehicle.  Returns true on success.
bool tcmSend(const CanFrame &f);

// Update per-second frame-rate counters; call once per second from loop().
void canTickStats();
