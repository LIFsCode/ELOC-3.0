#pragma once

#include <hal/gpio_types.h>
#include <driver/sdmmc_types.h>
#include <driver/sdspi_host.h>
#include "driver/sdmmc_host.h"

#include <string>

class SDCardSDIO {
 private:
  bool m_mounted = false;
  std::string m_mount_point = "";
  sdmmc_card_t *m_card = nullptr;
  sdmmc_host_t m_host = SDMMC_HOST_DEFAULT();

  /**
   * @brief Store free space of SD card
   * @note For SD manufacturers 1 GB = 1000,000,000 bytes
   * @ref https://en.wikipedia.org/wiki/Gigabyte
   *      => Assuming a 128GB SD card = 128 * 1000,000,000
   *                                  = 128000000000 bytes
   *     uint32_t can only store approx 4GB
   */

  static_assert(UINT64_MAX == 18446744073709551615ULL, "uint64_t is not 64 bits");
  uint64_t m_free_bytes = 0;

 public:
  SDCardSDIO();

  /**
   * @brief Initialize SD card
   * @param mount_point
   * @return esp_err_t
   */
  esp_err_t init(const char *mount_point);

  /**
   * @brief Check SD mounted & update free space
   * @note If not mounted, attempt to mount
   * @return esp_err_t
   */
  esp_err_t update();

  /**
   * @brief Update @param m_free_bytes with free space of SD card
   * @return esp_err_t
   */
  esp_err_t updateFreeSpace();

  /**
   * @brief Check SD card mounted & more than 0.5GB free space
   *
   * @return esp_err_t
   */
  esp_err_t checkSDCard();

  /**
   * @brief Get mount point of SD card
   * @return const std::string&
   */
  const std::string &get_mount_point() { return m_mount_point; }

  /**
   * @brief Check if SD card is mounted
   * @return bool
   */
  bool isMounted() const {
    return m_mounted;
  }

  /**
   * @brief SD card capacity in MB
   * @note Assumes 1 MB = 1024 * 1024 bytes
   * @return float
   */
  float getCapacityMB() const;

  /**
   * @brief Get STORED free space of SD card
   * @note to update free space, call updateFreeSpace()
   * @return uint64_t
   */
  uint64_t getFreeBytes() const {
    return m_free_bytes;
  }

  /**
   * @brief Get STORED free space in KB
   * @note Assumes 1 KB = 1024 bytes
   * @note to update free space, call updateFreeSpace()
   * @return uint64_t
   */
  uint64_t getFreeKB() const {
    return m_free_bytes / 1024;
  }

  /**
   * @brief Get STORED free space in GB
   * @note Assumes 1 GB = 1024 * 1024 * 1024 bytes
   * @note to update free space, call updateFreeSpace()
   * @return float
   */
  float freeSpaceGB() const {
    return static_cast<float>(m_free_bytes) / (1024 * 1024 * 1024);
  }

  /**
   * @brief Destroy the SDCardSDIO object
   * @note Unmounts SD card
   */
  ~SDCardSDIO();
};
