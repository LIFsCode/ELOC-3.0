#
# Enables exFAT support in the ESP-IDF FatFs library by patching FF_FS_EXFAT 0 -> 1
# in the packaged ffconf.h. This script is called by platformio.ini (pre: script).
#
# Why a patch script: this project builds against ESP-IDF 4.4.7 (espressif32 @ 6.9.0,
# hybrid espidf+arduino). IDF 4.4.7 bundles FatFs R0.13c, which fully supports exFAT,
# but its ffconf.h hardcodes `#define FF_FS_EXFAT 0` with no Kconfig switch
# (CONFIG_FATFS_USE_EXFAT only exists from IDF 5.1 onward). In hybrid mode PlatformIO
# builds the fatfs component from source, so flipping this one define is sufficient.
#
# NOTE: this edits the SHARED PlatformIO package (~/.platformio/packages/framework-espidf),
# so every project on this machine building against IDF 4.4.7 gets exFAT enabled.
# That is a harmless capability add: enabling exFAT only makes an additional filesystem
# type recognizable at mount time; FAT12/16/32 behavior is unchanged.
#
# The script is idempotent and self-healing: it runs on every build, so a freshly
# downloaded/reinstalled package (new checkout, CI, `pio pkg update`) is re-patched
# automatically. After the FIRST patch, do one full clean build (`pio run -t clean`)
# so no stale fatfs objects remain.
#

Import("env")     # Yes, starts with a capital letter. This is not a typo.
import os
import re
import sys

# FatFs revision ID this patch was verified against (R0.13c, as bundled in IDF 4.4.7).
EXPECTED_FFCONF_DEF = "86604"


def fail(msg):
    sys.stderr.write("ERROR [patch_fatfs_exfat.py]: %s\n" % msg)
    env.Exit(1)


def patch_fatfs_exfat():
    pkg_dir = env.PioPlatform().get_package_dir("framework-espidf")
    if not pkg_dir:
        fail("framework-espidf package not found - cannot locate ffconf.h")

    ffconf_path = os.path.join(pkg_dir, "components", "fatfs", "src", "ffconf.h")
    if not os.path.isfile(ffconf_path):
        fail("ffconf.h not found at expected location: %s" % ffconf_path)

    with open(ffconf_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Guard: only patch the FatFs revision this was verified against. A platform/IDF
    # bump that changes the revision must be re-verified by a maintainer. On IDF >= 5.1
    # use CONFIG_FATFS_USE_EXFAT=y in sdkconfig.defaults instead and delete this script.
    rev_match = re.search(r"#define\s+FFCONF_DEF\s+(\d+)", content)
    if not rev_match:
        fail("FFCONF_DEF not found in %s - unexpected file format" % ffconf_path)
    if rev_match.group(1) != EXPECTED_FFCONF_DEF:
        fail(
            "FFCONF_DEF is %s but this patch was verified against %s (FatFs R0.13c / "
            "IDF 4.4.7). The platform package has changed - re-verify exFAT patching, "
            "or if this is IDF >= 5.1, set CONFIG_FATFS_USE_EXFAT=y in sdkconfig.defaults "
            "and remove this script." % (rev_match.group(1), EXPECTED_FFCONF_DEF)
        )

    exfat_pattern = re.compile(r"(#define\s+FF_FS_EXFAT[ \t]+)([01])\b")
    exfat_match = exfat_pattern.search(content)
    if not exfat_match:
        fail("FF_FS_EXFAT define not found in %s - unexpected file format" % ffconf_path)

    if exfat_match.group(2) == "1":
        print("patch_fatfs_exfat.py: ffconf.h already patched (FF_FS_EXFAT 1)")
        return

    content = exfat_pattern.sub(r"\g<1>1", content, count=1)
    with open(ffconf_path, "w", encoding="utf-8") as f:
        f.write(content)
    print("Patched ffconf.h: FF_FS_EXFAT 0 -> 1 (exFAT enabled)")
    print("  -> %s" % ffconf_path)
    print("  -> NOTE: run a full clean build once after this first patch "
          "(pio run -t clean) to flush stale fatfs objects.")


patch_fatfs_exfat()
