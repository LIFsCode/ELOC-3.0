#pragma once

#include <hal/gpio_types.h>
#include <driver/sdmmc_types.h>
#include <driver/sdspi_host.h>
#include "driver/sdmmc_host.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string>

class SDCardSDIO {
 public:
  /**
   * @brief Board hook reading the SD socket's card-detect switch.
   * @return true when a card is physically inserted
   * @note Injected as a function pointer rather than #included: the detect line sits on the
   *       PCA9557 IO expander, and including ElocSystem/ELOC_IOEXP from lib/sd_card would make
   *       the PlatformIO LDF dependency graph cyclic (ElocHardware already depends on sd_card).
   *       The hook applies the board polarity (SDCARD_DETECT_PRESENT_LEVEL).
   */
  typedef bool (*CardDetectFn)();

 private:
  /// Logical "SD card is usable". Cleared the moment a removal is detected, i.e. before the
  /// VFS is actually torn down, so that every isMounted() caller stops opening new files.
  bool m_mounted = false;
  /// The FATFS volume / sdmmc driver are still registered and must be released by unmount().
  /// Tracked separately from m_mounted because teardown has to wait until the writer tasks
  /// have closed their file handles.
  bool m_vfsRegistered = false;
  uint32_t m_failedMounts = 0;
  std::string m_mount_point = "";
  sdmmc_card_t *m_card = nullptr;
  sdmmc_host_t m_host = SDMMC_HOST_DEFAULT();

  /// Held by tasks doing file I/O on the card, and by update()/init()/unmount() while they
  /// mount or release the volume. Without it, tearing the FATFS down under a task that is inside
  /// fopen()/fwrite() is a use-after-free. Not needed by the wav writer, which is stopped via a
  /// task handshake. Recursive: update() holds it across init(), which calls unmount().
  SemaphoreHandle_t m_fsMutex = nullptr;

  CardDetectFn m_cardDetect = nullptr;
  /// Card detect is wired AND its polarity was confirmed against a successful mount.
  /// Until confirmed (or if it disagreed) update() falls back to failure-polling.
  bool m_cdValidated = false;
  /// The detect line was caught lying (it claimed "inserted" for a card that had stopped
  /// answering) or failed the polarity check. Sticky for this boot: it is never trusted again,
  /// otherwise the next mount would re-validate it and the next removal would pay for it again.
  bool m_cdRejected = false;
  uint32_t m_cdStableSinceMs = 0;  ///< millis() when the detect line last changed state
  bool m_cdInserted = false;       ///< last debounced detect reading, for edge detection
  uint32_t m_lastProbeMs = 0;      ///< millis() of the last filesystem probe / mount attempt

  uint32_t m_retryIntervalMs = 0;  ///< current mount-retry backoff

  /// Debounce/settle times for the mechanical detect switch
  static const uint32_t m_INSERT_SETTLE_MS = 300;
  static const uint32_t m_REMOVE_DEBOUNCE_MS = 100;
  /// Probe interval for a mounted card when there is no usable detect line. Each probe on a dead
  /// card costs ~1 s of blocking SDMMC timeouts, so it must not run every cycle.
  static const uint32_t m_BLIND_PROBE_INTERVAL_MS = 5000;
  /// Mount-retry backoff. A mount attempt into an empty socket blocks ~4 s in the SDMMC driver
  /// (send_op_cond retries), so retrying tightly would keep the calling task busy almost
  /// permanently. Doubles from MIN to MAX and resets on a successful mount or an insert edge.
  static const uint32_t m_MOUNT_RETRY_MIN_MS = 2000;
  static const uint32_t m_MOUNT_RETRY_MAX_MS = 10000;
  /// Retry interval while a trusted detect line reports a card in the socket: mounting a real
  /// card takes ~200 ms, so there is nothing to back off from.
  static const uint32_t m_MOUNT_RETRY_PRESENT_MS = 1000;

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

  /**
   * @brief Update @param m_free_bytes with free space of SD card
   * @note Not thread safe, so keep it private
   * @return esp_err_t
   */
  esp_err_t updateFreeSpace();

  /**
   * @brief Read and debounce the card-detect line
   * @return debounced presence; true when no detect hook is installed
   */
  bool pollCardDetect();

 public:
  SDCardSDIO();

  /**
   * @brief Initialize SD card
   * @param mount_point
   * @return esp_err_t
   */
  esp_err_t init(const char *mount_point);

  /**
   * @brief Release the FATFS volume and the SDMMC driver
   * @note Safe to call when nothing is mounted. The caller MUST have stopped every task that
   *       holds an open file on the card first (wav writer, SD logging) — unregistering the
   *       VFS while another task sits in fwrite() frees the FATFS object under it.
   * @return esp_err_t
   */
  esp_err_t unmount();

  /**
   * @brief Poll the card: detect removal, re-mount after insertion, refresh free space
   * @note Cheap when a validated card-detect line is available (one I2C read while no card is
   *       present). Without one it falls back to filesystem probing, throttled because each
   *       probe on a missing card blocks ~1 s in the SDMMC driver.
   * @note Never unmounts by itself — on removal it clears the mounted flag and returns an
   *       error so the caller can stop the writers before calling unmount().
   * @return ESP_OK when the card is mounted and usable, ESP_ERR_NOT_FOUND when it is not,
   *         ESP_ERR_INVALID_RESPONSE when a mounted card stopped answering
   */
  esp_err_t update();

  /**
   * @brief Install the board's card-detect hook. Call before init().
   */
  void setCardDetect(CardDetectFn fn) {
    m_cardDetect = fn;
  }

  /**
   * @brief Claim the card for file I/O from a task other than the one running update()
   * @note Only needed by asynchronous writers (e.g. the inference thread's results CSV) so a
   *       hot-swap unmount cannot pull the filesystem out from under them. Always pair with
   *       releaseFs(); check isMounted() after claiming, the card may have gone in the meantime.
   * @param timeoutMs how long to wait for the lock
   * @return true when the lock was taken
   */
  bool claimFs(uint32_t timeoutMs = 1000);

  /**
   * @brief Release the lock taken by claimFs()
   */
  void releaseFs();

  /**
   * @brief Whether removal/insertion is detected via the hardware detect line
   * @return false if no hook was installed, or its polarity failed the boot-time check
   */
  bool hasCardDetect() const {
    return (m_cardDetect != nullptr) && m_cdValidated;
  }

  /**
   * @brief Physical card presence according to the detect line
   * @return true if a card is inserted, or if there is no usable detect line (unknown -> assume
   *         present, so the filesystem-probing fallback still runs)
   */
  bool isCardInserted() const {
    return hasCardDetect() ? m_cdInserted : true;
  }

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
