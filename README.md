# Rohnson R-8152 ceiling fan + light — ESPHome / Tuya-MCU bridge

This is an [ESPHome](https://esphome.io/) configuration that replaces the
stock WiFi module of a **Rohnson R-8152** ceiling fan (with integrated CCT
light) with a generic **ESP32-C3 Super Mini**, while still talking to the
fan's original Tuya-protocol MCU over UART. The result is full local control
from Home Assistant — fan on/off, 6-speed, direction, light on/off,
brightness, and 3-position color temperature — exposed as clean, native HA
entities, with no cloud dependency.

## Hardware

The Rohnson R-8152's main control board has an **unpopulated footprint**
clearly intended for an OEM Tuya WiFi module — 5 pads (TX, RX, two GND, and
one more) laid out in the usual Tuya module pattern — that the manufacturer
left off this SKU. An ESP32-C3 Super Mini was wired directly into that
footprint instead of the missing Tuya module.

**The 3.3V rail on that footprint is dead** — there's no usable power pin
among the 5 pads (two of the five are both GND, not a spare 3.3V; whatever
pin was meant to supply 3.3V to the OEM module isn't actually powered on
this board revision). So the ESP32-C3 is **powered externally** (its own USB
input / a separate 3.3V/5V supply) rather than from the fan board, and only
TX/RX/GND are actually shared with the Tuya MCU footprint.

Board identification, for anyone trying to match their own unit:
- Fan model: **Rohnson R-8152**
- Driver/control board markings: `12606AB` / date code `01-2026`, board
  rework code `52-13213`
- LED driver IC: On-Bright `OB38R08A1W16OP`
- Main transformer: `PQ2516`
- Transformer/PCB safety markings: `94V-0 C UL US`, `E482054`, `YH-11`,
  `RoHS`, `2550` — useful to cross-check if you're trying to confirm you've
  got the same driver board on a different fan model.

### Photos

| | |
|---|---|
| ![Enclosure back label](hardware/enclosure-back-label.jpg) Enclosure back label: `12606AB` / `01-2026` | ![Enclosure side label](hardware/enclosure-side-label.jpg) Enclosure side label: `52-13213` |
| ![Driver board overview](hardware/driver-board-overview.jpg) Full driver board, top side | ![Driver board power section](hardware/driver-board-power-section.jpg) Power section: bridge rectifier, main filter cap, inductors |
| ![Driver board at angle](hardware/driver-board-angle.jpg) Board angle showing transformer and heatsinks | ![Driver board MCU area](hardware/driver-board-mcu-area.jpg) MCU/control area — buzzer, crystal `6.7458`, yellow UART wire to the ESP32 |
| ![Driver board MCU area magnified](hardware/driver-board-mcu-area-magnified.jpg) Same area, magnified | ![LED driver IC closeup](hardware/led-driver-ic-closeup.jpg) LED driver IC closeup: On-Bright `OB38R08A1W16OP` |

Wiring used in this build (adjust to match your board's silkscreen/pad
labels — don't assume yours match without checking, and confirm with a
multimeter whether your board's "3.3V" pad is actually live before relying
on it):

| ESP32-C3 Super Mini | Fan board               |
|----------------------|--------------------------|
| GPIO20 (RX)          | Tuya MCU TX              |
| GPIO21 (TX)          | Tuya MCU RX              |
| GND                  | GND                      |
| (own external supply)| 3.3V pad — dead, unused  |

UART runs at 9600 baud, which is standard for Tuya MCU links.

## What works

- **Fan**: on/off, 6 discrete speeds, direction (forward/reverse) — one
  `fan` entity (`fan.template`), bridged to Tuya datapoints 1 (switch), 101
  (speed, enum 1-6), 8 (direction).
- **Light**: on/off, brightness, **and** 3-position color temperature
  (cold/neutral/warm) — merged into a **single** HA `light` entity with
  `color_temp` support, via the local `tuya_cct_light` external component
  (see below). Bridged to datapoints 15 (switch), 16 (brightness, 10-100),
  103 (color temp, enum 0/1/2).
- **Fan timer**: a `number` entity for the built-in countdown timer
  (datapoint 102).
- Remote-driven changes (the fan's own RF remote) are reflected back into
  Home Assistant in near real time for everything above, *except* one
  specific button — see Known limitations.

## The `tuya_cct_light` component — what it is and why it exists

[`components/tuya_cct_light/`](components/tuya_cct_light/) is a small local
[ESPHome external component](https://esphome.io/components/external_components.html)
written specifically for this fan. **It exists to solve one problem:** the
light physically only has 3 fixed color-temperature settings (cold / neutral
/ warm), driven by a Tuya *enum* datapoint — but the goal was for it to show
up in Home Assistant's default UI as **one normal-looking light entity**,
the same as any RGB/CCT bulb, not as a light plus a separate dropdown.

The "obvious" way to do this — ESPHome's built-in `light: platform: tuya`
(brightness/switch) plus a `select: platform: tuya` (color temp dropdown) —
works, but produces **two separate HA entities** for one physical light, and
the default Lovelace UI for that is clunky: you get a normal light card *and*
an unrelated dropdown card, instead of one light tile with a color-temp
slider built in. It also doesn't compose well with anything that expects a
real `light` entity with `color_temp` support (voice assistants, scenes,
adaptive lighting, etc.).

`tuya_cct_light` implements a custom `light::LightOutput` in C++ that
declares `COLOR_MODE_COLOR_TEMPERATURE` support directly, so the entity HA
sees has **brightness and a color-temperature slider together, on one
entity** — exactly like a normal smart bulb. Internally, since the hardware
only has 3 real positions, it:

- **Outgoing:** takes whatever continuous mireds value you pick on the HA
  slider, maps it to 0.0–1.0 across the configured cold/warm range, rounds
  to the nearest of 3 buckets, and sends that as a properly-typed Tuya
  **enum** datapoint frame (not a "value"/int frame — see the gotcha below
  about why that distinction matters).
- **Incoming:** listens for datapoint updates the same way (via
  `Tuya::register_listener`), so when the physical RF remote changes
  brightness, on/off, or color temp, it converts the raw enum value back to
  a mireds value and pushes it into the *same* merged HA entity — remote and
  app stay in sync without needing a second entity.

If your fan/light only has 2 CCT positions instead of 3, or you want to wire
this onto a different set of datapoint IDs, the component takes
`switch_datapoint` / `dimmer_datapoint` / `cct_datapoint` / `min_value` /
`max_value` / `cold_white_color_temperature` / `warm_white_color_temperature`
as plain YAML config — see its use in `tuya-ceiling-fan.yaml`.

## The debugging journey (a.k.a. the shenanigans)

None of this worked on the first try. In rough chronological order, here's
what actually happened getting from "stock select dropdown that beeps
angrily" to the current setup — useful context if you're reverse-engineering
a different Tuya MCU and hit similar walls:

1. **Symptom:** changing the color-temperature dropdown made the fixture
   emit an angry error beep, and the RF remote's "All Off" button didn't
   turn the light off in Home Assistant.
2. **First instrumentation:** enabled `logger: level: VERY_VERBOSE` with a
   `tuya:` component override, then OTA-flashed and live-tailed the
   ESPHome API log stream while triggering both symptoms — this is the only
   way to see raw UART frames; Home Assistant's own logs show nothing,
   because this traffic never reaches HA, it's purely between the ESP32 and
   the Tuya MCU.
3. **Root cause #1 — the beep:** the original config used enum datapoint
   values `0` (Cool), `64` (Neutral), `128` (Warm) for DP103. The capture
   showed `Cool (0)` ACKing instantly and cleanly, but `Neutral (64)` and
   `Warm (128)` got **zero response** from the MCU — ESPHome retried the SET
   command 5 times, then gave up and logged `Initialization failed at
   init_state 3`. The MCU was treating `64`/`128` as out-of-range enum
   indices and silently refusing them (with an audible beep as the only
   feedback). Fix: use small contiguous indices `0/1/2` instead — confirmed
   by capture to ACK cleanly every time, in both directions, repeatedly.
4. **Root cause #2 — "All Off":** captured the exact RF remote button press
   for both the master "All Off" scene and the plain "Fan Off" button.
   Byte-for-byte **identical** UART traffic came out of the MCU for both
   (`DP1→OFF`, `DP101→0`, `DP102→0`) — yet physically, "All Off" also kills
   the light and plain "Fan Off" doesn't. Confirmed via user observation on
   the actual fixture. Conclusion: the MCU's "All Off" scene cuts the light
   relay through an internal path that never gets reported as a datapoint
   change, and there is no way to distinguish the two button presses from
   the wire protocol alone — see "Known limitations" below.
5. **The merged-entity rewrite:** with the protocol understood, replaced the
   stock `light` + `select` pair with the custom `tuya_cct_light` component
   described above, reusing the now-confirmed-correct `0/1/2` enum values
   and the existing switch/dimmer datapoint wiring.
6. **Root cause #3 — color direction reversed:** after flashing the merged
   entity, the HA color-temp slider's "cool" end visually produced the
   *warm* LEDs and vice versa. The component's mireds→enum mapping had
   (reasonably, but wrongly) assumed raw enum `0` was the cold end and `2`
   was the warm end, copying the direction convention from ESPHome's stock
   `color_temperature_datapoint` handling. This particular MCU runs the
   *opposite* direction. Fixed by inverting the mapping in both directions
   (HA→MCU and MCU→HA) in `tuya_cct_light.cpp`, then re-verified with a full
   slider sweep through all 3 positions in both directions.

The throughline: **never assume a Tuya MCU's datapoint value range or
semantics from the variable names/comments in an existing config.** Capture
real traffic with `VERY_VERBOSE` logging and confirm what the MCU actually
ACKs before trusting any value.

## Known limitations

The remote's **"All Off" master scene button** turns the light off at the
hardware level *without ever reporting it over UART* — confirmed by capturing
identical wire traffic (`DP1→OFF`, `DP101→0`, `DP102→0`, **no DP15** in
either case) for both the master "All Off" button and the plain "Fan Off"
button. The two buttons have indistinguishable UART signatures, yet one
turns the light off physically and the other doesn't. There is no datapoint
to listen for here — this is a firmware limitation of the Tuya MCU itself,
not something fixable in ESPHome config. The plain single-purpose "Light
Off" and "Fan Off" remote buttons both work correctly and are unaffected;
only the combined "All Off" scene is invisible to Home Assistant.

## Setup

1. Install [ESPHome](https://esphome.io/guides/installing_esphome) (a
   Python venv is simplest: `python3 -m venv .venv && source .venv/bin/activate
   && pip install esphome`).
2. Copy `secrets.yaml.example` to `secrets.yaml` and fill in your own WiFi
   credentials, API encryption key, and OTA/AP passwords.
3. Flash for the first time over USB:
   ```
   esphome run tuya-ceiling-fan.yaml
   ```
   (select the USB serial port when prompted). After that, OTA updates work
   from anywhere on the same network:
   ```
   esphome run tuya-ceiling-fan.yaml --device <device-ip-or-.local-name>
   ```
4. Add the device to Home Assistant via the ESPHome integration (it
   auto-discovers via mDNS).

## Debugging

If you're adapting this for a different fan/board and need to see raw Tuya
UART frames (including any error/NAK replies from the MCU), temporarily set:
```yaml
logger:
  level: VERY_VERBOSE
  logs:
    tuya: VERY_VERBOSE
```
then watch live logs with `esphome logs tuya-ceiling-fan.yaml --device
<ip>`. Revert to `level: INFO` afterwards — VERY_VERBOSE has a real
performance cost and can cause API disconnects if left on permanently.

## Repo layout

```
tuya-ceiling-fan.yaml          Main ESPHome config
secrets.yaml.example           Template — copy to secrets.yaml (gitignored)
components/tuya_cct_light/     Local external component: merged light+CCT entity
hardware/                      Photos of the fan's control board
```
