import copy
import json
import subprocess
import sys
import os
import pkg_resources
import csv

import click
import semantic_version

def get_python_exe():
    def _create_venv(venv_dir):
        pip_path = os.path.join(
            venv_dir,
            "Scripts" if IS_WINDOWS else "bin",
            "pip" + (".exe" if IS_WINDOWS else ""),
        )
        if not os.path.isfile(pip_path):
            # Use the built-in PlatformIO Python to create a standalone IDF virtual env
            env.Execute(
                env.VerboseAction(
                    '"$PYTHONEXE" -m venv --clear "%s"' % venv_dir,
                    "Creating a virtual environment for IDF Python dependencies",
                )
            )

        assert os.path.isfile(
            pip_path
        ), "Error: Failed to create a proper virtual environment. Missing the pip binary!"

    # The name of the IDF venv contains the IDF version to avoid possible conflicts and
    # unnecessary reinstallation of Python dependencies in cases when Arduino
    # as an IDF component requires a different version of the IDF package and
    # hence a different set of Python deps or their versions
    idf_version = get_original_version(platform.get_package_version("framework-espidf"))
    venv_dir = os.path.join(
        env.subst("$PROJECT_CORE_DIR"), "penv", ".espidf-" + idf_version)

    if not os.path.isdir(venv_dir):
        _create_venv(venv_dir)

    python_exe_path = os.path.join(
        venv_dir,
        "Scripts" if IS_WINDOWS else "bin",
        "python" + (".exe" if IS_WINDOWS else ""),
    )

    assert os.path.isfile(python_exe_path), (
        "Error: Missing Python executable file `%s`" % python_exe_path
    )

    return python_exe_path

from SCons.Script import (
    ARGUMENTS,
    COMMAND_LINE_TARGETS,
    DefaultEnvironment,
)

from platformio import fs
from platformio.compat import IS_WINDOWS
from platformio.proc import exec_command
from platformio.builder.tools.piolib import ProjectAsLibBuilder
from platformio.package.version import get_original_version, pepver_to_semver

# Added to avoid conflicts between installed Python packages from
# the IDF virtual environment and PlatformIO Core
# Note: This workaround can be safely deleted when PlatformIO 6.1.7 is released
if os.environ.get("PYTHONPATH"):
    del os.environ["PYTHONPATH"]

env = DefaultEnvironment()
print(env)

platform = env.PioPlatform()
board = env.BoardConfig()
FRAMEWORK_DIR = platform.get_package_dir("framework-espidf")
#idf_path = os.environ['IDF_PATH']
print("Platform: ")
print(FRAMEWORK_DIR)
#print(idf_path)
print(get_python_exe())

partition_file = board.get("build.partitions", "partitions_singleapp.csv")
print(partition_file)

#python = IDF_PYTHON_ENV_PATH + "/Scripts/python.exe"
#esp_tool = FRAMEWORK_DIR + "/components/esptool_py/esptool/esptool.py"
nvs_tool = FRAMEWORK_DIR + "/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
print(nvs_tool)

with open(partition_file) as csv_file:
    csv_reader = csv.reader(csv_file, delimiter=',')
    nvs_base = 0
    nvs_size = 0
    for row in csv_reader:
        print(row)
        if (row[0] == "nvs"):
            print(f'NVS base: {row[3]}')
            nvs_base = row[3]
            nvs_size = row[4]
        if (row[0] == "partition0"):
            print(f'partition0 base: {row[3]}')
            partition_base = row[3]
    subprocess.call([get_python_exe(), nvs_tool,'generate', 'nvs.csv','.pio/build/esp32dev/nvs.bin', nvs_size])
    print("NVS generated successuflly")
   
