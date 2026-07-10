/*
 * Created on Fri May 05 2023
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2023 Fabian Lindner
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#ifndef FIRMWAREUPDATE_HPP_
#define FIRMWAREUPDATE_HPP_

#include "esp_err.h"
#include "WString.h"

namespace fwupd {

/* Staging paths shared between the boot-time SD updater and the BT transfer.
 * The .bin + trigger file are the legacy manual SD-swap interface and must
 * keep working (copy elocupdate.bin + doUpdate.txt onto the card by hand). */
constexpr const char* UPDATE_DIR    = "/sdcard/eloc/update";
constexpr const char* STAGED_BIN    = "/sdcard/eloc/update/elocupdate.bin";
/// Metadata written by setFwUpdateApply (or by hand): {"sha256","version","size"}.
/// Optional for the manual SD-swap path — the updater warns if absent.
constexpr const char* STAGED_META   = "/sdcard/eloc/update/elocupdate.json";
/// In-flight BT transfer metadata (enables resume across disconnects/reboots).
constexpr const char* TRANSFER_META = "/sdcard/eloc/update/transfer.json";
/// Outcome of the last boot-time update attempt, readable after reconnect.
constexpr const char* RESULT_FILE   = "/sdcard/eloc/update/result.json";
constexpr const char* TRIGGER_FILE  = "/sdcard/eloc/doUpdate.txt";

/// SHA-256 of a file as lowercase hex. hexOut must hold 65 bytes.
esp_err_t sha256File(const char* path, char hexOut[65]);

/**
 * Integrity sanity check of a staged application image:
 * file exists, size > 0 and <= the inactive OTA slot size, ESP image magic,
 * readable esp_app_desc_t. Extracts the embedded version string.
 * Deliberately does NOT compare versions/dates — whether to update (including
 * downgrades) is the app's/user's decision; the device only verifies integrity.
 */
esp_err_t validateImageFile(const char* path, String& newVersion, String& errMsg);

}  // namespace fwupd

bool updateFirmware();

void checkForFirmwareUpdateFile();

/**
 * Confirm the currently running image after a successful post-boot self-test so
 * the bootloader's rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) does not
 * revert it. Call late in app_main once SD/config/BT are up. No-op unless the
 * running slot is in ESP_OTA_IMG_PENDING_VERIFY state.
 *
 * Note: rollback protects updates *from this firmware version onward* — the
 * first OTA onto an older firmware still rides the old updater, and devices
 * whose factory-flashed bootloader was built without rollback support simply
 * degrade to the previous (no-rollback) behavior.
 */
void markRunningFirmwareValid();

#endif // FIRMWAREUPDATE_HPP_
