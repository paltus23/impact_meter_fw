#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/* ------------------------------------------------------------------ keys */

/** NVS keys (max 15 chars each). */
#define SETTINGS_KEY_PROFILE_NUM    "profile_num"
#define SETTINGS_KEY_PRECAPTURE_MS  "precap_ms"
#define SETTINGS_KEY_CAPTURE_MS     "capture_ms"
#define SETTINGS_KEY_WIFI_SSID      "wifi_ssid"
#define SETTINGS_KEY_WIFI_PASS      "wifi_pass"

/* ------------------------------------------------------------------ wifi credential sizes */

#define SETTINGS_WIFI_SSID_MAX_LEN  33   /* 32 chars + null */
#define SETTINGS_WIFI_PASS_MAX_LEN  65   /* 64 chars + null */

/* ------------------------------------------------------------------ defaults */

#define SETTINGS_DEFAULT_PROFILE_NUM    0
#define SETTINGS_DEFAULT_PRECAPTURE_MS  200
#define SETTINGS_DEFAULT_CAPTURE_MS     1000

/* ------------------------------------------------------------------ valid ranges */

#define SETTINGS_MIN_PROFILE_NUM     0L
#define SETTINGS_MAX_PROFILE_NUM     INT32_MAX

#define SETTINGS_MIN_PRECAPTURE_MS   0L
#define SETTINGS_MAX_PRECAPTURE_MS   5000L

#define SETTINGS_MIN_CAPTURE_MS      1L
#define SETTINGS_MAX_CAPTURE_MS      60000L

/* ------------------------------------------------------------------ events */

/** Event bit set whenever any setting is persisted to NVS. */
#define SETTINGS_EVENT_CONFIG_CHANGED  BIT0

/**
 * Return the event group used to signal configuration changes.
 * Valid only after settings_init() has been called.
 */
EventGroupHandle_t settings_get_event_group(void);

/**
 * Set SETTINGS_EVENT_CONFIG_CHANGED in the event group.
 * Safe to call from any task context.
 */
void settings_notify_config_changed(void);

/* ------------------------------------------------------------------ init */

/**
 * Initialise NVS flash and open the settings namespace.
 * If the NVS partition has no free pages or contains an incompatible version
 * it is erased and re-initialised automatically.
 * Must be called once before any other settings_* function.
 */
esp_err_t settings_init(void);

/* ------------------------------------------------------------------ generic API */

/**
 * Read a signed 32-bit integer stored under @p key.
 * Returns ESP_ERR_NVS_NOT_FOUND when the key does not exist yet.
 */
esp_err_t settings_get_i32(const char *key, int32_t *out_val);

/**
 * Write a signed 32-bit integer under @p key and commit immediately.
 */
esp_err_t settings_set_i32(const char *key, int32_t val);

/**
 * Read an unsigned 32-bit integer stored under @p key.
 * Returns ESP_ERR_NVS_NOT_FOUND when the key does not exist yet.
 */
esp_err_t settings_get_u32(const char *key, uint32_t *out_val);

/**
 * Write an unsigned 32-bit integer under @p key and commit immediately.
 */
esp_err_t settings_set_u32(const char *key, uint32_t val);

/**
 * Read a null-terminated string stored under @p key into @p buf (size @p buf_len).
 * Returns ESP_ERR_NVS_NOT_FOUND when the key does not exist yet.
 */
esp_err_t settings_get_str(const char *key, char *buf, size_t buf_len);

/**
 * Write a null-terminated string under @p key and commit immediately.
 */
esp_err_t settings_set_str(const char *key, const char *val);

/**
 * Erase @p key from NVS.
 * Returns ESP_ERR_NVS_NOT_FOUND if the key does not exist.
 */
esp_err_t settings_erase_key(const char *key);

/* ------------------------------------------------------------------ named variables */

/** Profile number: selects which capture profile is active (default 0). */
esp_err_t settings_get_profile_num(int32_t *out_val);
esp_err_t settings_set_profile_num(int32_t val);

/** Pre-capture duration in milliseconds (default 500 ms). */
esp_err_t settings_get_precapture_ms(int32_t *out_val);
esp_err_t settings_set_precapture_ms(int32_t val);

/** Capture duration in milliseconds (default 2000 ms). */
esp_err_t settings_get_capture_ms(int32_t *out_val);
esp_err_t settings_set_capture_ms(int32_t val);

/** WiFi station SSID (empty string = no saved credentials). */
esp_err_t settings_get_wifi_ssid(char *buf, size_t buf_len);
esp_err_t settings_set_wifi_ssid(const char *ssid);

/** WiFi station password (may be empty for open networks). */
esp_err_t settings_get_wifi_pass(char *buf, size_t buf_len);
esp_err_t settings_set_wifi_pass(const char *pass);

#endif /* SETTINGS_H */
