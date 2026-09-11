import subprocess
import sys
import os
import csv

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

    python_exe_path = os.path.join(
        venv_dir,
        "Scripts" if IS_WINDOWS else "bin",
        "python" + (".exe" if IS_WINDOWS else ""),
    )

    # Test for the interpreter itself, not just the directory: an existing-but-empty venv
    # dir (left behind by an interrupted setup or an external cleanup) passes an isdir()
    # test, so creation gets skipped and the assert below then fails with a confusing
    # "Missing Python executable" instead of just rebuilding the venv.
    if not os.path.isfile(python_exe_path):
        _create_venv(venv_dir)

    assert os.path.isfile(python_exe_path), (
        "Error: Missing Python executable file `%s`" % python_exe_path
    )

    return python_exe_path


def ensure_distutils(python_exe):
    """Make sure `import distutils` works in the IDF venv.

    nvs_partition_gen.py (IDF 4.4.x) imports distutils.dir_util at module level, but
    distutils was removed from the stdlib in Python 3.12 (PEP 632). setuptools re-provides
    it via distutils-precedence.pth - however a venv created by Python 3.12+ no longer
    bootstraps setuptools, and PlatformIO's IDF dependency install does not pull it in
    either. So every venv (re)creation would otherwise silently break NVS generation.
    """
    probe = subprocess.call(
        [python_exe, "-c", "import distutils.dir_util"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if probe == 0:
        return

    print("genNVS.py: distutils missing from the IDF venv (Python 3.12+ / PEP 632) - "
          "installing setuptools, which re-provides it")
    if subprocess.call([python_exe, "-m", "pip", "install", "--quiet", "setuptools"]) != 0:
        print("genNVS.py: WARNING - failed to install setuptools; NVS generation will "
              "likely fail with \"No module named 'distutils'\"")

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

build_dir = env.subst("$BUILD_DIR")
nvs_bin_path = os.path.join(build_dir, 'nvs.bin')

with open(partition_file) as csv_file:
    csv_reader = csv.reader(csv_file, delimiter=',')
    nvs_base = 0
    nvs_size = 0
    for row in csv_reader:
        print(row)
        if (row[0].strip() == "nvs"):
            print(f'NVS base: {row[3].strip()}')
            nvs_base = row[3].strip()
            nvs_size = row[4].strip()
        if (row[0].strip() == "partition0"):
            print(f'partition0 base: {row[3].strip()}')
            partition_base = row[3].strip()
    print(f'Generating NVS binary: {nvs_bin_path} (size: {nvs_size})')
    python_exe = get_python_exe()
    ensure_distutils(python_exe)
    ret = subprocess.call([python_exe, nvs_tool, 'generate', 'nvs.csv', nvs_bin_path, nvs_size])
    if ret != 0:
        print(f"ERROR: NVS generation failed with return code: {ret}")
    else:
        print("NVS generated successfully")
   
