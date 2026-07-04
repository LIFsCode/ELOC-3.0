# Add exFAT Support (keep FAT32 working)

**Status: planned, not yet implemented.**

## Context

The firmware currently only mounts FAT32 SD cards. This is a field pain point: SD cards larger
than 32 GB ship factory-formatted as **exFAT**, so field techs must reformat every card to FAT32
before use, and an exFAT card inserted by mistake silently fails with a generic
"Failed to mount filesystem" log.

**Root cause (verified):** there is *no* FAT32 check anywhere in the firmware. The restriction
comes entirely from the ESP-IDF FatFs library being compiled without exFAT:

- The project builds with `espressif32 @ 6.9.0` in hybrid `espidf + arduino` mode →
  **ESP-IDF 4.4.7** (`framework-espidf 3.40407`) + Arduino core 2.0.17.
- IDF 4.4.7 bundles **FatFs R0.13c** (`FFCONF_DEF 86604`), which fully supports exFAT — but its
  `ffconf.h` hardcodes `#define FF_FS_EXFAT 0` at line 255 with **no Kconfig switch**
  (`CONFIG_FATFS_USE_EXFAT` only exists from IDF 5.1 onward, so an sdkconfig.defaults entry would
  be silently ignored by kconfgen on this IDF version).
  File: `~/.platformio/packages/framework-espidf/components/fatfs/src/ffconf.h`
- exFAT's prerequisite (LFN enabled) is already met: `CONFIG_FATFS_LFN_HEAP=y`, `MAX_LFN=255`.
- In hybrid mode PlatformIO **builds the fatfs component from source**, so flipping that one
  define is sufficient — no library swap, no platform upgrade. (Upgrading to an IDF 5.x platform
  would be a massive migration — Arduino core 3.x, new I2S driver API — explicitly rejected as
  out of scope here.)

**Chosen approach (easiest + most solid):** an idempotent pre-build script that patches
`FF_FS_EXFAT 0 → 1` in the packaged `ffconf.h`, following the repo's existing precedent for
patching packaged sources (`pre:tools/modify_edge-impulse-sdk.py`). FAT32 behavior is unchanged —
enabling exFAT in FatFs only *adds* a recognizable filesystem type at mount time.

**Blast radius is tiny:** the firmware touches the FatFs API directly in exactly one place
(`f_getfree()` in `SDCardSDIO::updateFreeSpace()`); all other file I/O goes through POSIX VFS,
which is filesystem-agnostic. `allocation_unit_size` in the mount config is only used when
formatting (`format_if_mount_failed = false`, and there is no `f_mkfs` anywhere) — no-op.
WAV files are split every 60 s (~1–2 MB), far from any 4 GB concern. App partitions are 8 MB
(`elocPartitions.csv`), so the ~10–15 KB flash cost of exFAT code is negligible.

## Changes

### 1. New pre-build patch script — `tools/patch_fatfs_exfat.py`

PlatformIO SCons script (`Import("env")` style, like the existing tools):

- Locate the header via `env.PioPlatform().get_package_dir("framework-espidf")` +
  `components/fatfs/src/ffconf.h` (never hardcode the user path).
- **Safety checks, fail loudly with a clear message if any fails:**
  - file exists;
  - `FFCONF_DEF` is `86604` (FatFs R0.13c) — if a future platform bump changes this, the script
    must error and tell the maintainer to re-verify (newer IDF ≥5.1 should use
    `CONFIG_FATFS_USE_EXFAT=y` in sdkconfig.defaults instead, and this script becomes obsolete);
  - the file contains either `#define FF_FS_EXFAT		0` (needs patch) or `... 1` (already
    patched → print "already patched", do nothing). Anything else → error.
- Replace the `0` with `1`, preserving the original tab formatting; print a clear
  `Patched ffconf.h: FF_FS_EXFAT 0 -> 1 (exFAT enabled)` line.
- Idempotent and self-healing: runs on every build, so a re-downloaded/reinstalled package
  (fresh checkout, CI, `pio pkg update`) gets re-patched automatically.
- Note in the script header: this edits the **shared** `~/.platformio` package (all projects on
  the machine building against IDF 4.4.7 get exFAT enabled — harmless capability add).

### 2. `platformio.ini` — register the script

In `[options] extra_scripts`, add `pre:tools/patch_fatfs_exfat.py` (before `genVersion.py`).
Because the patch persists in the package, the unit-test envs (`target_unit_*`, which have their
own `extra_scripts`) build consistently too — no need to add it there, but doing so is harmless;
keep it only in `[options]` to minimize churn.

### 3. `lib/sd_card/src/SDCardSDIO.cpp` — hardening + diagnostics

- **Fix a latent 32-bit overflow** in `updateFreeSpace()` (line 102): the multiply happens in
  32-bit before widening. Change to
  `uint64_t fre_sect = static_cast<uint64_t>(fre_clust) * fs->csize;`
  (matters for large exFAT cards; correct regardless).
- **Log the mounted filesystem type** after a successful mount in `init()` (use the `FATFS*`
  returned by `f_getfree`, or a small helper): map `fs->fs_type` → `"FAT12/FAT16/FAT32/exFAT"`.
  Guard the `FS_EXFAT` case with `#if FF_FS_EXFAT` so the file still compiles if the patch is
  ever absent. House style: `ESP_LOGI(TAG, ...)`.
- **Improve the mount-failure message** (line 46): `"Failed to mount filesystem — card must be
  formatted as FAT32 or exFAT"`.

### 4. `lib/sd_card/src/SDCard.cpp` (SPI fallback) — message only

Update the stale example-copied failure message (lines 64–65, currently references
"EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig") to the same FAT32/exFAT wording.

### 5. `sdkconfig.defaults` — comment only

Add a comment near the `CONFIG_FATFS_*` block (lines ~106–111) explaining that exFAT is enabled
via `tools/patch_fatfs_exfat.py` because IDF 4.4.7 has no `CONFIG_FATFS_USE_EXFAT` Kconfig, and
that on a future IDF ≥5.1 upgrade this should become a real config entry.

### 6. Documentation (per repo convention)

- `memory-bank/techContext.md`: change the "Long filenames on FAT filesystem" note to state SD
  cards may be **FAT32 or exFAT**, and document the patch script + why (IDF 4.4.7 limitation).
- `memory-bank/activeContext.md` + `memory-bank/progress.md`: record the change and its
  hardware-test status after verification.

**Not in scope (deliberately):** no BT/status JSON changes (app compatibility untouched — capacity
and free-space fields work identically on exFAT), no on-device formatting, no platform upgrade.

**One-line legal note:** exFAT support is off by default in FatFs for historical Microsoft patent
reasons; Microsoft published the exFAT spec publicly in 2019 (OIN). Low risk, but worth the
maintainers knowing why the flag exists.

## Verification

1. **Patch mechanics:** run `pio run -e esp32dev` → build log shows the patch line; open the
   packaged `ffconf.h` and confirm `FF_FS_EXFAT 1`. Run again → "already patched". Do one
   **full clean build** after first patching (`pio run -t clean`) to be certain no stale fatfs
   objects remain, then build both `esp32dev` and `esp32dev-ei` cleanly and compare flash usage
   (expect roughly +10–15 KB, trivially within the 8 MB partition).
2. **FAT32 regression (hardware):** flash a device with a known-good FAT32 card → boot log shows
   `Filesystem: FAT32`, recording session works, `getStatus` over BT reports the same
   SdCardSize/FreeSpace as before, files read back fine on a PC.
3. **exFAT (hardware):** insert a factory-formatted exFAT card (≥64 GB) → mounts, log shows
   `Filesystem: exFAT`, record a session, verify WAV files play back on a PC and BT free-space
   figures match what the PC reports for the card.
4. **Duty-cycle path:** with the exFAT card, let the device go through a deep-sleep wake cycle to
   confirm remount-on-wake works (same `init()` path, but verify on hardware).
5. Run `pio test -e target_unit_selected_tests` to confirm the test envs still build/pass.
