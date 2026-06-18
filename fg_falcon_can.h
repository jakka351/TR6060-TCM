// =============================================================================
//  fg_falcon_can.h  -  FG Falcon HS-CAN message/signal definitions & codec
//
//  Derived from FG_Falcon_HighSpeed_CAN.dbc.  All signals in that DBC use the
//  Motorola / big-endian (@0) byte order with the standard DBC "start bit =
//  MSB position" sawtooth numbering.  The helpers below implement that codec
//  plus a small Intel/little-endian (@1) helper for completeness.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"

// -----------------------------------------------------------------------------
//  CAN message IDs  (11-bit standard).  Decimal IDs from the DBC shown alongside.
// -----------------------------------------------------------------------------
// ---- Produced BY the TCM (we decode these; NEVER forwarded to the vehicle) ----
#define ID_TCM_MSG_1        0x0C9   // 201  - in/out speed, TC slip, torque limit, torque reduction
#define ID_TCM_MSG_2        0x3E9   // 1001 - gear actual/selected/target, oil temp, shift map, faults
#define ID_TCM_DIAG_TX      0x7E9   // 2025 - TCM diagnostic responses
#define ID_TCM_DIAG_RX      0x7E1   // 2017 - TCM diagnostic requests/echo

// ---- Consumed BY the TCM (the "simulated vehicle" must provide these) ----
#define ID_PCM_MSG_1        0x097   // 151  - indicated/friction/demand/actual engine torque
#define ID_PCM_MSG_2        0x0FC   // 252  - TCS shift-map desired, VDC/TC flags
#define ID_PCM_MSG_3        0x120   // 288  - indicated/estimated engine torque
#define ID_PCM_MSG_4        0x12D   // 301  - engine speed, pedal, throttle, brake, crank, cruise
#define ID_PCM_MSG_5        0x200   // 512  - actual/min/max available torque
#define ID_PCM_MSG_6        0x207   // 519  - engine speed, vehicle speed, throttle
#define ID_ABS_MSG_1        0x210   // 528  - ABS/VDC/TCS flags + desired torque command
#define ID_PCM_MSG_7        0x230   // 560  - (PCM relays) gear pos, ratio, trans mode/flags
#define ID_TCS_MSG_1        0x4B0   // 1200 - four wheel speeds
#define ID_PCM_MSG_11       0x623   // 1571 - engine config (displacement, cyl count, induction)
#define ID_PCM_MSG_12       0x640   // 1600 - DRIVELINE CONFIG: auto/manual flags, axle ratio (OVERRIDE)
#define ID_PCM_MSG_9        0x44D   // 1101 - coolant/oil temp
#define ID_PCM_MSG_8        0x427   // 1063 - coolant temp, indicators

// =============================================================================
//  Bit-level signal codec
// =============================================================================

// ---- Motorola / big-endian (@0). startBit = DBC start bit (MSB of the signal).
static inline uint64_t fgGetMotorola(const uint8_t *d, uint8_t startBit, uint8_t len) {
  uint64_t val = 0;
  int bit = startBit;
  for (uint8_t i = 0; i < len; i++) {
    int byteIndex = bit >> 3;
    int bitIndex  = bit & 7;
    uint8_t b = (d[byteIndex] >> bitIndex) & 0x01;
    val = (val << 1) | b;
    // Motorola sawtooth: step toward the LSB.
    if (bitIndex == 0) bit += 15;   // wrap to bit 7 of the next byte
    else               bit -= 1;
  }
  return val;
}

static inline void fgSetMotorola(uint8_t *d, uint8_t startBit, uint8_t len, uint64_t val) {
  int bit = startBit;
  for (int i = len - 1; i >= 0; i--) {
    int byteIndex = bit >> 3;
    int bitIndex  = bit & 7;
    uint8_t b = (val >> i) & 0x01;
    if (b) d[byteIndex] |=  (1 << bitIndex);
    else   d[byteIndex] &= ~(1 << bitIndex);
    if (bitIndex == 0) bit += 15;
    else               bit -= 1;
  }
}

// ---- Intel / little-endian (@1). startBit = DBC start bit (LSB of the signal).
static inline uint64_t fgGetIntel(const uint8_t *d, uint8_t startBit, uint8_t len) {
  uint64_t val = 0;
  for (uint8_t i = 0; i < len; i++) {
    int bit = startBit + i;
    uint8_t b = (d[bit >> 3] >> (bit & 7)) & 0x01;
    val |= ((uint64_t)b) << i;
  }
  return val;
}

// Sign-extend an unsigned field of `len` bits into a signed 32-bit value.
static inline int32_t fgSignExtend(uint64_t raw, uint8_t len) {
  if (len < 64 && (raw & (1ULL << (len - 1)))) {
    return (int32_t)(raw | (~0ULL << len));
  }
  return (int32_t)raw;
}

// =============================================================================
//  Decoded state structures
// =============================================================================

// Live vehicle signals harvested from the (listen-only) vehicle bus.  These are
// the inputs the simulated vehicle re-broadcasts to the TCM.
struct VehicleState {
  // engine / driver
  float   engineRpm   = 0;     // 0x12D / 0x207
  float   vehicleKph  = 0;     // 0x207
  float   throttlePct = 0;     // 0x12D / 0x207
  float   pedalPct    = 0;     // 0x12D
  uint8_t brakeState  = 0;     // 0x12D  (0=off,1=pressed)
  uint8_t engCrank    = 0;     // 0x12D
  // torque (Nm)
  int16_t indicatedTq = 0;     // 0x097
  int16_t actualTq    = 0;     // 0x097 / 0x200
  int16_t demandTq    = 0;     // 0x097
  // wheel speeds (km/h)
  float   wsFL = 0, wsFR = 0, wsRL = 0, wsRR = 0;   // 0x4B0
  // temps
  int16_t coolantC = 0;        // 0x44D / 0x427
  // freshness
  uint32_t lastEngineMs = 0;
  uint32_t lastWheelMs  = 0;
};

// Decoded TCM outputs (for the dashboard).  Never leaves the device toward the car.
struct TcmState {
  // 0x0C9 TCM_MSG_1
  float    inputRpm   = 0;
  float    outputRpm  = 0;
  uint8_t  tcSlip     = 0;
  uint16_t torqueLimit = 0;    // Nm (factor 4)
  // 0x3E9 TCM_MSG_2
  uint8_t  gearActual   = 0;
  uint8_t  gearSelected = 0;
  uint8_t  gearTarget   = 0;
  int16_t  oilTempC     = 0;   // offset -40
  uint8_t  shiftMap     = 0;
  bool     transMalfunction = false;
  bool     tcLocked         = false;   // Torque_Converter_Status
  bool     gearSelFault     = false;
  bool     mil              = false;   // TCM_DTC_MIL_Status
  // 0x230 PCM_MSG_7 (if present on TCM bus)
  bool     shiftInProgress = false;
  bool     transOverheat   = false;
  float    gearRatio       = 0;
  uint8_t  transGearPos    = 0;
  // freshness
  uint32_t lastMs1 = 0;
  uint32_t lastMs2 = 0;
  bool seen() const { return lastMs1 || lastMs2; }
};

// =============================================================================
//  Value -> text helpers (from the DBC VAL_ tables) for the dashboard
// =============================================================================
static inline const char *fgGearActualName(uint8_t v) {
  switch (v) {
    case 0:  return "Neutral";  case 1: return "First";  case 2: return "Second";
    case 3:  return "Third";    case 4: return "Fourth"; case 5: return "Fifth";
    case 6:  return "Sixth";    case 12: return "Reverse";
    case 15: return "Shifting"; default: return "?";
  }
}
static inline const char *fgGearTargetName(uint8_t v) {
  switch (v) {
    case 0:  return "Neutral";  case 1: return "First";  case 2: return "Second";
    case 3:  return "Third";    case 4: return "Fourth"; case 5: return "Fifth";
    case 6:  return "Sixth";    case 12: return "Reverse";
    case 15: return "Invalid";  default: return "?";
  }
}
static inline const char *fgGearSelectedName(uint8_t v) {
  switch (v) {
    case 0:  return "Park";    case 1: return "Reverse"; case 2: return "Neutral";
    case 3:  return "Drive";   case 4: return "Fourth";  case 5: return "Third";
    case 6:  return "Second";  case 7: return "Fifth";   case 15: return "Intermediate";
    default: return "?";
  }
}
static inline const char *fgShiftMapName(uint8_t v) {
  switch (v) {
    case 0: return "Normal"; case 1: return "Sports";  case 2: return "NotUsed";
    case 3: return "Hot";    case 4: return "Gradient";case 5: return "Traction";
    case 6: return "Manual"; case 7: return "Cruise";  default: return "?";
  }
}

// Human label for any known ID (for the live sniffer).  Returns "" if unknown.
static inline const char *fgIdName(uint32_t id) {
  switch (id) {
    case ID_TCM_MSG_1:  return "TCM_MSG_1";   case ID_TCM_MSG_2:  return "TCM_MSG_2";
    case ID_TCM_DIAG_TX:return "TCM_Diag_Tx"; case ID_TCM_DIAG_RX:return "TCM_Diag_Rx";
    case ID_PCM_MSG_1:  return "PCM_MSG_1";   case ID_PCM_MSG_2:  return "PCM_MSG_2";
    case ID_PCM_MSG_3:  return "PCM_MSG_3";   case ID_PCM_MSG_4:  return "PCM_MSG_4";
    case ID_PCM_MSG_5:  return "PCM_MSG_5";   case ID_PCM_MSG_6:  return "PCM_MSG_6";
    case ID_ABS_MSG_1:  return "ABS_MSG_1";   case ID_PCM_MSG_7:  return "PCM_MSG_7";
    case ID_TCS_MSG_1:  return "TCS_MSG_1";   case ID_PCM_MSG_11: return "PCM_MSG_11";
    case ID_PCM_MSG_12: return "PCM_MSG_12";  case ID_PCM_MSG_9:  return "PCM_MSG_9";
    case ID_PCM_MSG_8:  return "PCM_MSG_8";   default: return "";
  }
}

// =============================================================================
//  Message decoders  (update the shared state structs in-place)
// =============================================================================
void fgDecodeVehicleFrame(const CanFrame &f, VehicleState &vs);
void fgDecodeTcmFrame(const CanFrame &f, TcmState &ts);

// 0x640 driveline-config override: rewrite `data` in place so the TCM sees an
// automatic, TCM-controlled driveline.  `seed` true => start from a sane default
// frame (used when the vehicle never sends 0x640); false => edit the live frame.
void fgBuildAutoConfig(uint8_t *data, const MitmConfig &cfg, bool seed);
