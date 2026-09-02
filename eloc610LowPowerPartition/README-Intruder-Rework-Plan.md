# Intruder Detection Rework — Handoff Brief

> **Status:** **implemented** in V1.73, extended in V1.74. Build-verified only — not on hardware.
> **Date:** 2026-09-01, revised 2026-09-02
> **Now serves as:** the bench plan (the test table below) and the design rationale.
> **Baseline:** V1.72, committed as `37071a2`.

> ⚠️ **The survey and analysis below describe the code as it was *before* V1.73.** They are kept
> because the reasoning is still what the implementation rests on. Two things have since changed and
> are called out inline: the knock counter now trips at six (it is fixed), and the siren now *does*
> sound on a candidate (V1.74) with the timing reworked so it no longer blinds the confirmation.

Goal: stop the intruder alarm firing on knocks alone, gate GPS and LoRa on *confirmed movement*, and
keep the heartbeat independent of all of it.

---

## Before you start

**V1.72 (the LoRa signal survey) is uncommitted and hardware-untested** in this working tree. It
touches `ElocLora.hpp/.cpp`, `ElocConfig.hpp/.cpp`, `ElocCommands.cpp` and `main.cpp` — the same files
this rework touches. **Commit or stash it first**, or the two changes tangle into one diff that cannot
be bisected when the bench test finds something.

Also note `keyfile.csv` and `nvs.csv` show as modified after any build. Never stage them.

---

## What already works (do not rebuild these)

Three of the six requirement areas are partly or wholly implemented. Reading the code before changing
it matters more than usual here.

### GPS is already motion-gated during an alarm

`src/main.cpp`, `manageGpsWhileAwake()` (~line 1172):

```cpp
if (ElocSystem::GetInstance().isIntruderDetected()) {
    if (ElocSystem::GetInstance().isDeviceMoving()) {
        if (!gps.isInitialized()) { gps.init(); }      // powering GPS on for location tracking
    }
    else if (gps.isInitialized()) { gps.deinit(); }    // parked, powering GPS down
    return;
}
```

**Requirement 2 is therefore already satisfied in structure.** What is wrong is only the timing
constant: `C_MOTION_QUIET_MS = 5*60*1000` in `ElocSystem.cpp` (~line 769). You want 30–60 s. That is a
constant change plus making it configurable — not new machinery.

### The heartbeat is already independent

`ElocLora::ElocLoraLoop()` does **not** return early after the intruder block — it falls through to the
event check and then the heartbeat. The two schedules use entirely separate clocks:

| Schedule | State | Storage |
|---|---|---|
| Intruder uplink | `mNextIntruderUplinkS` | RAM, epoch seconds |
| Heartbeat | `rtc_duty_cycle.lastStatusLoraTimeS` | RTC memory, survives deep sleep |

So an intruder uplink cannot postpone a heartbeat today. **Requirement 5's concern does not exist in
the current code** — do not "fix" it. Two real items remain under that heading:

1. `loraConfig.upLinkIntervalS` **defaults to 86400** (24 h), not 3600. Wanting an hourly heartbeat is
   a default change, and `C_MIN_UPLINK_INTERVAL_S` is 10 so 3600 is accepted fine.
2. **Survey mode does suspend the heartbeat.** V1.72's `surveyLoop()` short-circuits `ElocLoraLoop()`
   and returns. That is deliberate (a survey is a handheld mode, and nothing should compete for its
   airtime budget) but it means "heartbeat always" needs the caveat "except while surveying".

### The alarm is already latched

`mIntruderDetected` is set in `notifyStatusRefresh()` and cleared in exactly two places, both in
`ElocSystem.cpp`:

* `handleSystemStatus()` (~line 554) — when `intruderCfg.enable` goes false, or duty-cycle mode turns
  on. There is a comment explaining why: `notifyStatusRefresh()` only runs on a knock, so without this
  an alarm would stay latched forever after detection was disabled.
* `notifyStatusRefresh()` (~line 661) — same condition, on the next knock.

**There is no timeout and no explicit reset command.** So requirement 4's latching already holds; the
gap is only the *clearing* half — a `setIntruderReset` command and/or an agreed long timeout.

---

## What is missing or wrong

### The structural blocker: there is no motion state before an alarm

`ElocSystem::updateMotionState()` opens with:

```cpp
if (!mIntruderDetected) {
    mLastMotionMs = 0;   // "Nothing consumes the state outside an alarm"
    return;
}
```

Motion is only sampled **while an alarm is already active**. Requirement 1 needs the opposite:
*sample motion during a candidate window, and only promote to an alarm if movement is seen.* That
inversion is the main body of work.

Concretely, `updateMotionState()` must run whenever there is a candidate **or** an alarm, and
`mLastMotionMs` must not be zeroed on entry to the candidate state — but it also must not be
seeded as "moving" the way it currently is on alarm entry:

```cpp
// A device that has just been knocked is by definition being handled: assume movement until it
// has actually been still, otherwise the first sample would park it and power the GPS down.
if (mLastMotionMs == 0) { mLastMotionMs = nowMs ? nowMs : 1; }
```

That optimistic seed is correct for today's design and **fatal for the new one** — it would confirm
every candidate immediately, including the device sitting on a table. The candidate path needs a real
"has moved since the knock" observation, not an assumption.

### The buzzer blinds the accelerometer — and the siren runs 30 s

`C_BUZZER_KNOCK_GUARD_MS` (1 s) suppresses **both** the knock counter and `updateMotionState()` while
the buzzer sounds and for a second after. `updateIntruderSiren()` sweeps 600→1800 Hz for
`C_INTRUDER_SIREN_DURATION_MS = 30 s`.

So **if the siren fires on the candidate, movement can never be confirmed** — the confirmation window
would be entirely inside the guard.

> **Resolved differently in V1.74.** EDsteve wanted the candidate to announce itself after all: even a
> monkey playing with a device is worth reporting. So the siren *does* sound on a candidate now, and
> the conflict is settled by **starting the listening window only after the siren and the guard are
> over** rather than by keeping the siren silent:
>
> ```cpp
> const uint32_t listenFromMs =
>     mCandidateStartMs + C_INTRUDER_SIREN_DURATION_MS + C_BUZZER_KNOCK_GUARD_MS;
> ```
>
> Total candidate lifetime is therefore ~31 s + `confirmWindowS`. Nothing is missed: someone carrying
> the device away is still moving 30 s later. The siren runs **once per episode** and is not restarted
> on promotion to CONFIRMED — a second 30 s of blinding would land exactly when the tracking uplinks
> start depending on `isDeviceMoving()`. Test case 2b exists to catch a regression here.

The same coupling is why requirement 1's "wait briefly for the knock vibrations to settle" is
genuinely necessary and not just politeness: the knocks themselves register as movement.

### The knock threshold is off by one from the spec

`notifyStatusRefresh()`:

```cpp
static uint32_t lastRefreshMs = 0;
static uint32_t cntFastUpdates = 0;
...
if ((millis() - lastRefreshMs) <= cfg.detectWindowMS) { cntFastUpdates++; }
else { cntFastUpdates = 0; }
if ((cntFastUpdates > cfg.thresholdCnt) && (cfg.thresholdCnt != 0)) { mIntruderDetected = true; }
```

`lastRefreshMs` starts at 0, so the **first** knock takes the `else` branch and sets the counter to 0.
Each later in-window knock increments. With the default `thresholdCnt = 5` (`INTRUDER_DETECTION_THRSH`
in `include/project_config.h`):

| Knock | `cntFastUpdates` after | Trips? |
|---|---|---|
| 1 | 0 | no |
| 2–6 | 1–5 | no |
| **7** | **6** | **yes** (`6 > 5`) |

**It took seven knocks, not six.**

> **Fixed in V1.73:** a burst now starts the run at 1 rather than 0, so `thresholdCnt = 5` means six
> knocks, as the field name and the wiki always claimed. Devices upgrading become one knock more
> sensitive. Still worth confirming on hardware — the reasoning above is from reading the code.

### Every knock is a hardware double-tap

`lib/ElocHardware/src/config.cpp` (~line 71) configures the LIS3DH with `z_double = true` and every
single-tap and other-axis option `false`. So one "knock" as counted here is already a *double* tap on
the z-axis within `time_window` (255 / 400 Hz ≈ 640 ms). Worth stating in the wiki, since "six knocks"
means six double-taps.

---

## Target state machine

```
        IDLE
          │  knocks > threshold within detectWindowMS
          ▼
      CANDIDATE ──────────── settle window (knock vibration decays;
          │                   accelerometer readings ignored)
          │
          ├── movement seen within confirmWindowS ──► CONFIRMED
          │
          └── no movement, window expires ──────────► IDLE
                                                      (no siren, no GPS, no LoRa)

      CONFIRMED  (latched — survives being put down)
          │
          ├── moving   → GPS on,  intruder uplinks every alarmIntervalS
          ├── stopped  → GPS off, NO intruder uplinks
          ├── moves again → immediate uplink (do not honour the old deadline)
          └── cleared only by: explicit reset cmd | intruderCfg.enable=false | long timeout
```

Three timings, all currently hard-coded or absent:

| Timing | Now | Wanted |
|---|---|---|
| Knock settle before sampling motion | none (only the 1 s buzzer guard) | new, ~1–3 s |
| Candidate → confirm window | none | new, ~10–30 s |
| Moving → stopped quiet period | `C_MOTION_QUIET_MS` = 5 min | 30–60 s, configurable |

---

## Files to change

| File | What |
|---|---|
| `lib/ElocHardware/src/ElocSystem.hpp/.cpp` | The state machine. `notifyStatusRefresh()` promotes to CANDIDATE instead of setting `mIntruderDetected`. `updateMotionState()` must run for CANDIDATE too, without the optimistic seed. New accessors for the app/status. `updateIntruderSiren()` fires on CONFIRMED only. |
| `lib/ElocHardware/src/ElocLora.cpp` | `ElocLoraLoop()` intruder block: send on confirmation, suppress entirely while stopped, send immediately on movement resuming (clear `mNextIntruderUplinkS` rather than letting an old deadline stand). `sendIntruderAlarmMessage()` already carries a "moving" flag (bit 1) and a "GPS fix" flag (bit 0) — **the "moving, location unavailable" packet requirement 3 asks for already encodes correctly today**, no payload change needed. |
| `src/main.cpp` | `manageGpsWhileAwake()` — the intruder block already does the right thing; it will follow the new `isDeviceMoving()` semantics for free once the quiet period changes. |
| `lib/ElocHardware/src/ElocConfig.hpp/.cpp` | New `intruderCfg` fields (settle/confirm/quiet). Decide `idleIntervalS`'s fate. Watch `JSON_DOC_SIZE` — raised to 4096 for V1.72's `surveyCfg`; more fields may need another look. |
| `lib/Commands/src/ElocCommands.cpp` | A reset command, if you take that route. `printStatus()`'s `intruder` section needs the candidate/confirmed distinction. |
| App (`driver/Intruder.kt`, settings UI) | Any new config fields, and the status distinction. |

### `idleIntervalS` — requirement 6

Used today in `ElocLoraLoop()` to slow the alarm cadence when parked. Under the new rules a stopped
device sends **nothing**, so it has no radio meaning left. Options:

* **Deprecate cleanly** — accept and ignore it in `setConfig`, drop it from `getConfig`, remove from
  the app UI, mark it deprecated on the wiki. Cleanest, breaks nothing.
* **Repurpose** it as the stopped/quiet period. Tempting (the name almost fits) but the semantics
  differ enough — one was a transmit interval, one is a motion timeout — that a reader would be
  misled. Prefer a new field.

Either way this has a **documentation tail**: `idleIntervalS` is on the wiki in two places, the
[[Intruder Detection]] page and the `config.intruderCfg` table on [[Settings ‐ Config & Status]]. The
wiki rule says same-pass.

---

## Test plan, as pass/fail

Run in 24/7 mode (`dutyCycle.enable = false`) with `intruderCfg.enable = true`, on a bench with LoRa
joined and serial attached. Note which knock count you settled on above.

| # | Action | Pass | Fail signature |
|---|---|---|---|
| 1 | 5 knocks (or one below threshold), device still | No candidate, no log entry, no siren | Any `Intruder detected` line |
| 2 | Threshold knocks, device left flat on the table | *(V1.74)* Candidate logged, siren sounds 30 s, **exactly one** msgType-2 uplink, **no GPS power-up**. Candidate then expires and the device returns to its previous mode. | A second uplink; `powering GPS on` appears; or the candidate never expires |
| 2b | *(V1.74)* Threshold knocks, then pick the device up **during** the siren | Still confirms — the listening window starts after the siren ends, and someone carrying it is still moving then | Candidate expires despite the device being carried. This is the case the siren timing could break |
| 2c | *(V1.74)* Start recording, then knock immediately | Nothing at all for `armDelayS`; `getStatus` shows `armed:false` with `armsIn[s]` counting down | Knocks register during the grace period |
| 3 | Threshold knocks, then pick the device up and carry it | Candidate → CONFIRMED. Siren sounds. `powering GPS on`. **Intruder uplink attempted immediately**, not after the first fix | Uplink waits for a GPS fix |
| 4 | As (3) but indoors, no fix obtainable | Uplink still goes, flags byte = moving set, GPS-fix bit clear, lat/lng zero | No uplink at all |
| 5 | Keep moving for 5 min | Uplink every `alarmIntervalS` (60 s), each with a fresher fix once one lands | Cadence drifts or stalls |
| 6 | Put it down, wait past the quiet period | `parked, powering GPS down`. **Intruder uplinks stop entirely** | Uplinks continue at any cadence |
| 7 | Pick it up again | GPS back on and an uplink **immediately** — not at the next 60 s boundary | Waits out the stale deadline |
| 8 | Leave it still for 3 h | Exactly one heartbeat per `upLinkIntervalS`, and **no GPS power-up for the heartbeat** | GPS powers on hourly; or heartbeats missing |
| 9 | Explicit reset (command / `enable=false`) | Alarm clears, siren silent, GPS released | Alarm survives |

Two more that are not pass/fail but must be recorded:

* **Current draw in each state** — idle, candidate, confirmed+moving, confirmed+stopped. The whole
  point of the rework is that confirmed+stopped costs nearly nothing.
* **GPS behaviour under canopy** — time to first fix during a real carry-away, which determines
  whether the "moving, location unavailable" packet in test (4) is the common case or the rare one.

---

## Conventions for this repo

* Build: `pio run -e esp32dev-ei`. Never plain `esp32dev` for a release build.
* Bump `VERSION` in `include/project_config.h` by +0.01 per push **and** add a line to
  `VERSIONS.md`, newest first. (Note: V1.69–V1.71 are missing from that table — they exist only in
  `memory-bank/activeContext.md`. Not this change's job to fix, but do not add to the gap.)
* Update `memory-bank/activeContext.md` and `progress.md` when the work lands; move finished streams
  to `changelog.md`.
* Wiki: `https://github.com/LIFsCode/ELOC-3.0.wiki.git`, edit and `git push origin master`. Anything a
  ranger would notice gets updated in the same pass.
* Config changes that only apply at boot go on the restart list in
  `README-Config-Restart-Semantics.md` and the app's `restartRequiredProperties`.

## Related reading

* `README-Config-Restart-Semantics.md` — live vs reboot for each `setConfig` field
* `memory-bank/systemPatterns.md` — task model, which core runs what
* Wiki: [[Intruder Detection]], [[Settings ‐ Config & Status]], [[Status LEDs and Buzzer]]
