this is for the eloc main high power bluetooth partition. This can be used standalone as in old elocs 
if the bluetooth record is set to "on". If bluetooth record is set to "off" the eloc will 
reboot and record in the sep-idf low power partition.
If "update" is needed, the update partition must also be uploaded to the eloc. This automates updating from sd card. 
 




if compiling for SPI (old eloc ) hardware, uncomment the "#define USE_SDIO_VERSION" in config.h 

---------------To flash this under platformio:--------------
cd .pio\build\esp32dev
esptool.py --port  COM3 write_flash 0x10000 firmware.bin



running menuconfig
pio run -t menuconfig
The "L" key seems to be mapped to the Enter key too, "J" moves the cursor down and "K" moves the cursor up. "G" jumps to the top. Note that my keyboard layout is a german one, which may influence it (?).
https://community.platformio.org/t/esp-idf-sd-card-problem-with-long-file-names/9743/6









