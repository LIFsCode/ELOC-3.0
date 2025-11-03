/*
 * Session Persistence Implementation for ElocLora
 * RadioLib 7.2.1 Session and Nonces Persistence
 */

#include "ElocLora.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <esp_crc.h>

extern const char* TAG;
extern RTC_DATA_ATTR rtc_lorawan_session_t rtc_session;

// CRC32 calculation for session data integrity
uint32_t ElocLora::calculateCRC32(const uint8_t* data, size_t length) {
    return esp_crc32_le(0, data, length);
}

// Validate session data from RTC memory
bool ElocLora::isValidSession() {
    if (rtc_session.magic != LORAWAN_SESSION_MAGIC) {
        ESP_LOGW(TAG, "Invalid session magic: 0x%08X", rtc_session.magic);
        return false;
    }
    
    if (rtc_session.sessionSize == 0 || rtc_session.sessionSize > RADIOLIB_LORAWAN_SESSION_BUF_SIZE) {
        ESP_LOGW(TAG, "Invalid session size: %d", rtc_session.sessionSize);
        return false;
    }
    
    // Calculate CRC32 over session and nonces data
    uint32_t calculatedCRC = calculateCRC32(rtc_session.sessionData, rtc_session.sessionSize);
    calculatedCRC = esp_crc32_le(calculatedCRC, rtc_session.noncesData, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    
    if (calculatedCRC != rtc_session.crc32) {
        ESP_LOGW(TAG, "Session CRC mismatch: expected 0x%08X, got 0x%08X", rtc_session.crc32, calculatedCRC);
        return false;
    }
    
    ESP_LOGI(TAG, "Session validation successful");
    return true;
}

// Save current session to RTC memory
bool ElocLora::saveSessionToRTC() {
    if (!mInitDone) {
        ESP_LOGW(TAG, "Cannot save session - not initialized");
        return false;
    }
    
    // Get session buffer from RadioLib
    uint8_t* sessionBuffer = node.getBufferSession();
    if (sessionBuffer == nullptr) {
        ESP_LOGE(TAG, "Failed to get session buffer");
        return false;
    }
    
    // Get nonces buffer from RadioLib
    uint8_t* noncesBuffer = node.getBufferNonces();
    if (noncesBuffer == nullptr) {
        ESP_LOGE(TAG, "Failed to get nonces buffer");
        return false;
    }
    
    // Store in RTC memory
    rtc_session.magic = LORAWAN_SESSION_MAGIC;
    rtc_session.sessionSize = RADIOLIB_LORAWAN_SESSION_BUF_SIZE;
    memcpy(rtc_session.sessionData, sessionBuffer, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
    memcpy(rtc_session.noncesData, noncesBuffer, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    
    // Calculate and store CRC32
    rtc_session.crc32 = calculateCRC32(rtc_session.sessionData, rtc_session.sessionSize);
    rtc_session.crc32 = esp_crc32_le(rtc_session.crc32, rtc_session.noncesData, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    
    ESP_LOGI(TAG, "Session saved to RTC memory (size: %d bytes, CRC: 0x%08X)", rtc_session.sessionSize, rtc_session.crc32);
    
    // Also save nonces to NVS for permanent storage
    saveNoncesToNVS();
    
    return true;
}

// Load session from RTC memory
bool ElocLora::loadSessionFromRTC() {
    if (!isValidSession()) {
        ESP_LOGW(TAG, "No valid session in RTC memory");
        return false;
    }
    
    // IMPORTANT: Do NOT restore nonces from RTC memory!
    // Nonces are already loaded from NVS (permanent storage) which has the latest DevNonce.
    // Restoring nonces from RTC would overwrite with stale values and cause "dev_nonce_too_small" errors.
    
    // Restore ONLY session to RadioLib (not nonces!)
    int16_t state = node.setBufferSession(rtc_session.sessionData);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "Failed to restore session: %d", state);
        return false;
    }
    
    ESP_LOGI(TAG, "Session restored from RTC memory (nonces kept from NVS)");
    return true;
}

// Save nonces to NVS for permanent storage
bool ElocLora::saveNoncesToNVS() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("lorawan", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }
    
    // Save nonces buffer
    err = nvs_set_blob(nvs_handle, "nonces", rtc_session.noncesData, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save nonces to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Nonces saved to NVS");
        return true;
    }
    
    ESP_LOGE(TAG, "Failed to commit nonces to NVS: %s", esp_err_to_name(err));
    return false;
}

// Load nonces from NVS and apply them to RadioLib
bool ElocLora::loadNoncesFromNVS() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("lorawan", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No NVS partition found - first boot, will use default nonces");
        } else {
            ESP_LOGW(TAG, "Failed to open NVS for reading: %s", esp_err_to_name(err));
        }
        return false;
    }
    
    size_t required_size = RADIOLIB_LORAWAN_NONCES_BUF_SIZE;
    uint8_t noncesBuffer[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
    err = nvs_get_blob(nvs_handle, "nonces", noncesBuffer, &required_size);
    nvs_close(nvs_handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No nonces saved in NVS - first boot, will start fresh");
        return false;
    }
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load nonces from NVS: %s", esp_err_to_name(err));
        return false;
    }
    
    if (required_size != RADIOLIB_LORAWAN_NONCES_BUF_SIZE) {
        ESP_LOGW(TAG, "Invalid nonces size in NVS: %d bytes (expected %d)", required_size, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
        return false;
    }
    
    // Update RTC memory with NVS nonces
    memcpy(rtc_session.noncesData, noncesBuffer, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    
    // CRITICAL: Apply nonces to RadioLib node!
    int16_t state = node.setBufferNonces(noncesBuffer);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "Failed to apply nonces to RadioLib: %d", state);
        return false;
    }
    
    ESP_LOGI(TAG, "Nonces loaded from NVS and applied to RadioLib");
    return true;
}
