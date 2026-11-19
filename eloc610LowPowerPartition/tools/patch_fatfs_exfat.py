#
# Prepares the ESP-IDF FatFs library for ELOC by patching the packaged sources. This script is
# called by platformio.ini (pre: script). It does two things:
#
# 1. ffconf.h: enables exFAT support (FF_FS_EXFAT 0 -> 1).
#
#    Why a patch script: this project builds against ESP-IDF 4.4.7 (espressif32 @ 6.9.0,
#    hybrid espidf+arduino). IDF 4.4.7 bundles FatFs R0.13c, which fully supports exFAT,
#    but its ffconf.h hardcodes `#define FF_FS_EXFAT 0` with no Kconfig switch
#    (CONFIG_FATFS_USE_EXFAT only exists from IDF 5.1 onward). In hybrid mode PlatformIO
#    builds the fatfs component from source, so flipping this one define is sufficient.
#
# 2. ff.c: fixes two things in FatFs itself, see FF_C_PATCHES below.
#
# NOTE: this edits the SHARED PlatformIO package (~/.platformio/packages/framework-espidf),
# so every project on this machine building against IDF 4.4.7 gets these changes.
# That is harmless: enabling exFAT only makes an additional filesystem type recognizable at
# mount time, the first ff.c fix only changes the order of writes on exFAT, and the second
# only affects callers of utime(), which nothing in the firmware uses but RotateFile.
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

# Marks a line this script put into ff.c, which makes running it again a no-op.
FF_C_MARKER = "ELOC patch"

# Each entry is (what it fixes, the one line of ff.c to find, what to put in its place).
#
# create_chain(), a file on exFAT that grows into a cluster not adjacent to its last one:
#   FatFs links the old last cluster to the new one right away, but only writes the new
#   cluster's own end-of-chain entry at the next f_sync(). On exFAT the FAT entry of a free
#   cluster is undefined, typically a leftover of some deleted file. So a deep sleep or reset
#   between the two leaves a chain running through an undefined value, as soon as anything
#   moves FatFs' sector window and thereby writes the link to the card (RotateFile's stat()
#   on every log line does). Reopening the file then takes the new cluster as "already
#   followed", the end-of-chain entry is never written at all, and at the next cluster
#   boundary f_write() returns FR_INT_ERR, on every boot from then on. Windows calls the file
#   corrupted and refuses to read it. Writing the new cluster's end-of-chain entry first means
#   the chain on the card is valid at every moment. The FAT32 code already works this way.
#
# f_utime():
#   Also sets the created date. A log file created before GPS or the app has set the clock
#   carries the firmware build date, and FatFs has no other way to correct that.
FF_C_PATCHES = [
    (
        "create_chain(): end-of-chain entry before the link",
        "\t\t\t\tres = fill_last_frag(obj, clst, ncl);\t/* Fill last fragment on the FAT and link it to new one */",
        "\t\t\t\tres = put_fat(fs, ncl, 0xFFFFFFFF);\t/* ELOC patch: end-of-chain before the link, see tools/patch_fatfs_exfat.py */\n"
        "\t\t\t\tif (res == FR_OK) res = fill_last_frag(obj, clst, ncl);\t/* Fill last fragment on the FAT and link it to new one */",
    ),
    (
        "f_utime(): created date on exFAT",
        "\t\t\t\tst_dword(fs->dirbuf + XDIR_ModTime, (DWORD)fno->fdate << 16 | fno->ftime);",
        "\t\t\t\tst_dword(fs->dirbuf + XDIR_ModTime, (DWORD)fno->fdate << 16 | fno->ftime);\n"
        "\t\t\t\tst_dword(fs->dirbuf + XDIR_CrtTime, (DWORD)fno->fdate << 16 | fno->ftime);\t/* ELOC patch */",
    ),
    (
        "f_utime(): created date on FAT32",
        "\t\t\t\tst_dword(dj.dir + DIR_ModTime, (DWORD)fno->fdate << 16 | fno->ftime);",
        "\t\t\t\tst_dword(dj.dir + DIR_ModTime, (DWORD)fno->fdate << 16 | fno->ftime);\n"
        "\t\t\t\tst_dword(dj.dir + DIR_CrtTime, (DWORD)fno->fdate << 16 | fno->ftime);\t/* ELOC patch */",
    ),
]


def fail(msg):
    sys.stderr.write("ERROR [patch_fatfs_exfat.py]: %s\n" % msg)
    env.Exit(1)


def patch_ffconf(pkg_dir):
    ffconf_path = os.path.join(pkg_dir, "components", "fatfs", "src", "ffconf.h")
    if not os.path.isfile(ffconf_path):
        fail("ffconf.h not found at expected location: %s" % ffconf_path)

    with open(ffconf_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Guard: only patch the FatFs revision this was verified against. A platform/IDF
    # bump that changes the revision must be re-verified by a maintainer. On IDF >= 5.1
    # use CONFIG_FATFS_USE_EXFAT=y in sdkconfig.defaults instead and delete this script's
    # ffconf.h part. The ff.c fixes have to be re-checked against the new FatFs either way.
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


def patch_ff_c(pkg_dir):
    ff_c_path = os.path.join(pkg_dir, "components", "fatfs", "src", "ff.c")
    if not os.path.isfile(ff_c_path):
        fail("ff.c not found at expected location: %s" % ff_c_path)

    # latin-1 and newline="" hand every byte back unchanged, whatever the file is encoded in
    with open(ff_c_path, "r", encoding="latin-1", newline="") as f:
        content = f.read()
    eol = "\r\n" if "\r\n" in content else "\n"

    if FF_C_MARKER in content:
        print("patch_fatfs_exfat.py: ff.c already patched")
        return

    for what, old, new in FF_C_PATCHES:
        if content.count(old) != 1:
            fail("ff.c: cannot apply \"%s\", the line to patch was found %d times, expected once: %s"
                 % (what, content.count(old), old))
        content = content.replace(old, new.replace("\n", eol))

    with open(ff_c_path, "w", encoding="latin-1", newline="") as f:
        f.write(content)
    print("Patched ff.c: %s" % "; ".join(what for what, _, _ in FF_C_PATCHES))
    print("  -> %s" % ff_c_path)
    print("  -> NOTE: run a full clean build once after this first patch "
          "(pio run -t clean) to flush stale fatfs objects.")


def patch_fatfs():
    pkg_dir = env.PioPlatform().get_package_dir("framework-espidf")
    if not pkg_dir:
        fail("framework-espidf package not found - cannot locate the FatFs sources")
    patch_ffconf(pkg_dir)
    patch_ff_c(pkg_dir)


patch_fatfs()
