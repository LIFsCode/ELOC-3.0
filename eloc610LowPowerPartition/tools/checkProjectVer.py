# Guard against a stale PROJECT_VER in the firmware's embedded esp_app_desc_t.
#
# The top-level CMakeLists.txt derives PROJECT_VER by parsing the VERSION define out of
# include/project_config.h at CMake *configure* time. Under native idf.py/ninja the
# CMAKE_CONFIGURE_DEPENDS entry re-triggers configure when the header changes, but
# PlatformIO builds with SCons and caches the one-time CMake configure result in
# project_description.json — so a VERSION bump silently keeps stamping the old version
# into the app descriptor (the app then shows a bogus up/downgrade dialog).
#
# This pre-script compares the header's VERSION against the cached project_version and
# deletes the CMake cache on mismatch, forcing PlatformIO to reconfigure.

import json
import os
import re

Import("env")

PROJECT_DIR = env.subst("$PROJECT_DIR")
BUILD_DIR = env.subst("$BUILD_DIR")

header_path = os.path.join(PROJECT_DIR, "include", "project_config.h")
desc_path = os.path.join(BUILD_DIR, "project_description.json")

header_version = None
with open(header_path, "r", encoding="utf-8") as f:
    for line in f:
        m = re.match(r'\s*#define\s+VERSION\s+"([^"]+)"', line)
        if m:
            header_version = m.group(1)
            break

if header_version is None:
    print("checkProjectVer: no VERSION define found in project_config.h, skipping check")
elif os.path.isfile(desc_path):
    with open(desc_path, "r", encoding="utf-8") as f:
        cached_version = json.load(f).get("project_version")
    if cached_version != header_version:
        print("checkProjectVer: cached PROJECT_VER '{}' != header VERSION '{}', "
              "forcing CMake reconfigure".format(cached_version, header_version))
        for stale in ("CMakeCache.txt", "project_description.json"):
            path = os.path.join(BUILD_DIR, stale)
            if os.path.isfile(path):
                os.remove(path)
