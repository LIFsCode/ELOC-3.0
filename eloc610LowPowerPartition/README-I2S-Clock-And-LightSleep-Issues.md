# I2S clock, DFS timebases and light sleep — investigation + resolution

Started 2026-07-25 on V1.55 while bench-testing duty cycle; **resolved 2026-08-01 in V1.59.**

Three independent bugs that share one symptom (the device misbehaves in low-power recording modes)
but have different causes. All three are now understood; two are fixed, one is mitigated.

**The headline result:** the "I2S sample rate wrong … audio is unusable" error that this document
originally treated as a real audio fault was a **false alarm**. The audio was always correct. What
was broken were the two software clocks used to measure it. See Problem 3.

| | Status |
|---|---|
| Problem 1 — I2S runs 8× too fast after a deep-sleep wake | Root-caused, two fixes, needs a long soak |
| Problem 2 — RTCWDT reboots from automatic light sleep | Root-caused; light sleep now disabled and clamped |
| Problem 3 — DFS corrupts `esp_timer` **and** the FreeRTOS tick | **RESOLVED** — DFS removed from the low-power paths |

---

## Problem 1 — I2S runs at exactly 8× the configured sample rate after a deep-sleep wake

**Status: root-caused. Two independent fixes, both verified on hardware. Needs a longer soak.**

### Symptom

First boot records fine. After the first duty-cycle timer wake, the log floods with clipping and
buffer-overrun warnings and the device eventually reboots. `log22.txt`:

```
W (35714) I2SMEMSSampler: Audio sample clips occurred 141 times
W (35725) I2SMEMSSampler: Audio sample clips occurred 859 times
...
W (36002) I2SMEMSSampler: inference buffer overrun
```

Nothing reports an error. The driver's own clock log looks perfectly healthy.

### Measurement that identified it

Clip warnings arrive at a steady 12–14 per 100 ms for the whole captured second — no decay, so not a
mic settling transient. 115 warnings in 915 ms × 1024 samples per read = **~128,000 samples/s against
a configured 16,000**. Exactly 8.0×.

### Root cause

For this configuration (16 kHz, 32-bit, 2 slots, APLL) the IDF 4.4.7 driver builds the clock as:

```
APLL 8.192 MHz  --mclk_div=2-->  mclk 4.096 MHz  --bclk_div=4-->  BCK 1.024 MHz  = 16 kHz
```

`2 × 4 = 8`. After a deep-sleep wake neither divider is in effect — BCK runs straight off the APLL,
which is precisely 8× too fast. The ICS-43434 is then clocked far outside spec, so 30–100 % of
samples saturate. Relevant IDF code: `driver/i2s.c` `i2s_calculate_common_clock()`.

The divider chain above was independently confirmed in 2026-08 by reproducing the driver's clock
maths against a healthy boot log (`multi = 256`, `chan_num = 2`, `chan_bit = 32` ⇒ `bclk_div = 4`;
APLL 8.192 MHz ⇒ `mclk_div = 2`).

### Fix A (config only) — `MicUseAPLL: false`

Verified clean on hardware (`logAPLLoff.txt`, boot #7): zero clip warnings and zero buffer overruns in
the entire capture.

At 16 kHz this costs nothing in audio quality: PLL_D2 160 MHz through the fractional MCLK divider
gives `39 + 1/16` — **exactly 16000.000 Hz**. Verified by reimplementing
`i2s_hal_mclk_div_decimal_cal()`; 32 kHz and 48 kHz are likewise exact, and 44.1 kHz lands at
44099.76 Hz (−5.5 ppm), i.e. clock accuracy is a non-issue at any of these rates with APLL off.

It also has a *second* effect that matters for Problem 3: the driver picks its PM lock from
`use_apll` (`driver/i2s.c`) — `ESP_PM_NO_LIGHT_SLEEP` with APLL (which maps to `PM_MODE_APB_MIN` and
therefore *allows* DFS down to the min frequency), `ESP_PM_APB_FREQ_MAX` without (which pins APB, and
therefore the CPU, at 80 MHz and **disables DFS entirely during recording**).

### Fix B (code) — re-apply the clock after install

`I2SMEMSSampler::install_and_start()` calls `i2s_set_clk()` with exactly the install values after
`i2s_set_pin()`. That stops the peripheral, recalculates the dividers and rewrites the registers.
No-op on a healthy boot.

### Caveats

- The re-apply runs **once, at install**. If something later disturbs the dividers it will not help.
- `i2s_set_clk()`'s return value is logged but not acted on, and the sampler is marked started
  regardless. Reviewed 2026-08-01 and **deliberately left as is**: with the arguments used here (the
  exact values the driver was just installed with) both of its guard clauses are excluded by
  construction, and returning the error *without* first calling `i2s_driver_uninstall()` would wedge
  the "keep trying until successful" loop in `main.cpp` permanently — a worse failure than the one it
  guards against.
- **No return code can detect this fault class.** The 8× fault reported `ESP_OK` everywhere. Note
  also that `i2s_get_clk()` returns `p_i2s[num]->hal_cfg.sample_rate` — the driver's stored *intent*,
  not a register readback — so the `I2S clock configured: 16000 Hz` log line would print identically
  during the fault. The only real detector is the sample-rate meter (Problem 3).

---

## Problem 2 — `RTCWDT_RTC_RESET` reboots caused by automatic light sleep

**Status: root-caused. Light sleep now off by default and clamped in code. Underlying hang unexplained.**

### Symptom

Two manifestations, same signature — no Guru Meditation, no backtrace, no task-watchdog warning:

1. **Intermittent** — roughly 1 duty-cycle wake in 8, during the 30 s GPS time-sync wait.
2. **Deterministic** — every time, when left idle, immediately after the Bluetooth idle timeout.

### Root cause

Only four things arm the RTC watchdog in this build, and three are excluded (bootloader WDT is
disabled during startup; the panic handler would have printed a backtrace; `resetESP32()` logs first).
That leaves `esp_light_sleep_start()` (`esp_hw_support/sleep_modes.c`), which is literally commented
*"Safety net: enable WDT in case exit from light sleep fails"* — 1000 ms,
`WDT_STAGE_ACTION_RESET_RTC`, which is exactly reset reason `0x10`. The 1 s gap between the last log
line and the reset matches.

**So: the device hangs inside an automatic light-sleep entry/exit and the safety net resets it.**

### Why only in those two windows

Light sleep can only engage when *all* hold: Bluetooth down (the BT controller holds
`ESP_PM_NO_LIGHT_SLEEP`), not recording (I2S holds a lock either way), and LoRa disabled. That leaves
exactly two windows: idle after the BT timeout, and the duty-cycle GPS wait.

### Severity — worse than a reboot

An RTC WDT reset is not a deep-sleep wake, so the next boot takes the `default:` branch in
`getWakeupCause()` → `SLEEP_CYCLE_DISABLED`, and `main.cpp` **memsets the RTC duty-cycle state**. The
unit comes back with BT on, **not recording and not duty cycling** — a silently dead node in the field.

### Resolution (V1.59)

- `C_ElocConfig_Default.cpuEnableLightSleep` → `false`.
- New compile-time flag `ALLOW_AUTOMATIC_LIGHT_SLEEP` (0) in `project_config.h`, enforced by
  `clampLightSleep()` in `ElocConfig.cpp`.

The clamp is **required, not belt-and-braces**: the config cascade is SD → SPIFFS → compile-time
default, and a key that is *present* always wins over the default. Every unit provisioned before this
change still carries `"cpuEnableLightSleep": true` in its stored config, so changing the default alone
would not have touched a single deployed device. `clampLightSleep()` is called from `loadConfig()`
(which the app's `setConfig` also funnels through, via `updateConfig()`) and from
`setCpuFrequencyConfig()`, which the app calls directly and would otherwise bypass it. No eager
migration write is needed — the clamped value is what gets serialised, so the first `writeConfig()`
from any config change rewrites the stale `true` on its own.

The flag exists so the behaviour stays testable: **the underlying hang is still unexplained.** On this
board VDD_SDIO carries both the PSRAM and the SDIO card and light sleep power-cycles that rail — prime
suspect, never investigated. See also open issue
[#118](https://github.com/LIFsCode/ELOC-3.0/issues/118) ("Implement light sleep between inference
runs"), which is on a collision course with this finding.

---

## Problem 3 — DFS corrupts `esp_timer` *and* the FreeRTOS tick — **RESOLVED (V1.59)**

**The audio was never wrong. Both software clocks were.**

### What the old version of this document got wrong

It reported a *"~8 % audio deficit"* under `RECORDING_LOW_POWER` and listed three candidate causes it
could not separate, because *"the rate meter and the log timestamps share the same `esp_timer` time
base."* **They do not.** That assumption is what made the problem look unsolvable, and it was wrong:

```
CONFIG_LOG_TIMESTAMP_SOURCE_RTOS=y      # log stamps = FreeRTOS tick = CPU cycle counter (CCOMPARE)
CONFIG_ESP_TIMER_IMPL_TG0_LAC=y         # esp_timer  = TG0 LACT timer, divided from APB_CLK
CONFIG_FREERTOS_SYSTICK_USES_CCOUNT=y
```

Two different clocks, both rescaled on every DFS transition by `on_freq_update()`
(`esp_pm/pm_impl.c`) — the LACT divider for `esp_timer`, `_xt_tick_divisor` plus a CCOMPARE reprogram
for the tick — through **different correction paths**. So they drift apart from each other, which is
precisely what makes them separable.

### The measurement (2026-08-01, ELOC_00150, 2-day `recordOn_detectOff` run)

Two independent ways of measuring tick-vs-`esp_timer` agree exactly:

| Measurement | Expected (`esp_timer`) | Seen in tick-time | ratio |
|---|---|---|---|
| Rate-meter window (`RATE_WINDOW_US` = 5 s) | 5000 ms | ~4380 ms | **0.876** |
| GPS burst timeout (`GPS_TIME_SYNC_TIMEOUT_S` = 30 s) | 30000 ms | 26268 ms | **0.876** |

WAV byte counts are exact (`secs = m_file_size / 32000`), giving an independent audio-duration meter:

| | audio vs tick-time | × 0.876 | rate meter reported |
|---|---|---|---|
| t≈2.54 Ms | 0.817 | 0.716 | 0.706–0.714 ✓ |
| t≈15.94 Ms | 0.819 | 0.717 | 0.712 ✓ |
| t≈59.05 Ms | 0.809 | 0.709 | 0.700–0.706 ✓ |

Before the PM profile applies (CPU still 240/240) all three agree at 1.00×. Divergence starts exactly
at `Applying PM profile RECORDING_LOW_POWER`.

### The decisive evidence — WAV filename timestamps

`createFilename()` uses `timeObject.getDateTimeFilename()` → `gettimeofday()`. This build sets **only**
`CONFIG_ESP_TIME_FUNCS_USE_RTC_TIMER` (no `..._USE_ESP_TIMER`), so that resolves to
`esp_rtc_get_time_us()` — the RTC slow clock, a **third timebase that DFS cannot touch**. Each file
closes at exactly `16000 × 3600 × 2` B = 3600 s *of audio*, so the wall-clock gap between consecutive
filenames is exactly how long an hour of audio took in reality:

```
..._2026-07-31_10-50-21.wav   ->  ..._2026-07-31_11-50-20.wav    3599 s
..._2026-07-31_12-50-18.wav   ->  ..._2026-07-31_13-50-16.wav    3598 s
..._2026-08-01_00-50-21.wav   ->  ..._2026-08-01_01-50-24.wav    3603 s
..._2026-08-01_06-50-40.wav   ->  ..._2026-08-01_07-50-43.wav    3603 s
```

**21 files × 3600 s = 75,600 s of audio in 75,622 s of wall clock — 0.03 % off.**

### Verdict

| Clock | Source | Error under 10/80 MHz DFS |
|---|---|---|
| Audio (I2S) | 40 MHz XTAL → APLL / PLL_D2 | **correct** (0.03 %) |
| `esp_timer` | TG0 LACT ÷ APB | **+41 % fast** (~9.8 h gained/day) |
| FreeRTOS tick (all `ESP_LOGx` stamps) | CPU cycle counter | **+23 % fast** (~5.4 h/day) |
| Wall clock (`gettimeofday`, filenames) | RTC slow clock | unaffected by DFS |

DFS cannot touch the audio: I2S is clocked from the APLL (or PLL_D2), and `esp_pm` only manipulates
CPU_CLK/APB_CLK. The only way to lose samples is DMA overrun, which logs `wav buffer overrun` /
`Partial I2S read` — absent in steady state.

### Consequences that were *not* cosmetic

- Anything scheduled off `esp_timer` fires ~41 % early. **The GPS acquisition window is the one that
  hurts:** on a duty-cycle timer wake the profile is applied in `main.cpp` *before* the GPS
  time-sync wait and long before I2S installs, so a nominal 30 s window became ~21 s of real time and
  the patient 180 s cold-start window became ~128 s — below the ~30–35 s a cold GNSS fix needs.
- Every log timestamp is wrong, so the SD log cannot be used for timing analysis.
- Console output is garbled despite `configureConsoleUartRefTick()`.

### Resolution (V1.59)

1. **`RECORDING_LOW_POWER` → fixed 80 MHz** (`min_freq_mhz` 10/20 → 80) in `ElocSystem::pm_configure()`.
   The `>= 30 kHz ⇒ min 20` conditional is gone; it only ever existed to raise the min.
2. **`C_ElocConfig_Default.cpuMinFrequencyMHZ` → 80**, so `CONFIG_DEFAULT` is DFS-free too. This
   matters because `CONFIG_DEFAULT` is the profile in force during the duty-cycle GPS wait.
   *Deployed units keep their stored value* — unlike light sleep this is **not** clamped, because it
   is a validated, app-exposed setting and clamping would make the app's picker lie. Change it per
   device from the app.
3. **The rate meter now measures against `esp_rtc_get_time_us()`**, not `esp_timer_get_time()` —
   the only DFS-immune monotonic base (and unlike `gettimeofday()` it cannot be stepped by an app or
   GPS time-set). The clip-warning throttle in `read()` was moved to the same base, since it is
   seeded from the meter's window start. Header is `esp32/rtc.h` (**not** `soc/rtc.h`, which is the
   unrelated soc-component `rtc_clk` API).

Cost: only the idle dip from 80 down to 10 MHz. The 80-vs-240 MHz win is kept.

### This closes a two-year-old open question

Issue [#77](https://github.com/LIFsCode/ELOC-3.0/issues/77) ("Recording time value is off", closed
2024-04) is the same bug. Its stopwatch data:

| Real | Reported | Ratio |
|---|---|---|
| 5 min | 7 min | 1.40 |
| 15 min | 18 min | 1.20 |
| 60 min | 76 min | 1.27 |

Those are the 1.408× (`esp_timer`) and 1.227× (tick) skews measured above. The thread also contains
`esp_timer/gettimeofday = 1.416` in one sample and `1.006` in another, unexplained at the time. The
apparent randomness that defeated that investigation is **load dependence**: the skew scales with the
fraction of time spent at the minimum CPU frequency, so AI on/off and BT connected/disconnected each
change it.

The thread got a lot right — OOHehir identified that the console uses the FreeRTOS tick, and LIFsCode
moved `gettimeofday` onto the RTC clock in
[ffbfa34](https://github.com/LIFsCode/ELOC-3.0/commit/ffbfa34a93b67261220cdbeb4eb522a036f60025)
specifically to dodge DFS (which is why filename timestamps have been trustworthy since). LIFsCode
even proposed this exact fix — *"Could be checked by setting Min & max Freq equal to 80 in the PM
config"*. It was not adopted because the closing summary recorded the opposite conclusion:

> 2. DFS (Min. & max. CPU Frequency settings) did not have a major impact on timing accuracy
> 5. it is still unclear why `esp_timer_get_time` and `gettimeofday` show different accuracies

Point 2 is wrong; point 5 is answered above.

---

## Residual: wall-clock drift (not a DFS issue)

The RTC slow clock is `CONFIG_ESP32_RTC_CLK_SRC_INT_8MD256` — the internal 8 MHz RC divided to
31.25 kHz, calibrated against the crystal **once at boot** and never again on a continuous run. From
the ELOC_00150 filename data:

| | wall clock vs real |
|---|---|
| Hot afternoon (~35–40 °C) | −2 s/hour |
| Cool night (~29 °C) | +4 s/hour |
| Net over 21 h | +22 s ≈ **25 s/day** |

Drift is smallest near the boot temperature and grows away from it, so **booting a unit near its
average deployment temperature measurably reduces net drift**. `todo.txt` independently recorded
"Patrol mode: after 12 hours → 30 sec time drift due to RTC clock", the same order of magnitude.

There is **no 32.768 kHz crystal** on ELOC 3.0 (confirmed in the #77 thread), so
`CONFIG_ESP32_RTC_CLK_SRC_EXT_CRYS` is not available and `INT_8MD256` is already the best internal
option. Without a GPS module the only correction is an app `setTime` on each visit.

---

## Also fixed in V1.59

**GPS-absent detection.** `chars=0` in the GPS status line means something quite different from "no
fix": a powered GNSS receiver streams NMEA from power-up regardless of sky view (RMC status 'V', GGA
fix quality 0), so a no-signal install still yields tens of thousands of parsed chars — verified on
ELOC_00168, which climbed 7773 → 16119 → 25761 indoors with `sentences=0`. A count stuck at 0 means
nothing is on the UART at all. ELOC_00150 ran two days at exactly 0; the module was simply not fitted.
New `GPS_ABSENT_AFTER_SILENT_BURSTS` (3) declares the module absent and stops re-powering the rail for
the rest of the boot. `charsProcessed()` is cumulative, so one non-zero reading ever means it can never
fire — no per-burst delta needed. **Known limitation:** the flag is per-boot, so in duty-cycle mode
every timer wake restarts the count (the boot path powers GPS before `manageGpsWhileAwake()` runs).
Making it stick would need a field in `rtc_duty_cycle`, i.e. an RTC struct layout change.

**Two `WAVFileWriter` bugs.** `longestWriteMs` was a max-tracker seeded with `UINT32_MAX`, so its `>`
test was permanently false and the `WorstCase` field printed a constant `4294967295 ms` for the
lifetime of the build. And the `speed` calculation divided by `writeDurationMs` unguarded — Xtensa
traps on integer division by zero, so any sub-millisecond write (a small write absorbed by the FAT
cache) would have panicked the `wav_writer` task.

**`Applying PM profile …` now logs after the LoRa override**, so it reports what was actually applied
rather than what was requested.

---

## Open items, in priority order

1. **Hardware-verify V1.59** in `recordOn_detectOff`. Expect `CPU max 80 MHz, min 80 MHz, light sleep
   off`, a steady `I2S measured sample rate: ~16000 Hz` with no rate errors, a real number in
   `WorstCase`, and no console garbling.
2. **Soak APLL=true + the Problem 1 re-apply** over many duty cycles; every wake should log
   `I2S measured sample rate: ~16000 Hz`.
3. **Root-cause the light-sleep hang** (VDD_SDIO / PSRAM / SDIO suspect) before anyone acts on #118.
4. **Comment on #77** so the root cause is recorded where the next person will look.
5. **Consider persisting the GPS-absent flag** in `rtc_duty_cycle` if GPS-less units are built often.

## Working-tree note

`keyfile.csv` must never be committed.
