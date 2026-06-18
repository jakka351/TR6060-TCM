# FG Falcon TCM Man-In-The-Middle Gateway

Firmware for the **Autosport Labs ESP32-CAN-X2** (ESP32-S3) that lets you run an
**automatic transmission control module (TCM)** against an FG Falcon that is
actually fitted with a **manual** gearbox — without ever putting TCM traffic
onto the real car's CAN bus.

The device is a one-way gateway:

```
                    ┌─────────────────────────── ESP32-CAN-X2 ───────────────────────────┐
   REAL CAR         │                                                                     │        AUTO TCM
  HS-CAN bus  ──────┤ CAN1 / internal TWAI            gate            CAN2 / MCP2515       ├────── + simulated
 (many ECUs)        │  LISTEN-ONLY (silent)   ──►  forward / rewrite  ──►  ACTIVE (we TX)  │        vehicle bus
                    │  passive sniff only          VEHICLE ➜ TCM only                      │
                    └─────────────────────────────────────────────────────────────────────┘
                                         ▲                                   │
                                         └────────  TCM outputs decoded,  ◄──┘
                                                    NEVER sent to the car
```

* **Vehicle bus → CAN1 (internal TWAI), hardware `LISTEN_ONLY`.** The controller
  physically cannot emit a dominant bit — no data frames, no ACK, no error
  frames. The firmware also contains **no transmit function for this bus at all.**
* **TCM bus → CAN2 (MCP2515), active.** We act as the "vehicle": forward the
  subset of real messages the TCM needs, rewrite the driveline-config message
  `0x640` so the auto TCM enables itself, and (optionally) synthesise an idle
  vehicle for bench testing.
* Data flows **vehicle → TCM** by default, with one optional, tightly-scoped
  reverse path: a **diagnostic bridge** that relays the TCM's `0x7E9` response
  back to an OBD tester (see §4a). It is **on by default**.

A WiFi web dashboard provides a live CAN sniffer, decoded TCM telemetry,
editable forwarding rules, a TCM-bus frame injector, and all configuration.

---

## 1. Hardware & wiring

| Signal | ESP32-CAN-X2 | Connects to |
|--------|--------------|-------------|
| CAN1 H / L (internal TWAI) | `CAN1` header | **Vehicle** HS-CAN (tap, listen-only) |
| CAN2 H / L (MCP2515)       | `CAN2` header | **TCM** + simulated-vehicle harness |
| 12 V / GND                 | JST-PH power  | Switched/ignition 12 V & ground |

Pin map used by the firmware (from the ASL wiki; see `config.h` to change):

```
CAN1 (TWAI, vehicle):  TX=GPIO7  RX=GPIO6
CAN2 (MCP2515, TCM):   CS=GPIO10 MOSI=GPIO11 SCK=GPIO12 MISO=GPIO13 INT=GPIO3  (16 MHz xtal)
Status LED:            GPIO2
```

**Termination:** the TCM segment is a 2-node bus (gateway + TCM), so it needs
**120 Ω at each end** (≈60 Ω measured across H–L). The vehicle side is an
existing terminated bus — just tap it, do **not** add termination there.

> ⚠️ Keep the two CAN segments electrically separate. The whole point of the
> rig is that they are only bridged in software, one way.

---

## 2. Toolchain & libraries (Arduino IDE)

> The firmware was written against the libraries below but **could not be
> compiled on the dev machine** — the ESP32 core and the web/JSON libraries are
> not installed there yet. Install these three, then Verify in the IDE.

1. **ESP32 Arduino core 3.x** — Boards Manager → "esp32 by Espressif" (≥ 3.0.0).
   Provides WiFi, `Preferences`, and `driver/twai.h`. *(Not yet installed — the
   board index is downloaded but the core itself is missing.)*
2. **ESPAsyncWebServer** and **AsyncTCP** — use the maintained **ESP32Async**
   forks (Library Manager → *"ESPAsyncWebServer"* by ESP32Async, which pulls in
   *"Async TCP"* by ESP32Async). The old me-no-dev versions do not build on
   core 3.x. *(Not yet installed.)*
3. **ArduinoJson 7.x** — Library Manager → *"ArduinoJson"* by Benoit Blanchon.
   *(Not yet installed.)*

**No CAN library is required.** The MCP2515 (TCM bus) is driven by this project's
own `mcp_jakka.cpp/.h` (class `McpJakka`); the vehicle bus uses the ESP32's built-in TWAI
driver. We deliberately dropped the Longan/Seeed `mcp_can` library — its RX path
overruns the caller's buffer on a malformed DLC, its `begin()` re-inits SPI with
the wrong pins for this board, and its `send()` blocks until each frame is on the
wire. Our driver clamps every RX DLC to 8, leaves SPI pin setup to us, and sends
fire-and-forget into the MCP2515's three TX buffers.

> **Remove the stale CAN libraries** so they can't shadow anything: move
> `mcp_can`, `Longan_CAN_MCP2515-master`, and
> `Longan_Labs_Arduino_CAN_Bus_Library_for_MCP2515` out of
> `Documents\Arduino\libraries\`. None are used now. (`ESP32-TWAI-CAN` is also
> unused but harmless.)

### Board settings

* **Board:** *ESP32S3 Dev Module* (or "Autosport Labs ESP32-CAN-X2" if installed)
* **Flash size:** 8 MB
* **Partition scheme:** any with ≥ 2 MB app, e.g. *"8M Flash (3MB APP/1.5MB FATFS)"*
* **PSRAM:** OPI PSRAM (board is N8R8)
* **USB CDC On Boot:** Enabled (for the serial log)

Put the whole `FG_TCM_MITM/` folder in your sketchbook and open
`FG_TCM_MITM.ino`. All `.h`/`.cpp` files compile together.

---

## 3. First run

1. Flash the board, open Serial Monitor @ 115200. You should see
   `CAN: vehicle bus = LISTEN-ONLY, TCM bus = active`.
2. Join the WiFi AP **`FG-TCM-MITM`** (password **`falcon500`**) and browse to
   **http://192.168.4.1**. (Switch to "join your network" mode in *Settings*.)
3. The **Dashboard** shows decoded TCM gear/temperature/speed telemetry once the
   TCM starts transmitting `0x0C9`/`0x3E9`. **Live Sniffer** shows every ID on
   both buses with period & count, and whether each vehicle frame was forwarded.

---

## 4. How the gating works

Each **vehicle** frame is matched against the **forwarding rules** (editable in
the *Forwarding* tab):

| Action | Effect |
|--------|--------|
| `PASS` | forward the raw frame to the TCM bus unchanged |
| `BLOCK` | drop it (default for any ID with no rule) |
| `OVERRIDE` | rewrite selected signals, then forward (used for `0x640`) |
| `GENERATE` | don't forward; the sim engine synthesises this ID locally |

Default rules forward the engine/torque/wheel/brake messages the ZF6HP-style TCM
consumes (`0x097 0x0FC 0x120 0x12D 0x200 0x207 0x210 0x4B0 0x623 …`) and
**`OVERRIDE 0x640`** — the driveline-config message. In a manual FG the PCM sends
`ManualTrans_Flag = 1`; the override flips it to
`AutoTransTCMControl_Flag = 1, ManualTrans_Flag = 0` so the auto TCM believes it
is installed in a TCM-controlled automatic car and brings itself online. Gear
count, axle ratio and transmission config are set from *Settings*.

TCM-origin IDs (`0x0C9 0x3E9 0x230 0x7E9`) are only ever decoded. They are never
forwarding candidates — direction is hard-coded vehicle → TCM.

### Bench mode (no car attached)

Enable **"Bench idle simulation"** in *Settings*. When no live engine data is
seen, the gateway synthesises a minimal idle vehicle (`0x12D 0x207 0x097 0x4B0`
at ~50 Hz) plus the `0x640` auto-config, so the TCM powers up on the bench.

---

## 4a. Diagnostic bridge (OBD tester ↔ TCM)

A scan tool plugged into the car's OBD port talks to the TCM over ISO-TP on the
standard diagnostic ID pair:

```
   tester request   0x7E1  (vehicle bus)  ──forward──►  TCM bus      [veh → TCM]
   TCM response     0x7E9  (TCM bus)       ──relay────►  vehicle bus  [TCM → veh]
```

Enable **"Diagnostic Bridge"** in *Settings* (default IDs `7E1`/`7E9`, optional
functional request `7DF`). The two IDs are bridged **transparently in both
directions, byte-for-byte**, so multi-frame ISO-TP exchanges (First Frame →
Flow Control → Consecutive Frames) just work — the tester and TCM run the
ISO-TP state machine; the gateway only relays frames with sub-millisecond
latency. No ISO-TP/UDS stack is implemented or needed.

> **ON by default.** To put `0x7E9` back onto the vehicle bus, that controller
> runs in TWAI **NORMAL** mode (so it also ACKs vehicle traffic). The **only**
> frame ID ever transmitted on the car is the response ID (`0x7E9`). The
> dashboard banner reads *"VEHICLE BUS — NORMAL (diag bridge: TX 0x7E9 only)"*
> so the bus posture is always visible. To run the vehicle bus fully silent
> instead, untick the Diagnostic Bridge in Settings and restart.

When the bridge is on but no tester is connected, there is no `0x7E9` traffic,
so nothing is transmitted on the car (apart from CAN ACKs inherent to NORMAL
mode). Requests/responses and their relay counts appear live in *Dashboard* →
*Gateway* and in the *Live Sniffer* (`→TCM` / `→VEH` tags).

---

## 5. Web API (for scripting)

| Method | Path | Body / effect |
|--------|------|---------------|
| GET  | `/api/config`  | current configuration + rules (JSON) |
| POST | `/api/config`  | apply + persist configuration (JSON) |
| POST | `/api/defaults`| restore factory forwarding rules |
| POST | `/api/inject`  | `{"id":1600,"len":8,"data":[..]}` — **TCM bus only** |
| POST | `/api/restart` | reboot |
| WS   | `/ws`          | live telemetry + sniffer (pushed at 10 Hz) |

Configuration is stored in NVS (`Preferences`), so it survives reboots.

---

## 6. Safety model (read this)

There are two mutually-exclusive vehicle-bus postures, chosen at boot from a
persisted flag and always shown on the dashboard banner:

**Default — Diagnostic Bridge ON (banner amber, "NORMAL (diag bridge: TX 0x7E9 only)"):**
* The vehicle bus runs in **`TWAI_MODE_NORMAL`** so the TCM's diagnostic
  response is relayed back to the tester; the node therefore also ACKs vehicle
  traffic.
* The data plane back to the car is still a **tight whitelist of one ID**: the
  firmware only ever calls `vehSend()` for `diagRespId` (`0x7E9`). No other TCM
  frame, simulated frame, or injected frame is transmitted on the car.

**Diagnostic Bridge OFF (banner green, "LISTEN-ONLY / SILENT"):**
* The vehicle bus runs on the **internal TWAI in `TWAI_MODE_LISTEN_ONLY`** — a
  hardware guarantee of silence (no frames, no ACK, no error frames) — **and**
  `vehSend()` hard-refuses while listen-only. Nothing can reach the car. Untick
  the bridge in Settings and restart to use this mode.

General:
* This is bench/development tooling for an off-road / closed-course project.

---

## 7. Extending

* **More TCM inputs:** add a `PASS`/`OVERRIDE` rule for the ID, and (if you need
  to reshape it) extend `fgDecode*` / `fgBuildAutoConfig` in `fg_falcon_can.cpp`.
* **Richer bench simulator:** flesh out the encoders in `mitm_engine.cpp`
  (`encEngineRpmPedal`, `encRpmVss`, …) to drive throttle/speed sweeps.
* **Respond to TCM requests:** add handling in `mitmOnTcmFrame()` (e.g. answer
  `0x7E1` diagnostic requests with crafted `0x7E9` responses on the TCM bus).

Signal definitions come from `FG_Falcon_HighSpeed_CAN.dbc`; all signals there are
Motorola/big-endian and are decoded by `fgGetMotorola()` / `fgSetMotorola()`.
