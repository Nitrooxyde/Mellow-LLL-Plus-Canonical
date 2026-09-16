French original: COMMANDES_LLL_MANUEL.fr.md

# LLL Plus — Command Manual (canonical firmware v1)

> **The complete guide to every command.** For each command: what it does, its parameters, an example, and the pitfalls to know about.
> All commands are typed in the Mainsail/Fluidd console (or used in macros). All of them require `LANE=`: the name of the target board (`lll` or `lll2`) — except `LLL_DETECT_LOADED`, which is a global command.

---

## Before you start — the 4 concepts

**🛤️ The lane** — each LLL Plus board = one filament "lane". Each lane is independent: its settings, its state, its errors. `LANE=lll` refers to the first one, `LANE=lll2` to the second.

**🎭 The role** — what the lane is currently allowed to do:
| Role | Behavior |
|---|---|
| `active` | Full buffering: feeds and rewinds on its own based on arm tension (this is the lane that is printing) |
| `parked` | Waiting: **never feeds** filament (so it doesn't clog the Y), but **follows along during a rewind** if the filament is pulled (unloading is possible) |
| `standby` | Completely frozen: the motor never moves for any automatic reason |

**🎚️ The mode** — the feed automation level (for the `active` role):
| Mode | Behavior |
|---|---|
| `auto` | Historical behavior: the board handles everything, continuously |
| `semi_auto` | The board is only allowed ONE reload cycle per `LLL_ARM` — you approve each reload |
| `semi_manual` | No automatic start; a commanded feed (`LLL_RUN`) stops on its own at the equilibrium position |
| `manual` | Nothing automatic — your commands only |

**🧩 The functions** — the firmware is modular: 11 functions that can be toggled on/off at runtime (`LLL_ENABLE`). By default the essentials are on; the "bench" functions (LED, ADC, DIAG…) are off.

---

## 📊 Checking state

### `LLL_QUERY LANE=<lane>`
**Queries the board and prints its full state.** Also refreshes the state visible to macros (`printer["lll_buffer <lane>"]`).
```
LLL_QUERY LANE=lll
→ LLL lll: role=active mode=auto error=0 ecode=none feeding=0 running=0 arm=0 present=1 diag=0 cfgerr=0
```
| Field | Meaning |
|---|---|
| `role` / `mode` | current role and mode (see concepts) |
| `error` | 1 = an error is latched, the motor is stopped |
| `ecode` | the CAUSE: `none` / `feed_timeout` (feeding >60 s = probable jam) / `back_timeout` (abnormally long rewind) / `diag_fault` (driver fault) / `host_alive` (lost contact with the host) / `clear_pending` (clear requested, waiting for the driver fault to drop) |
| `feeding` | 1 = a forward feed is in progress (automatic or a commanded `LLL_RUN` fwd) |
| `running` | 1 = the motor is actually moving (any cause) |
| `arm` | arm position, one bit per sensor: 4=FEED zone, 2=EQUILIBRIUM zone, 1=REWIND zone, 0=in between |
| `present` | 1 = filament detected at the board's inlet |
| `diag` | 1 = driver DIAG line active (if the diag function is enabled) |
| `cfgerr` | 1 = a config command has been rejected since the last error clear (check the console) |

---

## 🎭 Roles

### `LLL_ROLE LANE=<lane> ROLE=active|parked|standby`
**Changes the lane's role.** Persistent (survives restarts).
```
LLL_ROLE LANE=lll2 ROLE=parked
```
💡 In normal multicolor use, don't call it directly: `SET_ACTIVE_LANE` handles both lanes' roles at once (macro provided in `config/lll_macros.cfg.example`).

### `LLL_SET_IDLE LANE=<lane> VALUE=0|1`
**Compatibility alias** (used by the existing macros): `VALUE=1` → parked, `VALUE=0` → active. Same effect as `LLL_ROLE`.

---

## 🎚️ Modes and functions

### `LLL_MODE LANE=<lane> LEVEL=auto|semi_auto|semi_manual|manual`
**Sets the feed automation level.** Persistent.
```
LLL_MODE LANE=lll LEVEL=semi_auto
```
⚠️ Changing the mode immediately stops any autonomous feed in progress (a host-commanded `LLL_JOG`/`LLL_RUN` keeps going); the mode changes what the board is allowed to **start**.

### `LLL_ARM LANE=<lane>`
**In `semi_auto` mode only: authorizes ONE reload cycle.** The board will feed at the arm's next request, then go back to waiting once equilibrium is reached. Rejected (explicit error) in any other mode.

### `LLL_ENABLE LANE=<lane> FUNC=<function> EN=0|1`
**Turns a firmware function on or off, at runtime.** Persistent.
| FUNC | Purpose | Default |
|---|---|---|
| `feed` | the automatic buffering itself | ✅ ON |
| `buttons` | the board's 2 physical buttons | ✅ ON |
| `entrance` | the "no filament at the inlet → stop" guard | ✅ ON |
| `tmc` | runtime tuning of the motor driver | ✅ ON |
| `move` | the motion commands (`LLL_JOG`/`LLL_RUN`) | ✅ ON |
| `telem` | the events pushed to Klipper | ✅ ON |
| `mbsig` | the motherboard signal inputs (PB5/PB6, legacy wired usage) | OFF |
| `diag` | reading the driver fault line (PB0) | OFF (to be validated on the bench) |
| `led` | the board's status/error LEDs | OFF (to be validated on the bench) |
| `adc` | supply voltage + board temperature measurement | OFF (to be validated on the bench) |
| `ioext` | the generic I/O registry (see IOEXT section) | OFF |
```
LLL_ENABLE LANE=lll FUNC=led EN=1
```
⚠️ Disabling a function cleanly stops whatever it was driving (a button held mid-motion → motor stopped, etc.). A rejection (pin conflict with IOEXT) is reported as a clear error.

---

## ⚙️ Settings

### `LLL_SET LANE=<lane> PARAM=<name> VALUE=<integer>`
**Sets a parameter, applied immediately and persistent.** The console confirms the value actually applied (clamped if needed).
| PARAM | Effect | Range | Default |
|---|---|---|---|
| `feed_timeout` | max continuous feed before jam error (ms) | 1000-60000 | 60000 |
| `back_timeout` | max continuous rewind before error (ms) | 1000-60000 | 60000 |
| `speed` | motor speed (VACTUAL driver units; 77575 = stock speed) | 0-8388607 | 77575 |
| `irun` | motor run current (0-15; **15 = maximum allowed**, stock value) | 0-15 | 15 |
| `ihold` | standstill current | 0-15 | 7 |
| `iholddelay` | delay before switching to the standstill current | 0-15 | 1 |
| `mres` | microstepping (0=256 … 8=full step; ⚠️ the speed is not recalculated automatically) | 0-8 | 2 (=64) |
| `stealth` | 1 = silent stealthChop mode, 0 = spreadCycle (default, more torque) | 0/1 | 0 |
| `standstill` | motor behavior at standstill: 0=normal, 1=freewheel, 2/3=braking | 0-3 | 0 |
| `tpowerdown` | delay before reducing the current at standstill | 0-255 | 20 |
| `run_max` | max duration of an `LLL_RUN` (ms) | 1000-60000 | 30000 |
| `host_alive` | host-loss watchdog: 0=off, otherwise delay in ms (cuts commanded movements if Klipper stops talking; autonomy continues) | 0 or 3000-60000 | 0 |
| `gate_by_role` | 1 = buttons inactive when the lane is not active | 0/1 | 0 |
| `inform` | "button pressed" outputs to PA2/PA3 (original Mellow behavior) | 0/1 | 1 |
| `diag_action` | on driver fault: 0=report only, 1=stop and latch | 0/1 | 0 |
| `diag_polarity` | electrical polarity of the DIAG line (to be determined on the bench) | 0/1 | 0 |
| `event_mask` | filter for the pushed events (127 = all) | 0-127 | 15 |
```
LLL_SET LANE=lll PARAM=speed VALUE=60000
→ LLL lll: speed=60000 (applied=60000)
```

---

## 🕹️ Manual movement

### `LLL_JOG LANE=<lane> DIR=fwd|back|stop MS=<duration>`
**Small one-off movement, capped at 10 s max.** Takes priority over everything except the buttons. `DIR=stop` stops immediately.
```
LLL_JOG LANE=lll DIR=back MS=2000     ; rewinds for 2 seconds
```

### `LLL_RUN LANE=<lane> DIR=fwd|back|stop MS=<duration>`
**Bounded long movement** — the tool for automation macros. `MS=0` = uses `run_max` (30 s default). Re-sending the command extends the window. Built-in safeties: moving forward requires filament present and zero errors (re-checked continuously); moving back is capped by `back_timeout`.
```
LLL_RUN LANE=lll2 DIR=fwd MS=0        ; feeds (max 30 s, or until LLL_STOP)
LLL_RUN LANE=lll2 DIR=stop            ; cancels
```
💡 Automation pattern: start a RUN, let **a sensor** stop it (`insert_gcode` → `LLL_STOP`). If the sensor never fires, the RUN expires on its own — never a runaway motor.

### `LLL_STOP LANE=<lane>`
**Immediately stops any commanded movement** (jog + run + armed cycle). ⚠️ This is not a master switch: the role's autonomy resumes on the next cycle (an `active` lane in `auto` mode may start buffering again). To freeze a lane: `LLL_ROLE ROLE=standby` or `LLL_MODE LEVEL=manual`.

---

## 📤 Automatic unload/load — not shipped

A complete automatic unload/load pipeline (synchronized extruder+buffer retract, post-splitter detector, calibrated tail, closed-loop anti-tangle compensation) was prototyped and worked on the reference setup. It is **not shipped**: without an automatic cutter or passive respoolers, these procedures proved too fragile to generalize. See `docs/MULTICOLOR_STATUS.en.md` for the architecture, the lessons learned, and what suitably equipped setups can build with the shipped commands.

---

## 🚨 Errors

### `LLL_CLEAR_ERROR LANE=<lane>`
**Unlatches an error** (jam resolved, etc.) and resets `ecode`/`cfgerr` to zero. If the driver fault (DIAG) is still physically present, the clear stays "pending" (`ecode=clear_pending`) and completes on its own once the fault drops.
💡 A short press on one of the board's buttons requests the same clear (without touching `cfgerr`).

---

## 🗂️ Named profiles (Beacon-style)

A **profile** = a snapshot of your settings (mode + parameters + functions — not the role), under a name you choose.

### `LLL_PROFILE_SAVE LANE=<lane> NAME=<name>`
Freezes the current settings under this name.
### `LLL_PROFILE_LOAD LANE=<lane> NAME=<name>`
Applies the profile and makes it persistent. Anything the profile does not define is left unchanged. Partial rejections are reported.
### `LLL_PROFILE_LIST LANE=<lane>` / `LLL_PROFILE_DELETE LANE=<lane> NAME=<name>`
Lists (with contents) / deletes.
```
LLL_SET LANE=lll PARAM=speed VALUE=50000
LLL_MODE LANE=lll LEVEL=semi_auto
LLL_PROFILE_SAVE LANE=lll NAME=nuit_silencieux
...
LLL_PROFILE_LOAD LANE=lll NAME=nuit_silencieux   ; switch in 1 command
```

---

## 🔌 IOEXT — generic I/O (advanced users)

The LLL exposes 12 I/O channels on its expansion pins: input, output, slow PWM, analog measurement. **Safe by construction**: the vital pins (USB, motor, sensors) are not in the table — impossible to touch them; pins not verified on the bench require an explicit unlock.

Enable it first: `LLL_ENABLE LANE=<lane> FUNC=ioext EN=1`

| CH | Pin | Analog | Lock |
|---|---|---|---|
| 0 / 1 | PA4 / PA5 (IO connector) | ✅ | no |
| 2 / 3 | PB10 / PB11 (IO connector) | — | no |
| 4 / 5 | PA2 / PA3 (EXT) | ✅ | 🔒 + release `inform` |
| 6 / 7 | PB5 / PB6 | — | `mbsig` function off |
| 8 / 9 | PA8 / PA15 (LED) | — | 🔒 + `led` function off |
| 10 / 11 | PA0 / PA1 (VIN / board temp) | ✅ | 🔒 + `adc` function off |

### `LLL_UNLOCK LANE=<lane> KEY=0xDEB1`
Unlocks the 🔒 channels **for the session** (deliberately not persistent: using an unvalidated pin should remain a conscious act).

### `LLL_IO_CFG LANE=<lane> CH=<0-11> FUNC=din|dout|pwm|adc|release [options]`
Configures a channel: `din` + `PULL=none|up|down` (input) · `dout` + `INIT=` `SHUTDOWN=` `MAXON=<s>` (output, with a fallback state on emergency shutdown and an optional safety timer) · `pwm` + `PERIOD=<ms>` (software PWM — LEDs/relays, no servos) · `adc` (measurement) · `release` (frees the channel).

### `LLL_IO_WRITE LANE=<lane> CH=<n> VALUE=<0/1 or 0-255>`
Writes an output (0/1) or the PWM duty cycle (0-255).

### `LLL_IO_READ LANE=<lane> CH=<n>`
Reads the channel: level (input), raw value 0-4095 (analog), setpoint (output/PWM).
```
LLL_ENABLE LANE=lll FUNC=ioext EN=1
LLL_IO_CFG LANE=lll CH=0 FUNC=din PULL=up
LLL_IO_READ LANE=lll CH=0
→ LLL lll: ch0 = 1
```

---

## 🔧 printer.cfg configuration

```ini
[lll_buffer lll]
mcu: LLL_PLUS               # required: the name of the board's [mcu]
role: parked                # RECOMMENDED for multi-lane: each board starts
                            # parked (no feeding during boot); the _LLL_INIT macro
                            # (provided in config/lll_macros.cfg.example)
                            # activates the right lane after ready
#feed_mode: auto            # mode at startup
#enable: led, diag          # functions enabled from startup
#disable: buttons           # functions disabled from startup
#poll_interval: 1.5         # state refresh rate (0.3-30 s)
#event_gcode:               # gcode triggered on EVERY event from the board
#  {% if event.name == 'ecode_set' and event.value == 1 %}
#    M118 Jam on {event.lane} !
#    PAUSE
#  {% endif %}
```
`event_gcode` receives `event.name` (`ecode_set`, `error_cleared`, `present`, `diag`, `button`, `arm`, `role`), `event.code`, `event.value`, `event.lane` — reaction in ~20-50 ms. The `button/arm/role` events require `LLL_SET PARAM=event_mask VALUE=127`.

**For macros**: the whole state lives in `printer["lll_buffer <lane>"]` (same fields as LLL_QUERY + a compatible `idle` + `last_event`). An `LLL_QUERY` forces an immediate refresh.

---

## 🔎 Detecting the loaded lane (FILAMENT CHECK)

### `LLL_FILAMENT LANE=<lane> TYPE=<txt> COLOR=<txt> [TEMP=<°C>]`
**Declares which filament is mounted on this lane** (persistent). Shown by the probe and in `get_status.filament`.
```
LLL_FILAMENT LANE=lll2 TYPE=ASA COLOR=rouge TEMP=265
```

### `LLL_DETECT_LOADED [MM=10] [EXPECTED=<lane>] [ON_MISMATCH=warn|pause|error]`
**Detects which lane is PHYSICALLY engaged in the extruder** — with no additional sensor: all lanes briefly switch to standby, small extruder retract (`MM`, default 10 mm), **only the engaged lane's arm moves** (its position sensors give it away), clean net-zero re-push, roles restored.
- Since v1.0.3 the verdict comes from the **arm events** (`EV_ARM`) the firmware reports **while** the extruder retracts — the probe temporarily adds the arm bit to each lane's telemetry mask and counts the transitions per lane, so a sensor crossed and released during the move still counts. The before/after arm-state comparison remains as a fallback.
- ⚠️ **Requires a hot extruder** (≥ `min_extrude_temp`, 170 °C): when cold, Klipper blocks the E movement and the filament is frozen in the nozzle (guaranteed slip) → the command refuses with a clear message.
- `EXPECTED=` + `ON_MISMATCH=pause`: if the detected lane is not the expected one → **PAUSE** + instruction message (load the correct lane, re-check, `RESUME`).
- `ON_MISMATCH=error`: same but as a **G-code error** — aborts the queue and the rest of the calling macro. Required to gate a purge line from inside a start/resume macro: a `PAUSE` does not stop macro expansion (the purge would still run with the wrong filament).
- Inconclusive verdict (no arm / several arms) → reported honestly, never a false positive.
- The detected lane is remembered (`lll_loaded_lane` in save_variables).
- `MM` must be calibrated for your geometry: the arm must cross at least one sensor during the retract (with the event-based verdict it no longer has to *end* in a different zone).

### Automatic check at print start (opt-in)
```
SAVE_VARIABLE VARIABLE=lll_filament_check VALUE=1    ; enable (0 = disable)
```
Recommended flow (macros provided in `config/lll_macros.cfg.example`), in two steps:
1. **Pre-select as early as possible**: pass `TOOL=[initial_tool]` to your `PRINT_START` and have it call `_AFC_START TOOL=` as its first action — the print's lane is activated and the previous print's lane is **parked before heating even starts**.
2. **Check BEFORE the purge**: call `_AFC_CHECK` right after your final `M109`, right before the purge line (`ON_MISMATCH=error`):
- match → `LLL probe: lane 'lll2' chargée — ASA rouge (265C) - concordance OK` → purge, print
- mismatch → **"FILAMENT CHECK" error** before a single millimeter is extruded; the print stays paused → load the correct lane → `RESUME` replays heat → check → purge (free retry, clean plate)
Philosophy: **the function is available, never imposed** — disabled by default; each user enables it to suit their workflow (essential for manual multi-lane loading, pointless for a single lane).

### Boot note (fix included)
The `[lll_buffer]` sections must carry `role: parked`: each board starts parked **right from the configuration sequence** (zero window during which a lane would feed by default), then `_LLL_INIT` restores the **persisted** active lane (`afc_active_lane`, written to save_variables by `SET_ACTIVE_LANE`) 2 s after ready — never a hard-coded default: after a restart, the lane you are not using stays parked. Without it, a restarted board is `active/auto` (compiled-in default) for ~15-40 s.
✅ **Hard MCU reset — fixed in the firmware (fix #9, boot grace period)**: after a `FIRMWARE_RESTART`, an M112 or a power-cycle, the board starts **no autonomous motion** until Klipper has spoken to it once (20 s cap) — the compiled-defaults window that used to cause a brief phantom push is closed (field-proven on the reference setup: reset with a fully-tensioned arm → zero motion). Buttons, jog, run and roles stay live during the grace. Deliberate trade-off (not Mellow parity): a board never driven by a host waits 20 s after power-on before auto-feeding, where the stock firmware starts ~1 s after boot. `role: parked` remains recommended as a complementary layer (it also covers a Klipper host staying down > 20 s with a tensioned arm).

---

## 🖥️ Visual interface — everything INSIDE Mainsail (zero fork)

Four complementary layers — three in Mainsail, one on the physical display:

### 1. The "LLL Lanes" card (webcam iframe) — the always-on view
The LLL dashboard is displayed **inside the Mainsail dashboard as a webcam** (native `iframe` service). One card per lane, **auto-discovered** (a newly declared lane shows up on its own):
- **Color dot** for the filament (the spool's real color from Spoolman if `SPOOL_ID` is set, otherwise inferred from the color name), material, temp, spool number
- Role (green ACTIVE badge / orange parked / gray standby), filament presence, arm position (3 segments), motor moving, errors in red
- **Click actions**: activate the lane, jog ±, clear errors, **✎ edit material/color/spool** (sends `LLL_FILAMENT`)
- Live (websocket through Mainsail's nginx proxy, same origin), auto-reconnect
- File: `~/lll_dashboard/index.html` served at `/lll/` (nginx); Moonraker webcam "LLL Lanes", service `iframe`, URL `/lll/`. In Mainsail: Webcam panel → select "LLL Lanes" (or the cam grid view).

### 2. `LLL_PANEL` — the quick-actions dialog
A macro (provided in `config/lll_macros.cfg.example`; button in the Macros panel) that opens a **native Mainsail dialog**: each lane's state + Activate/Clear/Probe/Refresh buttons. Auto-discovered from `_AFC_VARS.lanes`. Every action re-queries the lanes and re-displays (fresh state).
⚠️ Lesson learned: in a Klipper macro template, a `#` ANYWHERE in the line starts a comment — even inside a jinja string. Never put a `#` in dialog texts.

### 3. Spoolman — the spool database (native Mainsail panel)
Service installed on the Pi (`http://<ip>:7912`, systemd `Spoolman`), hooked into Moonraker (`[spoolman]`). Manages the spools: color (dot), material, **remaining weight**, vendor. **The active spool follows the active lane automatically** (the `SPOOL_ID` bridge: `SET_ACTIVE_LANE` → consumption is deducted from the right spool, per lane). Declare a spool in Spoolman → note its number → `LLL_FILAMENT LANE=x SPOOL_ID=n`.

### 4. "LLL Buffer" LCD menu (physical BTT MINI12864 display)
Section added in `~/printer_data/config/lcd_menu.cfg` (under the main menu). To drive the lanes **without a remote screen**, right at the machine:
- **Each submenu's name = the lane's LIVE state**: `L1 ACTIVE` / `L1 PARKED` (+ ` !ERR` on error) — instant read of which lane is active.
- Per lane: **Activate** (`LLL_ROLE active`), **Park/Hold** (`LLL_ROLE parked`, grayed out while printing — guard on `print_stats.state`; ⚠️ remains clickable during a PAUSE: do not park the active lane mid-print, parked is persistent and the lane "stops following" until it is re-activated), **Clear error** (`LLL_CLEAR_ERROR`), + a detailed status line (`LLL_QUERY`: arm, presence, ecode).
- **Deliberately NO jog/rewind on the LCD**: the LLL boards' physical buttons are right next to it. The menu is for **confirming/toggling the state** (parked ↔ active), not for moving filament.
- **This is the interface for the safe rewind workflow**: park the lane on the LCD → move back with the physical buttons (a parked lane never re-feeds forward) → re-Activate it.

---

## 🧬 Multi-lane integration: from 2 to N LLL boards

### Profiles are PER LANE
Each lane has its **own independent profile catalog** (stored as `lll_<lane>_profiles`). Lane 1 can be tuned one way and the 10th completely differently — that's native. The same profile name can exist on several lanes with different contents. To duplicate a profile from one lane to another: apply the same settings on the other lane, then `LLL_PROFILE_SAVE` there.

### Adding lane N — full recipe (example: 3rd board → lane `lll3`)

**Step 0 — Flash.** Flash the board with the canonical firmware (Katapult no-DFU, see the guide). It shows up as:
```
/dev/serial/by-id/usb-Klipper_stm32f072xb_<SERIAL-UNIQUE>-if00
```
Each board has a unique burned-in serial — no risk of confusion, even with 10 boards on a hub.

**Step 1 — printer.cfg** (3 blocks per lane, modeled on the existing lanes):
```ini
[mcu LLL_PLUS_3]
serial: /dev/serial/by-id/usb-Klipper_stm32f072xb_<SERIAL-UNIQUE>-if00
restart_method: command

[lll_buffer lll3]
mcu: LLL_PLUS_3
#role: parked              # optional: startup settings specific to THIS lane
#feed_mode: auto
#poll_interval: 1.5
#event_gcode: ...

[filament_switch_sensor lll3_entrance]
switch_pin: ^!LLL_PLUS_3:PB7
pause_on_runout: True
runout_gcode:
    M118 LLL Plus 3: filament runout detected
insert_gcode:
    M118 LLL Plus 3: filament reinserted
```
⚠️ The inlet sensor is ALWAYS `^!<mcu_name>:PB7` (pull-up + inverted). The firmware never touches this pin — it's native Klipper.

**Step 2 — host macros** (2 additions, made in your copy of `config/lll_macros.cfg.example`):
```ini
# 1) declare the lane in the registry (dict order = tool order T0,T1,T2…)
[gcode_macro _AFC_VARS]
variable_lanes: {'lll': 'lll_entrance', 'lll2': 'lll2_entrance', 'lll3': 'lll3_entrance'}

# 2) create the matching tool (Tn = position n in the dict, 0-based)
[gcode_macro T2]
gcode: _COLOR_CHANGE LANE=lll3
```
That's all: `SET_ACTIVE_LANE`, boot (`_LLL_INIT`), swapping, persistence and profiles all **loop over the registry automatically** — nothing else to modify, no matter how many lanes.

**Step 3 — restart**: `RESTART` (config) — and if the firmware/extra changed, `sudo systemctl restart klipper`.

### On the OrcaSlicer side (N colors)
The Orca→Klipper bridge is just 2 lines, to add to your machine G-code (**Printer settings → "Machine G-code" tab → "Change filament G-code" field**); the `SET_NEXT_TEMP`/`Tn` macros are provided in `config/lll_macros.cfg.example`:
```
SET_NEXT_TEMP TEMP=[new_filament_temp]
T[next_extruder]
```
- Orca emits `T0`, `T1`, `T2`… at each color change → the `Tn` macro swaps the right lane. **The mapping = the registry order**: 1st Orca filament = 1st lane in the dict, and so on.
- To add colors: the machine is set as "single extruder multimaterial" → **"+"** button in the filament bar to add slots (as many as there are lanes declared on the Klipper side).
- `SET_NEXT_TEMP` passes the NEW filament's temperature for heating during the swap (different filaments = different temperatures, handled).
- Purge tower disabled (in your multicolor process profile; purging is handled by the swap macros).

### Practical limits (how many LLLs max?)
- **Klipper**: no hard limit on the number of MCUs — 6-8 MCU configs are common. Each LLL adds ~1 poll/1.5 s + clock sync: negligible even ×10.
- **The real factor = USB and power**: a **powered** USB hub is mandatory beyond 2-3 boards, and each LLL wants its own 24 V.
- **Identical firmware on every board** (one-flash, everything is differentiated by config) — that's the project's plug-and-play principle.

---

## 🆘 Troubleshooting cheat sheet

| Symptom | Reflex |
|---|---|
| "JAM" in the console | The motor is already stopped. Fix the physical issue, then `LLL_CLEAR_ERROR` (or a button press). `ecode` tells you the cause. |
| The lane no longer feeds | `LLL_QUERY`: check `role=active`, `mode`, `error=0`, `present=1` |
| Grinding / the Orbiter slips, LLL arm fully stretched | 1) `LLL_QUERY`: if `error=0`, this is the FSM's **by-design dead band** (the feed only starts with the arm at the hall3 end / arm=4 and stops at hall2 → ~13 mm drained without re-feeding; identical across all firmware versions) — the grinding then comes from the **friction downstream of the Y** (an SFS on the reference setup — adapt to your filament path), which the Orbiter absorbs alone during the dead band. 2) Check the mechanics downstream of the Y (SFS angle: an elbow adds a resistance the buffer cannot relieve). 3) Do NOT raise the Orbiter's run_current as a first reflex (it damages the filament, especially fiber-filled); reducing the dead band (e.g. starting the feed on hall2 loss) is a lead for future evolution. |
| A command is rejected | Explicit error message in the console + `cfgerr=1` on query. Often: a pin conflict (function vs IOEXT) or a 🔒 lock |
| Setting lost after a reboot? | No — every `LLL_SET/MODE/ENABLE/ROLE/PROFILE_LOAD` is persistent: the value is stored in `[save_variables]` **and re-sent to the MCU at startup** by the module (`lll_buffer.py`: `klippy:connect` handler → `_replay_saved()` → re-sends each tweak to the firmware). So it is not the standard `save_variables` alone (which only stores): it's this custom replay that makes the setting effective at every boot. Only `LLL_UNLOCK` and the IOEXT configs are deliberately per-session. |
| Freeze everything, urgently | `LLL_ROLE LANE=<lane> ROLE=standby` (and `M112` remains the real red button: the firmware cuts the motor on any shutdown) |
