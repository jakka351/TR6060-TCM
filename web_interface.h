// =============================================================================
//  web_interface.h  -  WiFi + async web dashboard + WebSocket telemetry + NVS
// =============================================================================
#pragma once
#include <Arduino.h>

void webInit();   // bring up WiFi + HTTP/WS server (call from setup, after canInit)
void webLoop();   // apply pending config + broadcast live telemetry (call from loop)

// Config persistence (NVS / Preferences)
void cfgLoad();   // load saved config, or seed defaults on first boot
void cfgSave();   // persist current g_cfg
