// =============================================================================
//  mitm_engine.h  -  the man-in-the-middle gating + simulated-vehicle engine
//
//  Direction is hard-wired: VEHICLE --> TCM only.  Nothing this engine does can
//  put a frame on the vehicle bus (there is no vehicle transmit function at all).
// =============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "fg_falcon_can.h"

// One row of the live sniffer table.
struct FrameRecord {
  uint32_t id = 0;
  uint8_t  bus = 0;          // 0 = vehicle, 1 = TCM
  uint8_t  len = 0;
  uint8_t  data[8] = {0};
  uint32_t count = 0;
  uint16_t periodMs = 0;
  uint32_t lastTs = 0;
  bool     forwarded = false; // was the most recent veh frame forwarded to TCM?
  bool     used = false;
};

// Shared decoded state (written only from the loop task).
extern VehicleState g_veh;
extern TcmState     g_tcm;

extern FrameRecord  g_sniff[MAX_SNIFF_IDS];
extern uint32_t     g_forwardedCount;   // total frames forwarded veh->TCM
extern uint32_t     g_blockedCount;     // total veh frames not forwarded
extern uint32_t     g_overrideCount;    // total frames rewritten before forward

void mitmInit();

// Called for every frame received on each bus.
void mitmOnVehicleFrame(const CanFrame &f);
void mitmOnTcmFrame(const CanFrame &f);

// Periodic simulated-vehicle work (0x640 auto-config + optional bench idle sim).
void mitmPeriodic();

// Rule lookup helper (returns nullptr if no explicit rule for this ID).
ForwardRule *mitmFindRule(uint32_t id);
