# Changelog

## v1.0.3 — `LLL_DETECT_LOADED` reads the arm events during the retract — 2026-09-16

Host-side only (Klipper extra `lll_buffer.py`): **no firmware rebuild or flash needed** — keep running the v1.0.0 binary.

### Fixed
- `LLL_DETECT_LOADED` false negatives ("aucun bras n'a bougé" while the buffer arm visibly
  moved). The probe used to compare the arm's hall state **before** and **after** the
  retract, with a single snapshot 0.6 s after `M400`: a sensor crossing that happened
  *during* the move was invisible, and a lane whose arm already rested on the Back side
  (buffer full of slack, typically right after a load or a `LLL_JOG` forward) returned the
  same state before and after. The firmware already emits an `EV_ARM` event on every hall
  transition, but the default telemetry mask (`0x0F`) drops it and the probe never listened.
- Now the probe enables `EV_ARM` on every lane for the duration of the retract (telemetry
  mask `| 0x20`, previous value restored afterwards), **counts the arm events per lane while
  the extruder retracts**, and takes the lane that moved. The before/after state comparison
  stays as a fallback when no event arrives. Roles and masks are restored in a `finally`
  block whatever happens. Diagnosed with 50 ms sampling on the reference printer (events
  `arm:2→3→1→2→3` seen from 9 mm of retract on the loaded lane, none on the empty one) and
  validated on a real print start the same day. No extra extrusion is added and `MM` keeps
  its meaning.

### Note
- A Klipper `RESTART` does **not** reload a modified Python extra (the `Printer` object is
  recreated inside the same process): after updating `lll_buffer.py`, run
  `sudo systemctl restart klipper` with the printer idle.

## v1.0.2 — Feed-gate on lane selection + dashboard unfreeze/check — 2026-07-12

Host-side only (example macros + dashboard): **no firmware rebuild or flash needed**.

### Added
- Example macros: **feed-gate in `SET_ACTIVE_LANE`** — selecting a lane no
  longer arms its autonomous auto-feed while a *different* lane is physically
  loaded (tracked in `lll_loaded_lane`, written by every successful
  `LLL_DETECT_LOADED`). The frozen lane stays back-only (rewind still works,
  forward push impossible) and a console message says so; unknown state (`''`)
  arms as before, so a legitimate flow is never blocked. Field incident behind
  this: activating lane 1 for a test while lane 2 still occupied the merger
  and the extruder armed lane 1's autonomous feed — its buffer arm was sitting
  in the PUSH hall zone, so it immediately fed filament into the occupied
  common path. The same gate also closes the residual "previous print's lane
  feeds during heat-up" window at print start.
- Example macros: **automatic re-arm during a swap** (`_AFC_TEMP_WATCH`) — the
  moment the old filament clears the extruder-entry sensor, `lll_loaded_lane`
  is reset to unknown and the target lane is re-armed, so load+purge gets
  buffer assist (an incoming lane frozen while the extruder pulls alone means
  slip and tangles). One-shot, guarded by `loaded != ''`.
- Example macros: documented RESUME line — call
  `SET_ACTIVE_LANE LANE={printer["gcode_macro _AFC_VARS"].active}` right after
  `_AFC_CHECK` (idempotent, runs only when the check *passed*): without it, a
  lane frozen at print start would stay frozen for the whole print after you
  fix a mismatch by hand.
- Dashboard: **frozen badge + one-click "unfreeze"** on the affected lane card,
  visible only while the gate is actually freezing that lane. Unfreeze is the
  deliberate override: reset the loaded-lane memory to unknown, then re-select
  the lane (which arms it).
- Dashboard: **header "filament check" button** (`LLL_DETECT_LOADED`) and a
  permanent footer status `active lane · loaded lane` — the state driving the
  gate is always visible.
- `LLL_PANEL` (Mainsail dialog): same controls — conditional unfreeze button,
  loaded/active status line, and the probe promoted from a footer button to a
  regular button (some Mainsail set-ups don't render footer buttons).


## v1.0.1 — Pre-purge filament gating — 2026-07-10

Host-side only (extra + example macros): **no firmware rebuild or flash needed**.

### Added
- `LLL_DETECT_LOADED ON_MISMATCH=error`: raises a G-code error on mismatch,
  aborting the queue and the rest of the calling macro. Required to gate a
  purge line from inside a start/resume macro — a plain `PAUSE` does not stop
  macro expansion, so the purge would still run with the wrong filament.
- Example macros: new `_AFC_CHECK` — probes the ACTIVE lane, meant to be
  called after the final `M109` and before the purge line. Failed check
  leaves the print paused; `RESUME` replays heat → check → purge (free retry).
- Example macros: the active lane is now **persisted** (`afc_active_lane` in
  save_variables) and `_LLL_INIT` restores it after a restart instead of a
  hard-coded default. Field incident behind this: after a restart the
  hard-coded default promoted the wrong lane to `active`, and its autonomous
  auto-feed pushed the parked filament tip back into the Y merger (a parked
  lane never feeds forward — the fix keeps the unused lane parked).

### Changed
- Example macros: `_AFC_START` is now pre-selection only (roles + runout
  arming) and should be called as early as possible (forward
  `TOOL=[initial_tool]` through your `PRINT_START`), so the previous print's
  lane is parked before heating. The filament check moved from `_AFC_START`
  (which ran after the purge) to `_AFC_CHECK` (before the purge).

## v1.0.0 — "Canonical" (runtime-toggleable) — 2026-07-02

The autonomous smart-buffer core is unchanged (standalone-parity: same hall
conventions, same TMC2208 `VACTUAL` drive, same feed behaviour). Everything
below is added **on top** of it.

### Added
- **Full Klipper host control** via a new extra (`klippy/extras/lll_buffer.py`):
  21 G-code commands, multiplexed per lane (`LANE=`), N-scalable — one
  `[mcu]` + one `[lll_buffer]` section per board.
- **Roles per lane**: `active` / `parked` / `standby` — a parked lane never
  auto-feeds forward (multi-lane safety), `LLL_SET_IDLE` kept as a
  backwards-compatible alias.
- **4 feed automation levels**: `auto` / `semi_auto` (one-shot `LLL_ARM`) /
  `semi_manual` / `manual`.
- **11 runtime-toggleable functions** (`LLL_ENABLE`): FEED, BUTTONS, MBSIG,
  ENTRANCE, TMC, MOVE, DIAG, LED, ADC, TELEM, IOEXT — one flash, everything
  selectable at runtime.
- **Bounded motion commands**: `LLL_JOG` (≤10 s) and `LLL_RUN` (deadline,
  refreshable) — no unbounded motion order exists in the command set.
- **Runtime TMC tuning** (`LLL_SET`): current, microsteps, stealthChop,
  timeouts… persisted and replayed at every boot.
- **Named profiles**: `LLL_PROFILE_SAVE/LOAD/LIST/DELETE`.
- **Telemetry events** (`lll_event`) with configurable `event_gcode` hook
  (react to jams/runout/diag edges from printer.cfg in ~20–50 ms).
- **IOEXT**: 12-channel index-only GPIO/PWM/ADC register for the spare
  connectors — critical pins are *physically unreachable* (not in the table);
  unverified pins gated behind `LLL_UNLOCK`.
- **Filament identity & probing**: `LLL_FILAMENT` (type/color/temp/spool)
  and `LLL_DETECT_LOADED` (detects which lane is physically engaged in the
  extruder using only the buffer arm sensors).
- **Web dashboard** (`dashboard/`): self-contained HTML page, Moonraker
  WebSocket, lane auto-discovery, VCR-style transport with burst-bounded
  continuous mode — shown inside Mainsail as an iframe webcam card.
- **Spoolman integration** (optional): active lane drives Moonraker's active
  spool.
- **LCD menu example** (`config/lcd_menu_lll.cfg.example`): per-lane state +
  Activate / Park / Clear error.
- **Prebuilt binary** `build/klipper_canonical_v1_fix9.bin` (53 824 bytes,
  build `20260705_234145`, sha256
  `9239bdc367096438e72528d361f75f1ebb811784698cb0872ca79b2410837944`) — the
  exact binary running in production on the reference setup.

### Safety hardening (8 fixes, all in the shipped code)
1. `sched_is_shutdown()` guard as the literal first line of the buffer task,
   plus `DECL_SHUTDOWN` driving the motor enable high — **bench-proven**:
   `M112` during a commanded run cuts the motor dead.
2. Single-writer: command handlers set flags only; only the task and the
   shutdown handler touch EN/UART.
3. Forward run requires filament present && no latched error (checked every
   tick); backward run allowed on runout (legitimate unload).
4. `back_timeout` (default 60 s) on parked follow and commanded back runs.
5. USB/SWD/UART/hall/button pins and the 5 V headers are outside the IOEXT
   table — index-only addressing makes them unreachable by construction.
6. Every stop writes `VACTUAL=0` **before** disabling; every motion rewrites
   GCONF+VACTUAL.
7. Roles/modes/enables are replayed in Klipper's config sequence (before
   `ready`), closing the boot window on Klipper restarts (`role: parked`).
8. No UART bit-bang inside command handlers; TMC writes are deferred and
   spaced in the task.
9. Boot grace period: after a hard MCU reset (`FIRMWARE_RESTART`, `M112`,
   power-cycle), no autonomous feed starts until the host has spoken once
   (20 s cap; done-latch evaluated every tick so it can never re-arm) —
   closes the phantom-push window of the compiled defaults. Field-proven:
   `FIRMWARE_RESTART` with a fully-tensioned arm → zero motion. Deliberate
   standalone trade-off: a never-host-driven board starts auto-feeding 20 s
   after power-on (stock Mellow starts ~1 s after boot).

### Changed vs the predecessor project
- `src/buffer.c` grows from the single-purpose autonomous port (~490 lines)
  to the canonical runtime-toggleable implementation (~1550 lines), protocol
  shared verbatim with the host extra.

### Compatibility
- Same board (Mellow FLY LLL Buffer Plus, STM32F072CB), same Katapult
  bootloader layout (8 KiB offset), same flash procedure.
- Defaults preserve standalone behaviour: a board that is never spoken to
  feeds exactly like the v1.x firmware (buttons included).

## Lineage

This project supersedes
[Mellow-LLL-Plus-Klipper-Firmware](https://github.com/Nitrooxyde/Mellow-LLL-Plus-Klipper-Firmware)
(2026-05-24), whose autonomous smart-buffer core — Mellow's standalone
auto-feed ported into a Klipper MCU task, single USB cable — is embedded
here **unchanged**. That repo remains valid for a minimal, autonomous-only
build. Bootloader install without opening the case:
[LLL-Plus-Katapult-NoDFU](https://github.com/Nitrooxyde/LLL-Plus-Katapult-NoDFU).
