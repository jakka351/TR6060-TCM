// =============================================================================
//  fg_falcon_can.cpp  -  decoders + 0x640 driveline-config override builder
// =============================================================================
#include "fg_falcon_can.h"

// -----------------------------------------------------------------------------
//  Decode vehicle-bus signals the simulated vehicle / dashboard cares about.
//  (Signal bit positions taken directly from the DBC, Motorola @0.)
// -----------------------------------------------------------------------------
void fgDecodeVehicleFrame(const CanFrame &f, VehicleState &vs) {
  const uint8_t *d = f.data;
  switch (f.id) {

    case ID_PCM_MSG_4: {                                  // 0x12D
      vs.engineRpm   = fgGetMotorola(d, 39, 16) * 0.25f;   // Engine_Speed
      vs.pedalPct    = fgGetMotorola(d, 23,  8) * 0.5f;    // Pedal_Position
      vs.throttlePct = fgGetMotorola(d, 15,  8) * 0.5f;    // Throttle_Position
      vs.brakeState  = fgGetMotorola(d, 57,  2);           // Brake_State
      vs.engCrank    = fgGetMotorola(d, 58,  1);           // Eng_Crank
      vs.lastEngineMs = f.ts;
    } break;

    case ID_PCM_MSG_6: {                                  // 0x207
      vs.engineRpm   = fgGetMotorola(d,  7, 16) * 0.25f;       // Engine_Speed
      vs.vehicleKph  = fgGetMotorola(d, 39, 16) * 0.0078125f;  // Vehicle_Speed
      vs.throttlePct = fgGetMotorola(d, 55,  8) * 0.5f;        // Throttle_Position
      vs.lastEngineMs = f.ts;
    } break;

    case ID_PCM_MSG_1: {                                  // 0x097
      vs.indicatedTq = (int16_t)fgGetMotorola(d,  7, 16);  // Indicated_Eng_Torque
      vs.actualTq    = (int16_t)fgGetMotorola(d, 39, 16);  // Actual_Eng_Torque
      vs.demandTq    = (int16_t)fgGetMotorola(d, 55, 16);  // Driver_Demand_Torque
    } break;

    case ID_TCS_MSG_1: {                                  // 0x4B0
      vs.wsFL = fgGetMotorola(d,  7, 16) * 0.01f;
      vs.wsFR = fgGetMotorola(d, 23, 16) * 0.01f;
      vs.wsRL = fgGetMotorola(d, 39, 16) * 0.01f;
      vs.wsRR = fgGetMotorola(d, 55, 16) * 0.01f;
      vs.lastWheelMs = f.ts;
    } break;

    case ID_PCM_MSG_9: {                                  // 0x44D
      vs.coolantC = (int16_t)(fgGetMotorola(d, 39, 12) * 0.1f) - 40;  // Coolant_Temperature
    } break;

    case ID_PCM_MSG_8: {                                  // 0x427
      vs.coolantC = (int16_t)fgGetMotorola(d, 7, 8) - 40;            // Coolant_Temperature
    } break;

    default: break;
  }
}

// -----------------------------------------------------------------------------
//  Decode TCM outputs for the dashboard.
// -----------------------------------------------------------------------------
void fgDecodeTcmFrame(const CanFrame &f, TcmState &ts) {
  const uint8_t *d = f.data;
  switch (f.id) {

    case ID_TCM_MSG_1: {                                  // 0x0C9
      ts.torqueLimit = fgGetMotorola(d, 23,  8) * 4;        // Transmission_Torque_Limit
      ts.tcSlip      = fgGetMotorola(d, 31,  8);            // Torque_Conv_Slip
      ts.inputRpm    = fgGetMotorola(d, 39, 16);            // Transmission_Input_Speed
      ts.outputRpm   = fgGetMotorola(d, 55, 16);            // Transmission_Output_Speed
      ts.lastMs1 = f.ts;
    } break;

    case ID_TCM_MSG_2: {                                  // 0x3E9
      ts.gearSelected      = fgGetMotorola(d,  7, 4);       // Gear_Position_Selected
      ts.gearActual        = fgGetMotorola(d,  3, 4);       // Gear_Position_Actual
      ts.shiftMap          = fgGetMotorola(d, 15, 8);       // Transmission_Shift_Map
      ts.oilTempC          = (int16_t)fgGetMotorola(d, 23, 8) - 40;   // Transmission_Oil_Temperature
      ts.transMalfunction  = fgGetMotorola(d, 24, 1);
      ts.tcLocked          = fgGetMotorola(d, 26, 1);       // Torque_Converter_Status
      ts.gearSelFault      = fgGetMotorola(d, 27, 1);
      ts.mil               = fgGetMotorola(d, 30, 1);       // TCM_DTC_MIL_Status
      ts.gearTarget        = fgGetMotorola(d, 51, 4);       // Gear_Position_Target
      ts.lastMs2 = f.ts;
    } break;

    case ID_PCM_MSG_7: {                                  // 0x230 (gear relay, if present)
      ts.transGearPos     = fgGetMotorola(d,  7, 8);        // Trans_Gear_Pos
      ts.gearRatio        = fgGetMotorola(d, 15, 16) * 6.1035e-5f;  // Gear_Ratio
      ts.transOverheat    = fgGetMotorola(d, 61, 1);
      ts.shiftInProgress  = fgGetMotorola(d, 62, 1);
      ts.tcLocked         = fgGetMotorola(d, 63, 1);        // Torque_Conv_Locked
    } break;

    default: break;
  }
}

// -----------------------------------------------------------------------------
//  0x640 / PCM_MSG_12 driveline-config OVERRIDE.
//
//  This is the keystone of the whole rig: in a manual FG the PCM broadcasts
//  ManualTrans_Flag=1 / AutoTransTCMControl_Flag=0, which keeps the auto TCM
//  disabled.  We flip those bits so the TCM believes it is installed in a
//  TCM-controlled automatic car and brings itself online.
//
//  Signals (Motorola @0):
//    AutoTransTCMControl_Flag : 2|1   (bit 2 of byte0)
//    AutoTransPCMControl_Flag : 3|1   (bit 3 of byte0)
//    ManualTrans_Flag         : 4|1   (bit 4 of byte0)
//    AWD_Flag                 : 5|1
//    RWD_Flag                 : 6|1
//    FWD_Flag                 : 7|1
//    Forward_Gear_Count       : 51|4
//    Transmission_Config      : 54|1
//    Axle_Ratio               : 31|16 (factor 0.00012207)
//    Fraction_Portion_Ratio   : 15|16 (factor 0.00012207)
//    Base_Torque_Split        : 47|8  (factor 0.0078125)
// -----------------------------------------------------------------------------
void fgBuildAutoConfig(uint8_t *data, const MitmConfig &cfg, bool seed) {
  if (seed) {
    // Build a clean, plausible RWD automatic config from scratch.
    memset(data, 0, 8);
    fgSetMotorola(data,  6, 1, 1);                          // RWD_Flag = 1
    fgSetMotorola(data, 15, 16, (uint64_t)(1.0f / 0.00012207f));   // Fraction_Portion_Ratio ~1.0
    fgSetMotorola(data, 47,  8, (uint64_t)(1.0f / 0.0078125f));    // Base_Torque_Split ~1.0
  }

  // Force the automatic / TCM-controlled driveline regardless of what the car says.
  fgSetMotorola(data, 2, 1, 1);   // AutoTransTCMControl_Flag = True
  fgSetMotorola(data, 3, 1, 0);   // AutoTransPCMControl_Flag = False
  fgSetMotorola(data, 4, 1, 0);   // ManualTrans_Flag         = False

  // Apply configurable driveline parameters.
  fgSetMotorola(data, 51, 4, cfg.forwardGearCount & 0x0F);            // Forward_Gear_Count
  fgSetMotorola(data, 54, 1, cfg.transConfig ? 1 : 0);               // Transmission_Configuration
  fgSetMotorola(data, 31, 16, (uint64_t)(cfg.axleRatio / 0.00012207f)); // Axle_Ratio
}
