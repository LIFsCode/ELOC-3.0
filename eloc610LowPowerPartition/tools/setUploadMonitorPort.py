#
# Sets OS specific upload_port & monitor_port if not already set.
#
# This script is called by platformio.ini (post script). It tries to
# auto-detect the ESP32's USB-to-UART bridge by VID/PID (then by description,
# then by "only one port present"), and only falls back to a hardcoded default
# if detection finds nothing.
#

Import("env")     # Yes, starts with a capital letter. This is not a typo.
import sys

# Fallback ports, used ONLY when auto-detection finds nothing
WINDOWS_PORT = "COM5"
LINUX_PORT = "/dev/ttyUSB0"
MAC_PORT = "/dev/cu.SLAB_USBtoUART"

# CAUTION
# Even if MONITOR_PORT is set in platformio.ini it doesn't
# seem to be available when this script is run. So, we
# test for UPLOAD_PORT & set MONITOR_PORT to the same
SET_MONITOR_PORT = True

IS_WINDOWS = sys.platform.startswith("win")
IS_LINUX = sys.platform.startswith("linux")
IS_MAC = sys.platform.startswith("darwin")

# Known USB-to-UART bridge chips used on ESP32 boards: (VID, PID)
KNOWN_USB_SERIAL_IDS = {
    (0x10C4, 0xEA60),  # Silicon Labs CP210x (CP2102 / CP2104)
    (0x10C4, 0xEA70),  # Silicon Labs CP2105
    (0x1A86, 0x7523),  # WCH CH340
    (0x1A86, 0x55D4),  # WCH CH9102 / CH343
    (0x0403, 0x6001),  # FTDI FT232R
    (0x0403, 0x6010),  # FTDI FT2232
    (0x0403, 0x6014),  # FTDI FT232H
    (0x303A, 0x1001),  # Espressif native USB JTAG/serial
}

# Description / manufacturer keywords that hint at a USB-serial bridge
KEYWORDS = ("cp210", "ch340", "ch910", "ch343", "silicon labs", "ftdi",
            "usb serial", "usb-serial", "usb to uart", "espressif", "wch")


def detect_port():
    try:
        from serial.tools import list_ports
    except ImportError:
        print("  pyserial not available, cannot auto-detect port")
        return None

    ports = list(list_ports.comports())
    if not ports:
        print("  No serial ports found")
        return None

    # 1) Strong match: known USB-to-UART bridge VID/PID
    strong = [p for p in ports
              if p.vid is not None and p.pid is not None
              and (p.vid, p.pid) in KNOWN_USB_SERIAL_IDS]
    if strong:
        if len(strong) > 1:
            print("  Multiple ESP32 bridges detected, using the first:")
            for p in strong:
                print("     %s - %s" % (p.device, p.description))
        chosen = strong[0]
        print("  Auto-detected ESP32 bridge by VID/PID: %s (%s)" % (chosen.device, chosen.description))
        return chosen.device

    # 2) Weaker match: description / manufacturer keyword
    for p in ports:
        haystack = " ".join(filter(None, [p.description, p.manufacturer, p.product])).lower()
        if any(k in haystack for k in KEYWORDS):
            print("  Auto-detected USB-serial bridge by description: %s (%s)" % (p.device, p.description))
            return p.device

    # 3) Exactly one serial port present -> assume that's the device
    if len(ports) == 1:
        print("  Single serial port present, using it: %s (%s)" % (ports[0].device, ports[0].description))
        return ports[0].device

    print("  Could not auto-detect ESP32 port; candidates were:")
    for p in ports:
        print("     %s - %s" % (p.device, p.description))
    return None


print("Running setUploadMonitorPort.py...")

if env.get("UPLOAD_PORT") is None:
    port = detect_port()
    if port is None:
        if IS_WINDOWS:
            port = WINDOWS_PORT
        elif IS_LINUX:
            port = LINUX_PORT
        elif IS_MAC:
            port = MAC_PORT
        else:
            sys.stderr.write("Unrecognized OS.\n")
            env.Exit(-1)
        print("  Auto-detection failed, falling back to default:", port)
    env.Replace(UPLOAD_PORT=port)
    print("upload_port set to", env.get("UPLOAD_PORT"))
else:
    print("upload_port already set to", env.get("UPLOAD_PORT"))

if SET_MONITOR_PORT:
    env.Replace(MONITOR_PORT=(env.get("UPLOAD_PORT")))
    print("monitor_port set to", env.get("MONITOR_PORT"))
