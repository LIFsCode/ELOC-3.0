this is for the eloc low power record partition. It needs the high power partition to operate
and cannot be run as a standalone app.  This is the partition that future refinements and
functionality should be focused on.  

if compiling for SPI (old eloc ) hardware, uncomment the "#define USE_SDIO_VERSION" in config.h 

---------------To flash this under platformio:--------------
cd .pio\build\esp32dev
esptool.py --port  COM3 write_flash 0x150000 firmware.bin



running menuconfig
pio run -t menuconfig

https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/kconfig.html#

The "L" key seems to be mapped to the Enter key too, "J" moves the cursor down and "K" moves the cursor up. "G" jumps to the top. Note that my keyboard layout is a german one, which may influence it (?).
https://community.platformio.org/t/esp-idf-sd-card-problem-with-long-file-names/9743/6









