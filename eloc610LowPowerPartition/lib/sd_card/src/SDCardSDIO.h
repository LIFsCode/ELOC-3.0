#pragma once

#include <hal/gpio_types.h>
#include <driver/sdmmc_types.h>
#include <driver/sdspi_host.h>
#include "driver/sdmmc_host.h"

#include <string>

class SDCardSDIO {
 private:
  bool m_mounted;
  std::string m_mount_point;
  sdmmc_card_t *m_card;
  sdmmc_host_t m_host = SDMMC_HOST_DEFAULT();

 public:
  SDCardSDIO();
  esp_err_t init(const char *mount_point);

  ~SDCardSDIO();
  const std::string &get_mount_point() { return m_mount_point; }

  bool isMounted() const {
    return m_mounted;
  }
  float getCapacityMB() const;
  float freeSpaceGB() const;
};
