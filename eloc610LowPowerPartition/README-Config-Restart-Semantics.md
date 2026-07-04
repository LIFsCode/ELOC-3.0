# setConfig Fields: Which Ones Need a Reboot?

`setConfig` (see `ElocCommands.cpp`) merges new JSON into the running `gElocConfig`/`gMicInfo` structs
and persists it to SD/SPIFFS immediately. But merging the struct is not the same as applying it: several
subsystems only read their config once, at boot, into a local variable or a hardware register. Changing
those fields over Bluetooth updates the file that will be read *next boot*, but has no effect on the
device that is currently running until it is rebooted (power cycle, watchdog reset, or duty-cycle wake).

This file records which fields fall into which bucket, based on tracing each field from
`ElocConfig.cpp::loadConfig()`/`loadMicInfo()` to its point of use.

## Requires a reboot to take effect

| Field(s) | Why | Evidence |
|---|---|---|
| `cpuMaxFrequencyMHZ`, `cpuMinFrequencyMHZ`, `cpuEnableLightSleep` | Applying a CPU frequency change relocks the ESP32 BBPLL (240 MHz uses the 480 MHz PLL, 80/160 MHz the 320 MHz PLL). The Bluetooth radio is clocked off that PLL, so switching it while BT is up — which it always is while a `setConfig` is being handled — crashes/WDT-resets the device. Therefore `pm_configure()` is run **once, early in `setup()`** (before LoRa/BT start) and forces the switch there via a momentary `ESP_PM_CPU_FREQ_MAX` lock; `setConfig` only validates + persists the value. | `main.cpp` (`pm_configure()` right after `readConfig()`), `ElocSystem.cpp` (`pm_configure()`) |
| `mic.MicSampleRate`, `mic.MicUseAPLL`, `mic.MicChannel`, `mic.MicVolume2_pwr` | `input.init()` programs the I2S peripheral and snapshots the gain bit-shift into a member variable (`I2SMEMSSampler::volume2_pwr`). Called once at boot; `updateI2sConfig()` on a live `setConfig` only updates the in-memory `i2s_mic_Config` struct, not the running driver. Making this live would require an I2S driver uninstall/reinstall, which is unsafe while a recording/inference is running. | `main.cpp:1321`, `I2SMEMSSampler.cpp:25-39,224` |
| `bluetoothEnableAtStart` | Only read once, in `wakeup_task()` at boot (name is literal — it only governs the *start* decision). | `BluetoothServer.cpp:288` |
| `battery.avgSamples`, `battery.updateIntervalMs`, `battery.avgIntervalMs` | Read only in the `Battery` singleton's constructor member-init list into `const` members; the singleton is constructed once at boot. | `Battery.cpp:122-124` |
| `lorawan.loraRegion` | Read fresh via `getRegionFromConfig()`, but the region is baked into the LoRaWAN join session during `init()`. Changing it requires a full re-join, which only happens at boot. | `ElocLora.cpp` (`getRegionFromConfig`, `init`) |
| `lorawan.loraEnable` (**enabling only**) | The `ElocLora` constructor/`init()` only runs at boot; if LoRa was off at boot, turning the flag on has no effect until reboot. *Disabling* works live — `ElocLoraLoop()` checks `getConfig()` fresh and stops polling. Note `pm_configure()` also treats LoRa-enabled as "no DFS/light sleep", so toggling it affects effective CPU power settings on next reboot too. | `ElocLora.cpp` (constructor + loop early-return), `ElocSystem.cpp:337-343` |
| `delConfig` (not a field, but the command) | Comment confirms config isn't reset to default in RAM until next reboot — only the file is deleted. | `ElocCommands.cpp` (`cmd_DelConfig`) |

The Android app knows this list (`Command.requiresDeviceRestart()`) and shows a "restart required"
popup after changing any of these fields, with the option to send the `reboot` command (below).

## Apply live (no reboot needed)

These are read fresh from `getConfig()` / `getInferenceConfig()` / `getDutyCycleConfig()` on every loop
iteration or check, so a `setConfig` takes effect on the next check/cycle:

- `bluetoothEnableOnTapping`, `bluetoothEnableDuringRecord`, `bluetoothOffTimeoutSeconds` — `BluetoothServer.cpp:227,272-275,317`
- `intruderCfg.*` — `ElocSystem.cpp:439`
- `inference.*` (`threshold`, `observationWindowS`, `requiredDetections`) — `EdgeImpulse.cpp:275`
- `dutyCycle.*` (`enable`, `sleepDurationS`, `awakeDurationS`) — read fresh at each duty-cycle decision, e.g. `main.cpp:690,789,1088`
- `battery.noBatteryMode` — `Battery.cpp:152`
- `secondsPerFile` — read at the start of each recording session (`main.cpp:389`), so it applies to the
  *next* session without a device reboot (relevant mainly in continuous/non-duty-cycle mode, since a
  session usually only ends at reboot/deep-sleep anyway)

These used to be boot-only but are now **re-applied by `cmd_SetConfig` itself** (it gets a
`configChangeFlags_t` back from `updateConfig()` and re-applies the affected subsystems —
`ElocCommands.cpp::cmd_SetConfig`):

- `logConfig.*` (`logToSdCard`, `filename`, `maxFiles`, `maxFileSize`) — `cmd_SetConfig` forwards the
  merged values to `Logging::updateConfig()` (the same live path the `setLogPersistent` command uses).
- `lorawan.upLinkIntervalS` — `ElocLoraLoop()` re-reads it each loop via
  `ElocLora::refreshUplinkInterval()` (with the same min-interval validation as at boot).

### CPU frequency: validated + persisted, but applied only on reboot

CPU frequency is **not** applied live (see the reboot table for why — it would relock the BT PLL).
When `changeFlags.cpu` is set, `cmd_SetConfig` validates the value and, if bad, **rolls back** to the
previous CPU config in both RAM and SPIFFS and returns an **error** to the app, so the stored/displayed
config never drifts from what the hardware will boot with. The ESP32 only accepts a handful of CPU clocks
(max: 80/160/240 MHz; min additionally 40/20/10) — validation helpers `isValidCpuMaxFrequency()` /
`isValidCpuMinFrequency()` and the rollback setter `setCpuFrequencyConfig()` live in `ElocConfig.cpp`.
Each field is validated individually (not cross-checked min≤max) so a bad stored value in one field can't
block fixing the other. A valid new value is persisted and takes effect on the next boot, where
`pm_configure()` (run early, before the radios) applies and force-switches to it; the app shows the
restart-required popup so the user can reboot immediately.

## LoRa fields (traced)

- `lorawan.loraEnable` — disable: live; enable: reboot (see table above).
- `lorawan.upLinkIntervalS` — live (see above).
- `lorawan.loraRegion` — reboot (join session).
- `lorawan.eventCooldownS`, `lorawan.eventEndTimeoutS` — defined and merged in `ElocConfig.cpp` but
  currently **never read anywhere** in the firmware (and not exposed in the app UI). Unused until the
  event-cooldown feature is implemented.

## The `reboot` command

`reboot` (no arguments, `ElocCommands.cpp::cmd_Reboot`) restarts the device via `esp_restart()`,
delayed by ~1 s through a one-shot `esp_timer` so the command response reaches the app over Bluetooth
first. The app's restart-required popup uses it for its "Restart now" button.

## Practical takeaway

If a field isn't in the "apply live" table above, assume it needs a reboot until proven otherwise by
tracing it the same way: find where `gElocConfig`/`gMicInfo` is read, and check whether that read happens
in `setup()`/a constructor (boot-only) or in a recurring loop/task (live). When adding new config fields,
prefer wiring them to be read live from `getConfig()` at point-of-use — it avoids silently stale settings
after a `setConfig` call from the app.
