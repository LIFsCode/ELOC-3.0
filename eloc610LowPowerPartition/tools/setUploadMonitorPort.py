#
# Sets OS specific upload_port & monitor_port if not already set
# This script is called by platformio.ini
#

Import("env")     # Yes, starts with a capital letter. This is not a typo.
import sys

WINDOWS_PORT = "COM16"
LINUX_PORT = "/dev/ttyUSB0"

# CAUTION
# Even if MONITOR_PORT is set in platformio.ini doesn't
# seem to be available when this script is run. So, we
# test for UPLOAD_PORT & set MONITOR_PORT to the same
SET_MONITOR_PORT = True

IS_WINDOWS = sys.platform.startswith("win")
IS_LINUX = sys.platform.startswith("linux")
IS_MAC = sys.platform.startswith("darwin")

print("Running setUploadMonitorPort.py...")
# print("UPLOAD_PORT = ", env.get("UPLOAD_PORT"))
# print("MONITOR_PORT = ", env.get("MONITOR_PORT"))
# print(env.Dump())

if (env.get("UPLOAD_PORT") == None):
    if IS_WINDOWS:
        env.Replace(UPLOAD_PORT=WINDOWS_PORT)
    elif IS_LINUX:
        env.Replace(UPLOAD_PORT=LINUX_PORT)
    elif IS_MAC:
        print("Unknown port to set to")
    else:
        sys.stderr.write("Unrecognized OS.\n")
        env.Exit(-1)
    print("upload_port set to", env.get("UPLOAD_PORT"))
else:
    print("upload_port already set to", env.get("UPLOAD_PORT"))
    
if (SET_MONITOR_PORT):
    env.Replace(MONITOR_PORT=(env.get("UPLOAD_PORT")))
    print("monitor_port set to", env.get("MONITOR_PORT"))
