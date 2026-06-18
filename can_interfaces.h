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

// Initialise both controllers.  Returns false if either failed to come up.
bool canInit();

// Vehicle bus (listen-only).  Non-blocking; returns true when a frame was read.
bool vehReceive(CanFrame &f);

// TCM bus (active).  Non-blocking receive; returns true when a frame was read.
bool tcmReceive(CanFrame &f);

// TCM bus transmit.  This is the ONLY transmit path in the firmware and it can
// only ever reach the TCM bus - never the vehicle.  Returns true on success.
bool tcmSend(const CanFrame &f);

// Update per-second frame-rate counters; call once per second from loop().
void canTickStats();
