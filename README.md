# Mellow FLY LLL Buffer Plus — Canonical v1 Klipper MCU Firmware (autonomous auto-feed + full host control)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
![MCU](https://img.shields.io/badge/MCU-STM32F072CB-green)
![Klipper](https://img.shields.io/badge/Klipper-MCU%20firmware-orange)
![Version](https://img.shields.io/badge/firmware-canonical%20v1-blueviolet)

Custom **Klipper MCU firmware** for the Mellow **FLY LLL Buffer Plus** that brings together the **best of both worlds**:

- 🟢 the **autonomous smart-buffer auto-feed** of Mellow's standalone firmware (hall sensors drive the feed motor *in firmware*, real-time), **and**
- 🟢 the board running as a **standard Klipper USB MCU** — runout → print pause, **plus** (new in this canonical release) a complete host control layer: 21 G-code commands, per-lane roles, runtime-toggleable functions, telemetry events, and an optional web dashboard inside Mainsail.

Both at the same time, over a single USB cable (+ 24 V motor power) — something neither the stock standalone firmware nor the community host-macro configs can do on their own.

---

## The bigger picture — multicolor with just buffers

This firmware is one brick of a larger goal: **multicolor / multi-material
printing on a single direct-drive toolhead without a selector MMU** — no
ERCF, no BoxTurtle, no AMS. One LLL Plus buffer per filament lane, a passive
splitter, Klipper orchestrating lane roles. Two lanes ran in production on
the reference setup, including real prints with color changes; a fully
automatic load/unload pipeline was prototyped end-to-end and worked, then
was withdrawn — held back by missing *equipment* (toolhead cutter, passive
respoolers), not by the concept.

![Reference rig](docs/img/architecture_tested.svg)

**Read the full story — objective, what was proven, what held us back, and
why an equipped, clean installation makes LLL-only multicolor entirely
feasible:** [docs/THE_PROJECT_STORY.en.md](docs/THE_PROJECT_STORY.en.md)
([FR](docs/THE_PROJECT_STORY.fr.md)) · method details:
[docs/MULTICOLOR_STATUS.en.md](docs/MULTICOLOR_STATUS.en.md).

---

## What's new — "Canonical v1, runtime-toggleable" (v1.0.0 of this repo)

The autonomous smart-buffer core is **unchanged** (same hall conventions, same TMC2208 `VACTUAL` drive, same feed behaviour, buttons included) — with one deliberate exception: after a hard MCU reset, autonomous feeding waits for the host or 20 s (safety fix #9, see [Known behaviour](#known-behaviour--limitations)). Everything below is added **on top**:

- **11 runtime-toggleable functions** (`LLL_ENABLE`): FEED, BUTTONS, MBSIG, ENTRANCE, TMC, MOVE, DIAG, LED, ADC, TELEM, IOEXT — **one flash**, everything selectable at runtime. No more rebuilding to change behaviour.
- **Roles per lane**: `active` / `parked` / `standby`. A parked lane **never auto-feeds forward** (multi-lane safety) but still follows a rewind; standby is fully frozen. `LLL_SET_IDLE` is kept as a backwards-compatible alias.
- **4 feed automation levels**: `auto` / `semi_auto` (one cycle per `LLL_ARM`) / `semi_manual` / `manual`.
- **Bounded motion only**: `LLL_JOG` (hard-limited to 10 s in firmware) and `LLL_RUN` (deadline, refreshable). **No unbounded motion order exists in the command set.**
- **Runtime tuning** (`LLL_SET`): feed/back timeouts, TMC current, microsteps, stealthChop, telemetry mask… persisted and replayed at every boot. **Named profiles** per lane (`LLL_PROFILE_SAVE / LOAD / LIST / DELETE`).
- **Telemetry events** (`lll_event` push) with a configurable `event_gcode` hook — react to jams / runout / diag edges from printer.cfg in ~20–50 ms instead of waiting for a poll.
- **IOEXT**: a 12-channel, **index-only** GPIO/PWM/ADC register for the spare connectors — critical pins are *not in the table*, hence physically unreachable; unverified pins are gated behind `LLL_UNLOCK`.
- **Filament identity & probing**: `LLL_FILAMENT` (type / color / temp / Spoolman spool) and `LLL_DETECT_LOADED` (detects which lane is physically engaged in the extruder using only the buffer arm sensors).
- **Multi-lane, N-scalable**: one `[mcu]` + one `[lll_buffer]` section per board — every G-code command is multiplexed by `LANE=`. The dashboard auto-discovers new lanes.
- **Web dashboard** (optional): a single self-contained HTML page shown inside Mainsail as an iframe webcam card, with a VCR-style per-lane transport. See [Dashboard & UI](#dashboard--ui).

---

## Why this exists

The official Mellow firmware is **standalone**: the buffer auto-feeds on its own but the board does **not** talk Klipper (no USB MCU; runout has to go through a separate signal wire).

The community Klipper integrations ([river29](https://github.com/river29/Mellow-LLLBufferPLUS-klipper), [ss1gohan13](https://github.com/ss1gohan13/BufferPLUS-klipper)) flash **stock Klipper** and re-implement the buffer with **host-side g-code macros**. That works, but feed bursts go through the **g-code queue** (latency, can lag during long `G1 E` moves) and they switch the active extruder, which can **misdirect extrusion during a print**.

This project runs the buffer state machine **inside the MCU firmware** as a Klipper task — zero g-code-queue latency, no active-extruder juggling, can't stall a print — while the host gets a **full command surface** on top.

| | Standalone (Mellow) | Stock Klipper + host macros | **This firmware** |
|---|---|---|---|
| Auto-feed | ✅ in-firmware | ⚠️ host macros (queue latency) | ✅ **in-firmware** |
| Klipper MCU / USB sensor | ❌ | ✅ | ✅ |
| Risk of disrupting a print | n/a | ⚠️ `ACTIVATE_EXTRUDER` | ✅ none |
| Runout → print pause | signal wire | USB (PB7) | USB (PB7) |
| Host control (roles, modes, moves, tuning) | ❌ | partial (macros) | ✅ **21 G-code commands** |
| Multi-lane | ❌ | ⚠️ manual macro work | ✅ N-scalable (`LANE=`) |
| One flash, all features | ❌ | — | ✅ runtime-toggleable |

---

## Architecture

Three stages, each optional beyond the first:

1. **MCU firmware task** (`src/buffer.c`) — the autonomous state machine (Mellow's logic ported verbatim) plus a command layer: **15 MCU commands / 5 response messages** (`lll_state`, `lll_ack`, `lll_event`, `lll_io`, `lll_io_ack`). The protocol block is shared byte-identically with the host extra. The file is *added* to the Klipper tree (modifies nothing) and survives `git pull`.
2. **Klipper host extra** (`klippy/extras/lll_buffer.py`) — one `[lll_buffer <lane>]` object per board: registers the **21 G-code commands** (per-lane mux by `LANE=`), exposes live state to macros via `printer["lll_buffer <lane>"]`, runs your `event_gcode` on every firmware event, and **persists + replays** roles/modes/enables/tweaks through `[save_variables]` at every connect.
3. **Web dashboard** (`dashboard/index.html`, optional) — a static page served by your existing nginx, talking to Moonraker's WebSocket, embedded in Mainsail as an iframe webcam card.

---

## How it works (the autonomous core)

A small Klipper MCU task (`src/buffer.c`, `DECL_INIT` + `DECL_TASK`) ports the state machine of Mellow's open firmware ([Fly3DTeam/Buffer](https://github.com/Fly3DTeam/Buffer)) and drives the on-board TMC2208 over its single-wire UART (`VACTUAL` velocity mode):

```
HALL3 (PB4) blocked  → motor FORWARD (push filament)
HALL2 (PB3) blocked  → motor STOP
HALL1 (PB2) blocked  → motor BACK (retract)
ENDSTOP_3 (PB7) = no filament → STOP   (+ host can pause the print)
```

Photo-sensor convention is taken **verbatim** from the source: *blocked = 1, clear = 0*. Manual **feed/retract buttons** (KEY1/KEY2) and the **mainboard control signals** (PB5/PB6, active-low) are supported too (both runtime-toggleable). A 60 s continuous-feed timeout acts as a jam safety; a matching 60 s back-timeout guards rewinds. Error codes (`ecode`) a lane can latch: `none`, `feed_timeout`, `back_timeout`, `diag_fault`, `host_alive`, `clear_pending` — reported by `LLL_QUERY` and pushed as `lll_event`.

### Pins (STM32F072CB)

| Function | Pin | Function | Pin |
|---|---|---|---|
| HALL1 | PB2 | KEY1 (retract) | PB13 |
| HALL2 | PB3 | KEY2 (feed) | PB12 |
| HALL3 | PB4 | FRONT signal | PB5 |
| Entrance switch | PB7 | BACK signal | PB6 |
| TMC EN | PA6 | TMC UART | PB1 |
| TMC DIAG | PB0 | LEDs | PA8 / PA15 |

Spare I/O (IOEXT channels): PA4, PA5, PB10, PB11, PA2, PA3, plus bench-gated LED/ADC pins. USB, SWD, TMC UART, halls, PB7, buttons and the 5 V headers are **outside** the IOEXT table by construction.

---

## Safety hardening

Nine fixes, **all present in the shipped code**:

1. `sched_is_shutdown()` guard as the **literal first line** of the buffer task, plus a `DECL_SHUTDOWN` handler that drives the motor enable high. **Bench-proven:** an `M112` fired in the middle of a commanded `LLL_RUN` cuts the motor dead.
2. **Single-writer**: command handlers set flags only — only the task and the shutdown handler ever touch EN/UART.
3. A forward run requires `present && !error`, re-checked **every tick**; backward runs stay allowed on runout (legitimate unload).
4. `back_timeout` (default 60 s) on parked follow and commanded back runs.
5. Critical pins (5 V headers PB8/PB9, host lines PB14/PB15, USB, SWD, TMC UART, halls, PB7, buttons) are **outside the IOEXT table** — index-only addressing makes them unreachable, not merely forbidden.
6. Every stop writes `VACTUAL=0` **before** re-asserting enable; every motion rewrites GCONF+VACTUAL.
7. Roles / modes / enables / tweaks are replayed in Klipper's **config sequence** (before `ready`) — `role: parked` closes the startup window on Klipper restarts.
8. **No UART bit-bang inside command handlers**; runtime TMC writes are deferred and spaced inside the task.
9. **Boot grace period**: after a hard MCU reset (`FIRMWARE_RESTART`, `M112`, power-cycle), no autonomous feed starts until the host has spoken once (20 s cap) — closes the phantom-push window of the compiled defaults; the done-latch is evaluated every tick so the grace can never re-arm (timer wrap included). **Field-proven:** `FIRMWARE_RESTART` with a fully-tensioned arm → zero motion. See [Known behaviour](#known-behaviour--limitations) for the deliberate standalone trade-off.

---

## Host G-code commands

All commands take `LANE=<lane>` (the `[lll_buffer <lane>]` section name), except `LLL_DETECT_LOADED` which is global.

| Command | What it does |
|---|---|
| `LLL_QUERY` | Full state report: role, mode, error/ecode, feeding, running, arm position, filament present, diag, cfgerr |
| `LLL_ROLE` | Set lane role `active` / `parked` / `standby` (persistent) |
| `LLL_SET_IDLE` | Backwards-compat alias (`VALUE=1` → parked, `0` → active) |
| `LLL_MODE` | Set FEED automation level `auto` / `semi_auto` / `semi_manual` / `manual` |
| `LLL_ARM` | Arm exactly one feed cycle (semi_auto mode) |
| `LLL_ENABLE` | Toggle one of the 11 functions at runtime |
| `LLL_SET` | Set a runtime parameter (timeouts, TMC current/microsteps/stealth, run limits, telemetry mask…) — persisted |
| `LLL_JOG` | Bounded jog `fwd` / `back` / `stop`, clamped to 10 s |
| `LLL_RUN` | Bounded run with deadline (`MS=0` → `run_max_ms`), refreshable |
| `LLL_STOP` | Stop host-commanded motion (autonomous behaviour resumes) |
| `LLL_CLEAR_ERROR` | Clear a latched jam/error (refused while a driver fault is still asserted) |
| `LLL_PROFILE_SAVE` | Save current tweaks as a named profile |
| `LLL_PROFILE_LOAD` | Apply a named profile (persists as current) |
| `LLL_PROFILE_LIST` | List saved profiles |
| `LLL_PROFILE_DELETE` | Delete a named profile |
| `LLL_FILAMENT` | Register the filament loaded on this lane (`TYPE=`, `COLOR=`, `TEMP=`, `SPOOL_ID=` for Spoolman) |
| `LLL_UNLOCK` | Unlock unverified IOEXT pins for the session (`KEY=0xDEB1`) |
| `LLL_IO_CFG` | Configure an IOEXT channel (din / dout / soft-PWM / ADC / release) |
| `LLL_IO_WRITE` | Write an IOEXT dout / PWM duty |
| `LLL_IO_READ` | Read an IOEXT channel |
| `LLL_DETECT_LOADED` | **Global**: probe which lane is physically loaded, using only the arm sensors (`EXPECTED=`, `ON_MISMATCH=warn\|pause\|error`) |

Full manual with parameters, examples and pitfalls: **[docs/LLL_COMMANDS_MANUAL.en.md](docs/LLL_COMMANDS_MANUAL.en.md)** (English) · **[docs/COMMANDES_LLL_MANUEL.fr.md](docs/COMMANDES_LLL_MANUEL.fr.md)** (French).

---

## Configuration

Start from **[config/printer.cfg.example](config/printer.cfg.example)** — one `[mcu]` + one `[lll_buffer]` pair per board:

```ini
[mcu LLL_PLUS]
serial: /dev/serial/by-id/usb-Klipper_stm32f072xb_XXXXXXXXXXXX-if00
restart_method: command

[save_variables]
filename: ~/printer_data/variables.cfg

[lll_buffer lll]
mcu: LLL_PLUS
role: parked
```

Two things matter:

- **`role: parked` is the recommended startup role.** The compiled-in default is `active`/`auto` ("keep buffering" even with no host), so there is a short window at startup where the board could push filament before Klipper's config arrives. `role: parked` closes that window for **Klipper restarts** (the role is replayed in the config sequence, before `ready`). Activate the lane you actually print with afterwards: `LLL_ROLE LANE=lll ROLE=active`. See [Known behaviour](#known-behaviour--limitations) for the hard-MCU-reset case.
- **`[save_variables]` is required for persistence.** Roles, modes, enables, `LLL_SET` tweaks, profiles and filament declarations are stored there and **replayed to the board at every connect** — your tuning survives restarts and re-flashes.

Optional section options: `feed_mode:` (startup automation level), `enable:` / `disable:` (force functions on/off from config), `poll_interval:` (state refresh, default 1.5 s), and `event_gcode:` (a Jinja template run on **every** event the board pushes — e.g. `PAUSE` on a jam within ~20–50 ms).

The entrance sensor can still pause the print the classic way — it is exposed as a normal MCU pin:

```ini
[filament_switch_sensor lll_entrance]
switch_pin: ^!LLL_PLUS:PB7
pause_on_runout: True
```

---

## Dashboard & UI

An optional, self-contained control panel (`dashboard/index.html`) displayed **inside Mainsail** as an interactive iframe webcam card. No Mainsail fork, no plugin, no extra service — one static file served by the nginx you already have:

```nginx
location /lll/ { alias /home/pi/lll_dashboard/; index index.html; add_header Cache-Control "no-store"; }
```

- Talks to Moonraker over its WebSocket through Mainsail's existing proxy — same origin, **zero CORS configuration**.
- **Auto-discovers every lane**: declare a new `[lll_buffer <lane>]` and its card appears by itself.
- **VCR-style transport** per lane: **hold** = jog, **double-click** = continuous, **single click** = stop. Continuous mode is a stream of **10-second-bounded orders refreshed every 3 s** — the page never sends an unbounded command, so if the browser dies mid-run the motor stops on its own in under 10 s.
- **Feed-gate aware** (v1.0.2): the footer always shows *active lane · loaded lane*; when the safety gate freezes a lane (selected while a different one is still physically loaded), its card shows a **frozen badge** and a one-click **unfreeze** (deliberate override: reset the loaded-lane memory and re-arm). A header **filament check** button runs `LLL_DETECT_LOADED` on demand.
- **Language follows Mainsail's own setting** (read from the Moonraker database), falling back to the browser language. English and French bundled.
- **Spoolman** (optional): run Spoolman as its own service (port 7912), add `[spoolman]` with `server: http://localhost:7912` / `sync_rate: 5` to `moonraker.conf` → Mainsail's native spool panel appears, and `LLL_FILAMENT ... SPOOL_ID=` links a lane to a spool. **When a lane becomes active, its spool becomes Moonraker's active spool** — per-spool usage tracking follows the lane.
- **LCD menu** (optional): [config/lcd_menu_lll.cfg.example](config/lcd_menu_lll.cfg.example) for a BTT MINI12864 — per-lane state + Activate / Park / Clear error. Safe manual-rewind workflow: **Park → physical buttons → Activate** (a parked lane never re-feeds forward under you).

Full walkthrough: **[dashboard/INSTALL.md](dashboard/INSTALL.md)**.

---

## Installation

> Done from your Klipper host (e.g. a Raspberry Pi). Commands assume `~/klipper` and `~/katapult` ([Arksine/katapult](https://github.com/Arksine/katapult)) are cloned, the Klipper build toolchain is installed, and `dfu-util` is available (`sudo apt install dfu-util`).
>
> No usable BOOT/RESET buttons on your unit? See the companion guide: [LLL-Plus-Katapult-NoDFU](https://github.com/Nitrooxyde/LLL-Plus-Katapult-NoDFU).

### Step 0 — Find your buffer & power it

Connect the buffer's USB to the host **and** give it **24 V on VIN** (the motor needs it; the firmware configures the TMC at first boot, so 24 V must be present then). List USB serial devices to identify the board and note your **unique chip ID** (you'll reuse it in `printer.cfg`):

```bash
ls /dev/serial/by-id/
# e.g. usb-Klipper_stm32f072xb_3C003A...3720-if00   (or a Mellow / katapult id)
#                            └──────── your XXXXXX ────────┘
```

### Step 1 — Enter DFU mode (to install Katapult)

On the buffer board: **hold BOOT**, tap **RESET**, release **RESET**, then release **BOOT**. Confirm with `sudo dfu-util -l` → `Found DFU: [0483:df11] ... name="@Internal Flash  /0x08000000/064*0002Kg"`.

### Step 2 — Back up the existing firmware (recommended)

```bash
sudo dfu-util -a 0 -d 0483:df11 -U ~/lll_backup_flash.bin       -s 0x08000000:0x20000
sudo dfu-util -a 1 -d 0483:df11 -U ~/lll_backup_optionbytes.bin            # option bytes
```

### Step 3 — Build & flash Katapult (one-time bootloader)

Katapult lets you flash all **future** updates over USB without opening the case.

```bash
cd ~/katapult
make menuconfig
```

- Micro-controller Architecture: **STMicroelectronics STM32**
- Processor model: **STM32F072**
- Clock Reference: **8 MHz crystal**
- Communication interface: **USB (on PA11/PA12)**
- Application start offset: **8 KiB**
- Support bootloader entry on rapid double click of reset: **OFF** ⚠️ *(the board **does** have BOOT/RESET buttons — that's how you entered DFU in Step 1 — but double-click-reset bootloader entry is **not reliable on this board**, and leaving this ON makes it fail to enumerate. It isn't needed anyway: normal bootloader entry is done over USB — `flashtool.py -r`, done automatically when flashing.)*

```bash
make clean && make
# board still in DFU (Step 1); flash with mass-erase so no stale app remains:
sudo dfu-util -a 0 -d 0483:df11 -D out/katapult.bin -s 0x08000000:force:mass-erase:leave
```

Re-plug USB and confirm Katapult enumerates: `ls /dev/serial/by-id/` → `usb-katapult_stm32f072xb_XXXXXX-if00`.

### Step 4 — Get this repository

Clone this repo onto the host (all following steps reference it by absolute path — you are still in `~/katapult` at this point, so relative paths would break):

```bash
git clone https://github.com/Nitrooxyde/Mellow-LLL-Plus-Canonical.git ~/lll-firmware
cd ~/lll-firmware
```

### Step 5 — Add this firmware to Klipper

Two files — the MCU task **and** the host extra:

```bash
cp ~/lll-firmware/src/buffer.c ~/klipper/src/buffer.c
cp ~/lll-firmware/klippy/extras/lll_buffer.py ~/klipper/klippy/extras/lll_buffer.py
```

Add **one line** to `~/klipper/src/Makefile` (only compiled for the F072 buffer, never for your main board):

```make
src-$(CONFIG_MACH_STM32F072) += buffer.c
```

> ⚠️ This Makefile line is reverted by `git pull` on the Klipper tree — re-add it after updating Klipper. `buffer.c` and `lll_buffer.py` themselves are untracked and survive.

### Step 6 — Build Klipper

> ⚠️ `make menuconfig` overwrites `~/klipper/.config`. If you build other MCUs (your main board, toolhead…) from this same tree, **back up its config first** and restore it afterwards:
> ```bash
> cp ~/klipper/.config ~/klipper/.config.mainboard   # backup
> # ...build & flash the buffer...
> cp ~/klipper/.config.mainboard ~/klipper/.config    # restore later
> ```

The exact build configuration used for the shipped binary is provided as **[build/klipper.config](build/klipper.config)** — you can copy it straight in:

```bash
cp ~/lll-firmware/build/klipper.config ~/klipper/.config
cd ~/klipper && make olddefconfig     # or: make menuconfig and verify
```

Or set it manually via `make menuconfig`: **STMicroelectronics STM32** → **STM32F072** → Bootloader offset **8 KiB bootloader** *(must match Katapult — application address `0x8002000`)* → Clock Reference **8 MHz crystal** → Communication interface **USB (on PA11/PA12)**.

```bash
make clean && make
```

### Step 7 — Flash over USB (via Katapult)

```bash
~/klippy-env/bin/python3 ~/katapult/scripts/flashtool.py \
  -f ~/klipper/out/klipper.bin \
  -d /dev/serial/by-id/usb-katapult_stm32f072xb_XXXXXX-if00
```

> Use the **klippy-env** Python — it has `pyserial` (the system Python usually doesn't). After flashing, the board re-enumerates as `usb-Klipper_stm32f072xb_XXXXXX-if00`.

### Step 8 — printer.cfg

Follow [Configuration](#configuration) / [config/printer.cfg.example](config/printer.cfg.example), then `FIRMWARE_RESTART`.

**No motor/stepper/TMC section** — the firmware owns the motor; the host talks to it through `[lll_buffer]`.

---

## Prebuilt binary

If you'd rather not build, the repo ships the **exact binary running in production on the reference setup**: [`build/klipper_canonical_v1_fix9.bin`](build/klipper_canonical_v1_fix9.bin) (53 824 bytes), built from Klipper `v0.13.0-700` (commit `d6ea62542`), build `20260705_234145`, config = [`build/klipper.config`](build/klipper.config). It is the canonical firmware including safety fix #9 (boot grace period).

SHA-256: `9239bdc367096438e72528d361f75f1ebb811784698cb0872ca79b2410837944`

```bash
sha256sum ~/lll-firmware/build/klipper_canonical_v1_fix9.bin    # verify before flashing
~/klippy-env/bin/python3 ~/katapult/scripts/flashtool.py \
  -f ~/lll-firmware/build/klipper_canonical_v1_fix9.bin \
  -d /dev/serial/by-id/usb-katapult_stm32f072xb_XXXXXX-if00
```

Building from source (Steps 4–7) remains the clean path — it guarantees your `klippy` host version and the MCU firmware come from the same tree.

## Updating later (no case opening)

> ⚠️ Same multi-MCU warning as Step 6: if you build other MCUs from this Klipper tree, `~/klipper/.config` may currently target your **main board** — building with it and flashing the result to the buffer would flash the **wrong binary**. Re-copy the buffer config (or verify via `make menuconfig` that `.config` targets the STM32F072) before building.

```bash
[ -f ~/klipper/.config ] && cp ~/klipper/.config ~/klipper/.config.pre_lll   # backup whatever was there
cp ~/lll-firmware/build/klipper.config ~/klipper/.config   # make sure the build targets the buffer (STM32F072)
cd ~/klipper && make olddefconfig
make clean && make          # (re-add the Makefile line if a git pull removed it)
~/klippy-env/bin/python3 ~/katapult/scripts/flashtool.py \
  -f ~/klipper/out/klipper.bin \
  -d /dev/serial/by-id/usb-Klipper_stm32f072xb_XXXXXX-if00
# then FIRMWARE_RESTART
```

`flashtool` reboots the running app into Katapult over USB automatically — you can target the **Klipper** serial directly, no buttons. The bundled helper automates the whole flash (bootloader request, wait, flash, wait for re-enumeration): `bash ~/lll-firmware/scripts/flash_lll.sh <chip-serial>` — one board at a time, flashes `~/klipper/out/klipper.bin`.

---

## TMC2208 register values

Derived directly from Mellow's firmware parameters (via the `TMCStepper` library it uses), not guessed:

| Register | Value | Notes |
|---|---|---|
| GCONF | `0x000001C4` | spreadCycle, pdn_disable, mstep_reg_select (shaft bit set for FORWARD) |
| CHOPCONF | `0x12020055` | toff=5, vsense, 64 µsteps, intpol |
| IHOLD_IRUN | `0x00010F07` | IRUN=15, IHOLD=7 (≈ `rms_current(500)` @ Rsense 0.11) |
| PWMCONF | `0xC10D0024` | pwm_autoscale |
| VACTUAL | `77575` | 260 rpm, 64 µsteps |

With the canonical firmware you no longer need to edit the source to tune these: `LLL_SET LANE=... PARAM=...` changes current (IRUN/IHOLD), microsteps, stealthChop, standstill mode, TPOWERDOWN and the VACTUAL magnitude at runtime — persisted and replayed at boot. Bounds are clamped to the deployed known-good values; don't raise the current without your motor's datasheet.

---

## Known behaviour & limitations

### Mainsail shows MCU load ~100 % (false positive)

Once this firmware is running, Mainsail's **MCU Load** bar for the LLL Plus may read **~100 %**. **This is a false positive — the MCU is not overloaded.**

The auto-feed task bit-bangs the TMC2208 `VACTUAL` value over a software UART. Each update is a short, tightly-timed burst (~8 ms), which inflates the per-task timing **standard deviation** that Mainsail's load indicator is built on — even though the MCU is idle the vast majority of the time.

Measured over a full print (`klippy.log` / MCU stats):

| Metric | Value | Meaning |
|---|---|---|
| `mcu_awake` | ~0.03–0.06 | awake only **~3–6 %** of the time |
| `mcu_task_avg` | ~0.4 ms | tasks are short |
| `mcu_task_stddev` | ~2.5 ms | spikes from the bit-bang `VACTUAL` bursts (~8 ms) — this is what drives the "100 %" bar |
| `send_seq` vs `receive_seq` | equal | no command backlog |
| retransmits | a few bytes over a whole print | link healthy |
| "Timer too close" / shutdowns | **0** | no real overload |

**Conclusion:** display artifact of the bit-bang timing jitter. Auto-feed, buttons and runout all keep working normally — no action needed.

### Feed dead band between the STOP and PUSH hall zones (by design)

The state machine — identical to Mellow's standalone, all versions — starts a feed **only when the tension arm reaches the hall3 (push) position** and stops it at hall2 (balanced). Between the two there is a dead band (**~13 mm of filament on the reference setup**) during which the extruder pulls against the buffer alone until the arm travels back to hall3. This is inherent to the three-sensor design, not a fault; it matters if you're diagnosing brief tension rises right after a feed cycle ends.

### Startup after a hard MCU reset — fixed by the boot grace period (fix #9)

The compiled-in defaults are `active` / `auto` (deliberate: a board that loses its host **keeps buffering**). After a **hard MCU reset** (`FIRMWARE_RESTART`, an `M112` emergency stop, or a power-cycle) the task runs on those defaults before Klipper's config arrives — on earlier builds this caused a brief phantom filament push whenever the tension arm was loaded. **The shipped firmware closes this**: no autonomous step-9 motion (forward feed or active-role back-follow) starts until the host has spoken once, capped at 20 s. Buttons, jog, run, roles and the parked follow stay live during the grace. Field-proven on the reference setup: `FIRMWARE_RESTART` with a fully-tensioned arm → zero motion, arm position unchanged.

Two honest notes:
- **Deliberate standalone trade-off, not Mellow parity**: a board that is *never* host-driven starts auto-feeding 20 s after power-on, where Mellow's stock standalone firmware starts ~1 s after boot. That one-time latency is the price of closing the post-reset push.
- `role: parked` in printer.cfg remains recommended as a complementary layer (it also covers the case of a Klipper host that stays down longer than 20 s with a tensioned arm).

### Kalico and other Klipper derivatives (untested)

This project is developed and tested on **mainline Klipper only**. It has never been run on Kalico or on any other Klipper derivative.

- **Why it can stop at startup.** The host module `lll_buffer.py` calls Klipper's internal Python API, and a derivative does not always follow mainline's changes to it. Known case ([issue #2](../../issues/2)): mainline Klipper renamed `mcu.register_response()` to `mcu.register_serial_response()` in March 2026 and Kalico kept the old name, so up to v1.0.3 the host halts at connect with `Internal error during connect: 'MCU' object has no attribute 'register_serial_response'`. A mainline Klipper older than March 2026 stops the same way.
- **What to try.** Take `klippy/extras/lll_buffer.py` from **v1.0.4 or later** (it uses whichever of the two names the host provides), copy it over the one in your host's `klippy/extras/` (Step 5), then restart the Klipper service itself — a plain `RESTART` does not reload a modified extra. If the host then stops on another `... has no attribute ...` error, it is probably the same kind of difference elsewhere in the module: the name in the message and the traceback in `klippy.log` point to the line.
- **Feedback wanted.** The v1.0.4 change was written from Kalico's sources; it has not been run on a Kalico machine. If you run this project on Kalico or another derivative, please tell us how it went with a [🧪 field test report](../../issues/new?template=test_report.yml) — working or not, with the host version and the exact error text if there is one. That is what will tell whether this is sorted out.

---

## Field validation status

Honest scope of what has been tested:

- **One reference setup**: a single printer, **two LLL Plus boards** (two lanes), STM32F072CB.
- **Host software: mainline Klipper only** (`v0.13.0-700`, commit `d6ea62542`, on the reference printer). Kalico and other Klipper derivatives have never been run — see [Kalico and other Klipper derivatives](#kalico-and-other-klipper-derivatives-untested).
- The shipped binary has been running **in production** on both boards since its build date, through **multiple real multi-day prints including filament color changes**.
- **Bench-tested module by module**: roles, modes, enables, bounded jog/run, error latch/clear, IOEXT gating, telemetry events, profile persistence, and the `M112` shutdown kill.
- **Not exercised on this setup** (no hardware/wiring to trigger them): mainboard signal control (PB5/PB6) beyond bench checks, and non-default IOEXT wiring on the spare headers.
- **No other board revision or hardware combination has been tested.** Pins and register values are specific to this board.

---

## Host macros (example)

**[config/lll_macros.cfg.example](config/lll_macros.cfg.example)** ships the multi-lane orchestration layer used on the reference setup: `SET_ACTIVE_LANE`, `_LLL_INIT`, the `Tn` / `_COLOR_CHANGE` tool-change entry points, `LLL_PANEL`, and friends. It is the layer the dashboard and the command manuals refer to. Copy it into your `printer.cfg` (or `[include]` it) and adapt lane names, temperatures and purge values to your machine.

Since **v1.0.2** this layer ships a **feed-gate**: `SET_ACTIVE_LANE` never arms a lane's autonomous feed while a *different* lane is physically loaded (`lll_loaded_lane` memory, written by `LLL_DETECT_LOADED`), with automatic re-arm during a swap once the old filament has left the extruder entry — see the [CHANGELOG](CHANGELOG.md) for the field incident behind it.

## Multicolor / automatic filament change — status

On the reference setup, filament color changes are done **manually**: there is no automatic cutter and no passive respoolers, so a human swaps the filament at the `Tn` pause. A complete automatic pipeline (unload / load / resume orchestration) was prototyped and did work, but it is **not shipped** in this repo. Full, honest breakdown of what works, what was prototyped and what is missing: **[docs/MULTICOLOR_STATUS.en.md](docs/MULTICOLOR_STATUS.en.md)** (English) · **[docs/MULTICOLOR_STATUS.fr.md](docs/MULTICOLOR_STATUS.fr.md)** (French).

## Experimental: Phase C (v1.1) — informational only

The **[experimental/phase-c/](experimental/phase-c/README.md)** folder exposes, purely for information and **without any official status**, a 46-line evolution of the firmware: a grace period at boot plus an optional `rewind_pause` behaviour. It was written, reviewed, flashed and bench-tested — then **withdrawn from the reference setup**: the `rewind_pause` option proved dangerous on a lane actively printing. Since then, **its grace-period half has been re-derived, hardened (tick-level done-latch) and integrated into the official firmware as safety fix #9** — what remains specific to this folder is the `rewind_pause` story. It is provided as-is for feedback and testing by people who know what they are doing; it is **not** what the shipped binary contains. Details, diff and post-mortem: [experimental/phase-c/README.md](experimental/phase-c/README.md).

## 🙏 We're asking for your feedback — it's what makes this evolve

This firmware has been validated on **one** setup. If you get the chance to
try it on yours, **please tell us how it went** — we are genuinely asking:

- **It misbehaved?** Open a [🐛 bug report](../../issues/new?template=bug_report.yml)
  — and give us as much detail as you can on **how the problem presented
  itself** (when, doing what, repeatable or not, `LLL_QUERY` output, log
  excerpt). That detail is exactly what lets us keep improving this repo.
- **It worked? Partially? Surprised you?** A [🧪 field test report](../../issues/new?template=test_report.yml)
  is just as valuable — successes from different setups tell us what is
  solid, failures tell us what to fix next.
- PRs welcome too, and general discussion lives in the
  [TeamFDM thread](https://www.teamfdm.com/forums/topic/7617-faster-printing-requiring-filament-buffer-feeding/)
  where this project took shape.

Don't hesitate — no report is too small, and none of them will be met with
anything but gratitude.

## Language note

The shipped sources are **byte-identical** to the deployment validated in production on the reference setup — that is the core guarantee of this repo, backed by the published SHA-256 fingerprints. As a consequence, some runtime messages and code comments are in **French**. Translations are welcome as PRs, but be aware they would break the byte-for-byte identity with the deployed code — which is why the shipped files themselves are left untouched.

---

## Status / disclaimer

**⚠️ No warranty — use at your own risk.** This is experimental, community firmware provided **AS-IS, with NO WARRANTY of any kind** (see the [GPL-3.0](LICENSE) license — *Disclaimer of Warranty* §15 and *Limitation of Liability* §16). Flashing MCU firmware **can brick the board or damage hardware**. You assume **all** risk; the authors accept **no liability** for any damage, bricked board, failed/ruined print, or other loss. **Back up your existing firmware first** (Step 2) and make sure you understand each step before running it.

**We tested this as far as our own hardware allowed — no further.** Everything marked "field-proven" was proven on **one** reference setup (two lanes, one printer); anything beyond that (other boards revisions, other printers, more lanes, the multicolor pipeline itself) is *your* experiment, on *your* responsibility. If you follow this repo, you do so at your own risk — and we would genuinely love to hear how it went, but we are in no way responsible for the outcome.

## Credits — who did what

This project did not start here, and it was not built by one pair of hands:

- **The idea comes from the community.** It grew out of many reads through the Voron community and the TeamFDM forums (notably [this buffer-feeding thread](https://www.teamfdm.com/forums/topic/7617-faster-printing-requiring-filament-buffer-feeding/)) and the research they pushed us into — that is what motivated this little project in the first place. [Fly3DTeam / Mellow](https://github.com/Fly3DTeam/Buffer) published the original FLY Buffer firmware & hardware (the autonomous state machine is ported from their open source); [river29](https://github.com/river29/Mellow-LLLBufferPLUS-klipper), [ss1gohan13](https://github.com/ss1gohan13/BufferPLUS-klipper) and [depau/lll-buffed](https://github.com/depau/lll-buffed) opened the Klipper road before us.
- **Constraints, resolutions and field testing — [Nitrooxyde](https://github.com/Nitrooxyde).** The design decisions under real-world constraints (USB-only, one board per lane, park-first safety, sensors at determined positions with measured distances) and every hour of bench and production validation on the reference setup.
- **Implementation — Claude (Anthropic's AI).** The firmware, the host extra, the dashboard and the documentation were written by Claude working in pair with Nitrooxyde — it is what made this project possible, and it is stated here openly (see also the `Co-Authored-By` in the commit history). We would rather say it upfront than have anyone feel misled.
- **[Klipper](https://github.com/Klipper3d/klipper)** — MCU firmware framework and host.
- **[Katapult](https://github.com/Arksine/katapult)** — USB bootloader. Install guide without opening the case: [LLL-Plus-Katapult-NoDFU](https://github.com/Nitrooxyde/LLL-Plus-Katapult-NoDFU).
- **[Spoolman](https://github.com/Donkie/Spoolman)** — spool database (optional integration).

## License

[GPL-3.0](LICENSE) — derived from GPL-3.0 sources (Fly3DTeam/Buffer, Klipper).
