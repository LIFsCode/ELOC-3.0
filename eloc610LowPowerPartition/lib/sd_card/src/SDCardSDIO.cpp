#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#include "SDCardSDIO.h"
static const char *TAG = "SDC";

#define SPI_DMA_CHAN 1

/// Monotonic milliseconds. Local helper so this library stays free of the Arduino core.
static inline uint32_t nowMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

// Map FatFs fs_type to a human-readable name. FS_EXFAT only exists when the
// FatFs library is built with FF_FS_EXFAT (see tools/patch_fatfs_exfat.py).
static const char *fsTypeName(BYTE fs_type) {
  switch (fs_type) {
    case FS_FAT12: return "FAT12";
    case FS_FAT16: return "FAT16";
    case FS_FAT32: return "FAT32";
#if FF_FS_EXFAT
    case FS_EXFAT: return "exFAT";
#endif
    default: return "unknown";
  }
}

SDCardSDIO::SDCardSDIO() : m_mounted(false), m_card(nullptr) {
  // Recursive: update() holds the lock across init(), which in turn calls unmount().
  m_fsMutex = xSemaphoreCreateRecursiveMutex();
  if (m_fsMutex == nullptr) {
    ESP_LOGE(TAG, "Failed to create SD filesystem mutex");
  }
}

bool SDCardSDIO::claimFs(uint32_t timeoutMs) {
  if (m_fsMutex == nullptr) {
    return true;  // no mutex: fall back to unsynchronised access rather than blocking writes
  }
  return xSemaphoreTakeRecursive(m_fsMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void SDCardSDIO::releaseFs() {
  if (m_fsMutex != nullptr) {
    xSemaphoreGiveRecursive(m_fsMutex);
  }
}

esp_err_t SDCardSDIO::init(const char *mount_point) {
  m_mount_point = mount_point;
  esp_err_t ret;

  // A previous mount must be fully released first: esp_vfs_fat_sdmmc_mount() fails with
  // ESP_ERR_INVALID_STATE while the path is still registered, which is what used to make a
  // re-inserted card unrecoverable without a reboot.
  if (m_vfsRegistered) {
    unmount();
  }
  // Options for mounting the filesystem.
  // If format_if_mount_failed is set to true, SD card will be partitioned and
  // formatted in case when mounting fails.
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024};

  ESP_LOGI(TAG, "Initializing SD card");

  // This initializes the slot without card detect (CD) and write protect (WP) signals.
  // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 1;
  // Enable internal pull-ups on enabled pins. The internal pull-ups
  // are insufficient however, please make sure 10k external pull-ups are
  // connected on the bus. This is for debug / example purpose only.
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  ret = esp_vfs_fat_sdmmc_mount(m_mount_point.c_str(), &m_host, &slot_config, &mount_config, &m_card);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount filesystem - card must be formatted as FAT32 or exFAT");
    } else {
      ESP_LOGE(TAG, "Failed to initialize the card (%s)", esp_err_to_name(ret));
    }
    return ret;
  }

  m_mounted = true;
  m_vfsRegistered = true;
  m_failedMounts = 0;
  m_retryIntervalMs = m_MOUNT_RETRY_MIN_MS;
  ESP_LOGI(TAG, "SDCard mounted at: %s", m_mount_point.c_str());

  // A card is provably present right now, so this is the one moment the detect line can be
  // checked against ground truth. If it disagrees, the wiring/polarity assumption
  // (SDCARD_DETECT_PRESENT_LEVEL) is wrong or the input floats -> ignore it and fall back to
  // filesystem probing rather than acting on a signal we cannot trust.
  if (m_cardDetect != nullptr && !m_cdValidated && !m_cdRejected) {
    m_cdInserted = m_cardDetect();
    if (m_cdInserted) {
      m_cdValidated = true;
      ESP_LOGI(TAG, "SD card-detect line validated (reads 'inserted' with a mounted card)");
    } else {
      m_cdRejected = true;
      ESP_LOGW(TAG, "SD card mounted but card-detect reads 'removed' - check the IO4 wiring, "
                    "its pull-up and SDCARD_DETECT_PRESENT_LEVEL. Falling back to polling.");
    }
    m_cdStableSinceMs = nowMs();
  }

  FATFS *fs = nullptr;
  DWORD fre_clust;
  if (f_getfree(m_mount_point.c_str(), &fre_clust, &fs) == FR_OK && fs != nullptr) {
    ESP_LOGI(TAG, "Filesystem: %s", fsTypeName(fs->fs_type));
  }

  // Card has been initialized, print its properties
  sdmmc_card_print_info(stdout, m_card);

  return ret;
}

float SDCardSDIO::getCapacityMB() const {
  if (!m_mounted) {
    return 0;
  }
  return static_cast<float>((uint64_t) m_card->csd.capacity * m_card->csd.sector_size) / (1024.0 * 1024.0);
}

esp_err_t SDCardSDIO::unmount() {
  if (!m_vfsRegistered) {
    m_mounted = false;
    return ESP_OK;
  }
  // Wait out any asynchronous writer holding the filesystem (see claimFs()). Proceeding after a
  // timeout is deliberate: the card is already gone, and never unmounting would make the socket
  // permanently dead - the exact failure this whole path exists to avoid.
  bool locked = claimFs(2000);
  if (!locked) {
    ESP_LOGW(TAG, "Unmounting while another task still holds the filesystem");
  }
  // Releases the FATFS volume, deinitialises the SDMMC host and frees m_card.
  esp_err_t err = esp_vfs_fat_sdcard_unmount(m_mount_point.c_str(), m_card);
  if (err != ESP_OK) {
    // Log and continue: leaving the stale registration in place would block every future mount.
    ESP_LOGW(TAG, "esp_vfs_fat_sdcard_unmount() returned %s", esp_err_to_name(err));
  }
  m_vfsRegistered = false;
  m_mounted = false;
  m_card = nullptr;
  m_free_bytes = 0;
  if (locked) {
    releaseFs();
  }
  ESP_LOGI(TAG, "SD card unmounted");
  return err;
}

bool SDCardSDIO::pollCardDetect() {
  if (m_cardDetect == nullptr) {
    return true;  // no detect line: presence unknown, assume a card is there
  }
  bool level = m_cardDetect();
  uint32_t now = nowMs();

  if (level != m_cdInserted) {
    // State change: hold it for the debounce/settle time before believing the switch.
    // Insertion needs the longer wait — the detect switch closes before the card contacts
    // have fully wiped, and mounting too early yields a bogus init failure.
    uint32_t settle = level ? m_INSERT_SETTLE_MS : m_REMOVE_DEBOUNCE_MS;
    if ((now - m_cdStableSinceMs) >= settle) {
      m_cdInserted = level;
      m_cdStableSinceMs = now;
      ESP_LOGI(TAG, "SD card-detect: card %s", level ? "inserted" : "removed");
    }
  } else {
    m_cdStableSinceMs = now;
  }
  return m_cdInserted;
}

esp_err_t SDCardSDIO::update() {
    bool cardDetectUsable = hasCardDetect();
    // Poll the switch even before it is validated, so m_cdInserted tracks the line from boot.
    bool wasInserted = m_cdInserted;
    bool inserted = pollCardDetect();
    uint32_t now = nowMs();

    // Held across the mount/unmount so no other task can be inside the filesystem while it is
    // torn down or replaced. If someone else has it, report no change rather than block.
    if (!claimFs(50)) {
        return m_mounted ? ESP_OK : ESP_ERR_NOT_FOUND;
    }

    if (m_mounted) {
        if (cardDetectUsable && !inserted) {
            // Physical removal. Drop the logical mount immediately so no task opens another
            // file, but leave the teardown itself to the caller (see unmount()).
            ESP_LOGW(TAG, "SD card removed");
            m_mounted = false;
            m_free_bytes = 0;
            releaseFs();
            return ESP_ERR_NOT_FOUND;
        }
        // Without a usable detect line the filesystem itself is the only removal sensor, and
        // every probe on a dead card blocks ~1 s in the SDMMC driver - so throttle it.
        if (!cardDetectUsable && ((now - m_lastProbeMs) < m_BLIND_PROBE_INTERVAL_MS)) {
            releaseFs();
            return ESP_OK;
        }
        m_lastProbeMs = now;
        esp_err_t err = updateFreeSpace();
        if (err != ESP_OK) {
            // Card stopped answering: pulled out, or failed electrically.
            ESP_LOGE(TAG, "SD card stopped responding (%s)", esp_err_to_name(err));
            if (cardDetectUsable) {
                // The detect line claimed a card was still in the socket, so it is not wired to
                // what we think it is (or floats). Stop trusting it for the rest of this boot -
                // acting on it would keep the mount retries tight and delay every teardown.
                m_cdValidated = false;
                m_cdRejected = true;
                ESP_LOGE(TAG, "Card-detect still reads 'inserted' - detect line unusable, "
                              "falling back to filesystem polling");
            }
            m_mounted = false;
            m_free_bytes = 0;
            releaseFs();
            return ESP_ERR_INVALID_RESPONSE;
        }
        releaseFs();
        return ESP_OK;
    }

    // --- not mounted: wait for a card, then re-mount ------------------------------------
    if (m_vfsRegistered) {
        // Removal was detected but the caller has not torn the volume down yet.
        releaseFs();
        return ESP_ERR_NOT_FOUND;
    }
    if (m_mount_point.empty()) {
        ESP_LOGE(TAG, "Mount point not set");
        releaseFs();
        return ESP_ERR_INVALID_STATE;
    }
    if (cardDetectUsable && !inserted) {
        releaseFs();
        return ESP_ERR_NOT_FOUND;  // empty socket: nothing to do, and nothing to time out
    }
    if (inserted && !wasInserted) {
        m_retryIntervalMs = 0;  // fresh insert edge: try immediately
    }
    uint32_t retryInterval = (cardDetectUsable && inserted) ? m_MOUNT_RETRY_PRESENT_MS
                                                            : m_retryIntervalMs;
    if ((now - m_lastProbeMs) < retryInterval) {
        releaseFs();
        return ESP_ERR_NOT_FOUND;
    }
    m_lastProbeMs = now;

    ESP_LOGW(TAG, "SD card not mounted, attempting to mount (attempt %u)", m_failedMounts + 1);
    init(m_mount_point.c_str());
    if (!m_mounted) {
        m_failedMounts++;
        // Back off: without a card this attempt just burned ~4 s of blocking SDMMC timeouts.
        m_retryIntervalMs = (m_retryIntervalMs < m_MOUNT_RETRY_MIN_MS)
                              ? m_MOUNT_RETRY_MIN_MS
                              : std::min(m_retryIntervalMs * 2, m_MOUNT_RETRY_MAX_MS);
        m_lastProbeMs = nowMs();  // measure the backoff from the end of the blocking attempt
        releaseFs();
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = updateFreeSpace();
    releaseFs();
    return err;
}

esp_err_t SDCardSDIO::updateFreeSpace() {
    FATFS *fs;
    uint32_t fre_clust;
    FRESULT res;
    /* Get volume information and free clusters of drive 0 */
    res = f_getfree(m_mount_point.c_str(), &fre_clust, &fs);
    if (res != FR_OK) {
      return ESP_ERR_INVALID_RESPONSE;
    }
    /**
     * @ref: http://elm-chan.org/fsw/ff/doc/getfree.html
     * Free bytes =  free clusters * sectors per cluster * sector size
     *            =  fre_clust * fs->csize * fs->ssize
     */
    uint64_t fre_sect = static_cast<uint64_t>(fre_clust) * fs->csize;
    m_free_bytes = fre_sect * fs->ssize;
    if (0) {
      ESP_LOGI(TAG, "SD card free space: %llu bytes", m_free_bytes);
      ESP_LOGI(TAG, "SD card free space: %llu KiB", getFreeKB());
      auto freeSpaceGB = static_cast<float>(static_cast<float>(m_free_bytes) / (1024 * 1024 * 1024));
      ESP_LOGI(TAG, "SD card free space: %f GiB", freeSpaceGB);
    }

    return ESP_OK;
}

esp_err_t SDCardSDIO::checkSDCard() {
  // Must be checked first: on removal m_free_bytes keeps its last value until update() runs,
  // and callers use this as the go/no-go gate before starting a recording or an SD write.
  if (!m_mounted) {
    return ESP_ERR_NOT_FOUND;
  }
  if ((m_free_bytes > 0) && (m_free_bytes < (0.5 * 1024 * 1024 * 1024))) {
    ESP_LOGE(TAG, "Insufficent free space");
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

SDCardSDIO::~SDCardSDIO() {
  unmount();
}
