#include <freertos/FreeRTOS.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
//#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#include "SDCard.h"

static const char *TAG = "SDC";

#define SPI_DMA_CHAN 1
extern bool gMountedSDCard;

SDCard::SDCard(const char *mount_point, gpio_num_t miso, gpio_num_t mosi, gpio_num_t clk, gpio_num_t cs)
{
  //newTom
  gMountedSDCard=false;
  esp_vfs_fat_sdmmc_unmount();
  //End newTom
  m_mount_point = mount_point;
  esp_err_t ret;
  // Options for mounting the filesystem.
  // If format_if_mount_failed is set to true, SD card will be partitioned and
  // formatted in case when mounting fails.
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 2,

      /**
     * If format_if_mount_failed is set, and mount fails, format the card
     * with given allocation unit size. Must be a power of 2, between sector
     * size and 128 * sector size.
     * For SD cards, sector size is always 512 bytes. For wear_levelling,
     * sector size is determined by CONFIG_WL_SECTOR_SIZE option.
     *
     * Using larger allocation unit size will result in higher read/write
     * performance and higher overhead when storing small files.
     *
     * Setting this field to 0 will result in allocation unit set to the
     * sector size.
     */
      .allocation_unit_size = 32 * 1024};  //was 16

  ESP_LOGI(TAG, "Initializing SPI card");

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
  slot_config.gpio_miso = miso;
  slot_config.gpio_mosi = mosi;
  slot_config.gpio_sck = clk;
  slot_config.gpio_cs = cs;

  ret = esp_vfs_fat_sdmmc_mount(m_mount_point.c_str(), &host, &slot_config, &mount_config, &m_card);

  if (ret != ESP_OK)
  {
    if (ret == ESP_FAIL)
    {
      ESP_LOGE(TAG, "Failed to mount filesystem. "
                    "If you want the card to be formatted, set the format_if_mount_failed");
    }
    else
    {
      ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                    "Make sure SD card lines have pull-up resistors in place.",
               esp_err_to_name(ret));
    }
    
    return;
  }
  gMountedSDCard=true;
  ESP_LOGI(TAG, "SPI card mounted at: %s", m_mount_point.c_str());

  // Card has been initialized, print its properties
  sdmmc_card_print_info(stdout, m_card);
}



SDCard::~SDCard()
{
  // All done, unmount partition and disable SDMMC or SPI peripheral
  ESP_LOGI(TAG, "Trying to unmount SPI Card");
  esp_vfs_fat_sdmmc_unmount();
   ESP_LOGI(TAG, "SPI Card unmounted");
}