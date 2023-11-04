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

SDCardSDIO::SDCardSDIO(const char *mount_point) : m_mounted(false)
{
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
  // Enable internal pullups on enabled pins. The internal pullups
  // are insufficient however, please make sure 10k external pullups are
  // connected on the bus. This is for debug / example purpose only.
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  ret = esp_vfs_fat_sdmmc_mount(m_mount_point.c_str(), &m_host, &slot_config, &mount_config, &m_card);

  if (ret != ESP_OK)
  {  
    if (ret == ESP_FAIL)
    {
      ESP_LOGE(TAG, "Failed to mount filesystem. "
                    "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
    }
    else
    {
      ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                    "Make sure SD card lines have pull-up resistors in place.",
               esp_err_to_name(ret));
    }
    return;
  }
  m_mounted = true;
  ESP_LOGI(TAG, "SDCard mounted at: %s", m_mount_point.c_str());

  // Card has been initialized, print its properties
  sdmmc_card_print_info(stdout, m_card);
}

SDCardSDIO::~SDCardSDIO()
{
  ESP_LOGI(TAG, "Trying to unmount SDIO card" );
  // All done, unmount partition and disable SDMMC or SPI peripheral
  esp_vfs_fat_sdcard_unmount(m_mount_point.c_str(), m_card);
  ESP_LOGI(TAG, "SDIO card unmounted");
}

  float SDCardSDIO::getCapacityMB() const {
    if (!m_mounted) {
      return 0;
    }
    return static_cast<float>(m_card->csd.capacity * m_card->csd.sector_size) / (1024.0 * 1024.0);
  }

float SDCardSDIO::freeSpaceGB() const {

    FATFS *fs;
    uint32_t fre_clust, fre_sect, tot_sect;
    FRESULT res;
    /* Get volume information and free clusters of drive 0 */
    res = f_getfree(m_mount_point.c_str(), &fre_clust, &fs);
    if (res != FR_OK) {
      return 0;
    }
    /* Get total sectors and free sectors */
    tot_sect = (fs->n_fatent - 2) * fs->csize;
    fre_sect = fre_clust * fs->csize;

    /* Print the free space (assuming 512 bytes/sector) */
    printf("%10u KiB total drive space.\n%10u KiB available.\n", tot_sect / 2, fre_sect / 2);

    float freeSpaceGB = float((float)fre_sect / 1048576.0 / 2.0);
    printf("\n %2.1f GB free\n", freeSpaceGB);
    return freeSpaceGB;
}

