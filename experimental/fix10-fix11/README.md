# Candidate firmware — driver configuration rewritten before every motor start (fix #10 + #11)

> **Status: candidate, NOT the official firmware.** The official, field-validated
> firmware of this repository remains the canonical v1 in `src/` and `build/`
> (fix #9). This folder holds the next firmware step, published so that it can be
> read, built and tried. **This exact build has not been flashed on a board yet** —
> see "What is tested and what is not" below.

## The problem it fixes

The motor driver (TMC2225) is powered from the 24 V input, while the MCU stays
alive on USB. If the 24 V goes away and comes back while USB stays connected, the
driver restarts with its power-on settings, and the MCU, which never restarted,
does not know it.

The official firmware writes the driver configuration only once after boot. So
from then on the motor runs with the wrong microstepping and a higher current: it
turns slowly, has no torque, and the 24 V supply can go into current limit.
Connecting USB before the 24 V at power-up gives the same result.

**Workaround with the official firmware:** after any interruption of the 24 V, do
a `FIRMWARE_RESTART` before moving filament; at power-up, have the 24 V present
before USB, or together with it.

## Why this fix exists

This did not start as a theory. The supply throttling itself is something I had
noticed on my own printer for a long time: after a power interruption, the 24 V
supply of the buffers went into current limit and the motors crawled. I had worked
out that a firmware restart was enough to get out of it, and I lived with that.

Then one day I forgot that detail, and lost a lot of time looking for the cause
everywhere else. The fix was written after that: the firmware should not depend
on someone remembering to restart it.

## What changes

- **Fix #10** — before every motor start (forward or back), the complete driver
  configuration is written again (CHOPCONF, IHOLD_IRUN, PWMCONF, TPOWERDOWN, GCONF),
  and the driver is enabled only after that.
- **Fix #11** — those writes are spread over the firmware task, one write per turn,
  so that no turn holds the main loop (and therefore USB) for more than about 10 ms.
  Done in one block, the rewrite would hold it for 60–70 ms, beyond the 25 ms after
  which the Klipper host starts retransmitting.

Everything else is the canonical v1 source: same state machine, same roles and
modes, same safety fixes #1 to #9. The host protocol is unchanged — the
command/response dictionary of this build is identical to the official one — so
`klippy/extras/lll_buffer.py` is used as is.

Two behaviour differences to know:

- the motor starts about 60 ms after the request (about 20 ms in v1);
- a request from the buttons, from the mainboard signal inputs, from the `parked`
  role or from semi-auto mode that disappears before the arming is finished (about
  50 ms) produces no movement at all.

## What is tested and what is not

- **Tested, but in a different firmware.** These two fixes have been running since
  October 2026 on three LLL Plus boards, inside a different version of this
  firmware made for a splitter project tied to the LLL: it changes filament
  automatically, a bit like an AMS. That project is not published. The problem has
  not come back there. We never ran the dedicated test (cutting the 24 V on purpose
  and measuring the feed speed).
- **Not tested: this exact build.** It is that same `buffer.c` logic put back on
  the canonical v1 source, minus one diagnostic counter that belongs to the other
  firmware. It compiles without warnings and its protocol dictionary is identical
  to the official one, but **it has not been flashed on a board**.
- **Never exercised with fix #11:** the buttons, the `parked` role and semi-auto
  mode. The other firmware does not use them.
- **Build check.** Rebuilding the unchanged v1 source in the same tree gives the
  published binary back, except for the 23 bytes of the build timestamp.

## Files

| File | |
|---|---|
| [`buffer.c`](buffer.c) | candidate source (takes the place of `src/buffer.c`) |
| [`klipper_canonical_fix11_candidate.bin`](klipper_canonical_fix11_candidate.bin) | prebuilt, 54 280 bytes, Klipper `v0.13.0-700` (commit `d6ea62542`), config = [`build/klipper.config`](../../build/klipper.config) |

SHA-256 of the binary:
`d3f45c26b584d16c98c76ed67eb77fa9059784caade0b200439eb3deb29dcf5e`

## Trying it

Build it yourself (main README, Steps 5 and 6, with this folder's `buffer.c`) or
take the prebuilt binary, then flash over USB as in Step 7. To go back, flash
[`build/klipper_canonical_v1_fix9.bin`](../../build/klipper_canonical_v1_fix9.bin)
the same way.

The check that matters: with USB connected, cut the 24 V, restore it, and start a
feed **without any restart**. The speed should be normal and the supply should
stay out of current limit.

If you try it, a field test report — working or not — is very welcome (main
README, "We're asking for your feedback").

Flashing firmware is at your own risk: see "Status / disclaimer" in the main
README.
