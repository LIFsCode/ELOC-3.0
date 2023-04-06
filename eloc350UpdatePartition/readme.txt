this is for the "update" partition. 

if compiling for SPI (old eloc ) hardware, uncomment the "#define USE_SDIO_VERSION" in config.h 

---------------To flash this under platformio:--------------
cd .pio\build\esp32dev
esptool.py --port  COM3 write_flash 0x2A0000 firmware.bin



running menuconfig
pio run -t menuconfig
The "L" key seems to be mapped to the Enter key too, "J" moves the cursor down and "K" moves the cursor up. "G" jumps to the top. Note that my keyboard layout is a german one, which may influence it (?).
https://community.platformio.org/t/esp-idf-sd-card-problem-with-long-file-names/9743/6









