# Non Volatile Storage

## Format of nvs.csv

The content of the NVS memory section is defined in the nvs.csv file and translated into binary format with the ./tools/genNVS.py script.

2 Sections (namespaces) exist:
1. **factory**: Contains 
    * hw_gen: Major version of the HW: Causes full incompatibility with the FW, e.g. FW designed for hw_gen 3 is not able to run on hw_gen 4.
    * hw_rev: Revision of the HW, e.g. Layout or schematic changes that do not cause incompatibility with the FW. But FW versions which do not know this revision may not be able to fully use all HW features. **IMPORTANT**: Firmware must always be designed to be backwards compatible. E.g. if certain HW features are not available in previous revisions, the FW has to deal with that and must provide a fallback path.
    * serial: Device unique serial number. Should be unique across all devices to allow HW tracking & identification. No external requirements apply.
2. **loraKeys**: Contains the secret keys for setting up the encrypted LoraWAN connection
    * devEUI: Device specific globally unique 64 bit ID. Must be individually set based on official database (e.g. TTN)
    * appKey: Application spedific key, stored in *LoraWAN_appKey.hex* (not stored in repo). Key format is a hex string, as provided via TTN without any prefix '0x' or spaces. Direct copy from TTN console.
    * nwkKey: Network session key, stored in *LoraWAN_nwkKey.hex* (not stored in repo). Key format is a hex string, as provided via TTN without any prefix '0x' or spaces. Direct copy from TTN console.


## Handling of the the NVS

The nvs.csv contains data which is intended to be changed on a per device basis. The nvs.csv should never be used unmodified. The checkedin content serves as example only.

The NVS.bin is not programmed into the device from within the standard VS Code workflow. So the device specific data is preserved.

To program the initial NVS information in a fabric new ELOC device use the flashNVSonly.py script or use the following command from espidf python:
```bash
python -m esptool write_flash 0x9000 .\.pio\build\esp32dev\nvs.bin
```

A script to partly automate this process is in the root folder called Autoflasher.py. More info about that can be found in the Wiki:
https://github.com/LIFsCode/ELOC-3.0/wiki/Firmware-Update-&-Initial-Flashing-with-NVS#initial-flashing-including-nvs

## Limitations

The current NVS structure does not allow encrptytion. Hence the LoraWAN Keys are stored **unencrypted** on the device and may be retrieved easily when having physical access to the devise! 
