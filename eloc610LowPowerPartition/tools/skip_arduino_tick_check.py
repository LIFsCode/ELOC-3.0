# Lets the Arduino core build with the FreeRTOS tick at 100 Hz (CONFIG_FREERTOS_HZ in
# sdkconfig.defaults, since V1.84). Used by every ESP32 env in platformio.ini.
#
# The Arduino core's CMakeLists.txt aborts the build unless CONFIG_FREERTOS_HZ is 1000, because
# its delay() is vTaskDelay(ms / portTICK_PERIOD_MS): at 100 Hz any delay() shorter than 10 ms
# does not wait at all, and longer ones round down to a multiple of 10 ms. This firmware's own
# waits use pdMS_TO_TICKS() or at least one tick, and RadioLib gets an exact delay() through
# ElocRadioHal (ElocLora.cpp). The core reads the ARDUINO_SKIP_TICK_CHECK environment variable
# to skip that check, and PlatformIO starts CMake with a copy of this process's environment, so
# setting it here is enough.
import os

os.environ["ARDUINO_SKIP_TICK_CHECK"] = "1"
