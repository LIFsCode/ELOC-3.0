#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#include "SDCardSDIO.h"
static const char *TAG = "SDC";

#define SPI_DMA_CHAN 1

SDCardSDIO::SDCardSDIO() : m_mounted(false), m_card(nullptr) {
}

esp_err_t SDCardSDIO::init(const char *mount_point) {
  m_mount_point = mount_point;
  esp_err_t ret;
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
      ESP_LOGE(TAG, "Failed to mount filesystem");
    } else {
      ESP_LOGE(TAG, "Failed to initialize the card (%s)", esp_err_to_name(ret));
    }
    return ret;
  }

  m_mounted = true;
  ESP_LOGI(TAG, "SDCard mounted at: %s", m_mount_point.c_str());

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

esp_err_t SDCardSDIO::update() {
    if (!m_mounted) {
        ESP_LOGW(TAG, "SD card not mounted, attempting to mount");
        if (m_mount_point.empty()) {
          ESP_LOGE(TAG, "Mount point not set");
          return ESP_ERR_INVALID_STATE;
        }
        init(m_mount_point.c_str());
        // Successfully mounted?
        if (!m_mounted) {
            return ESP_ERR_NOT_FOUND;
        }
    }

    return updateFreeSpace();
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
    uint64_t fre_sect = fre_clust * fs->csize;
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
  if ((m_free_bytes > 0) && (m_free_bytes < (0.5 * 1024 * 1024 * 1024))) {
    ESP_LOGE(TAG, "Insufficent free space");
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

SDCardSDIO::~SDCardSDIO() {
  esp_vfs_fat_sdcard_unmount(m_mount_point.c_str(), m_card);
}
