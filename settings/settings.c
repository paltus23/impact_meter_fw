#include "settings.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define TAG "settings"

#define NVS_NAMESPACE "settings"

/* Handle kept open for the lifetime of the application. */
static nvs_handle_t s_handle;
static bool s_initialised = false;
static EventGroupHandle_t s_event_group;

/* ------------------------------------------------------------------ init */

esp_err_t settings_init(void)
{
    s_event_group = xEventGroupCreate();
    if (!s_event_group)
    {
        ESP_LOGE(TAG, "failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS partition needs erase (%s)", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_flash_init failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    s_initialised = true;
    ESP_LOGI(TAG, "initialised (namespace=\"" NVS_NAMESPACE "\")");
    return ESP_OK;
}

EventGroupHandle_t settings_get_event_group(void)
{
    return s_event_group;
}

void settings_notify_config_changed(void)
{
    if (s_event_group)
    {
        xEventGroupSetBits(s_event_group, SETTINGS_EVENT_CONFIG_CHANGED);
    }
}

/* ------------------------------------------------------------------ internal helpers */

static esp_err_t check_init(void)
{
    if (!s_initialised)
    {
        ESP_LOGE(TAG, "not initialised – call settings_init() first");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ generic API */

esp_err_t settings_get_i32(const char *key, int32_t *out_val)
{
    if (!key || !out_val) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = check_init();
    if (ret != ESP_OK) return ret;

    ret = nvs_get_i32(s_handle, key, out_val);
    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGD(TAG, "key \"%s\" not found", key);
    }
    else if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "get_i32 \"%s\" failed (%s)", key, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t settings_set_i32(const char *key, int32_t val)
{
    if (!key) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = check_init();
    if (ret != ESP_OK) return ret;

    ret = nvs_set_i32(s_handle, key, val);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "set_i32 \"%s\" failed (%s)", key, esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_commit(s_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "commit after set_i32 \"%s\" failed (%s)", key, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t settings_get_u32(const char *key, uint32_t *out_val)
{
    if (!key || !out_val) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = check_init();
    if (ret != ESP_OK) return ret;

    ret = nvs_get_u32(s_handle, key, out_val);
    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGD(TAG, "key \"%s\" not found", key);
    }
    else if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "get_u32 \"%s\" failed (%s)", key, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t settings_set_u32(const char *key, uint32_t val)
{
    if (!key) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = check_init();
    if (ret != ESP_OK) return ret;

    ret = nvs_set_u32(s_handle, key, val);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "set_u32 \"%s\" failed (%s)", key, esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_commit(s_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "commit after set_u32 \"%s\" failed (%s)", key, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t settings_get_str(const char *key, char *buf, size_t buf_len)
{
    if (!key || !buf || buf_len == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = check_init();
    if (ret != ESP_OK) return ret;

    ret = nvs_get_str(s_handle, key, buf, &buf_len);
    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGD(TAG, "key \"%s\" not found", key);
    }
    else if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "get_str \"%s\" failed (%s)", key, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t settings_set_str(const char *key, const char *val)
{
    if (!key || !val) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = check_init();
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(s_handle, key, val);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "set_str \"%s\" failed (%s)", key, esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_commit(s_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "commit after set_str \"%s\" failed (%s)", key, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t settings_erase_key(const char *key)
{
    if (!key) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = check_init();
    if (ret != ESP_OK) return ret;

    ret = nvs_erase_key(s_handle, key);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "erase_key \"%s\" failed (%s)", key, esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_commit(s_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "commit after erase_key \"%s\" failed (%s)", key, esp_err_to_name(ret));
    }
    return ret;
}

/* ------------------------------------------------------------------ named variables */

static esp_err_t get_i32_with_default(const char *key, int32_t default_val, int32_t *out_val)
{
    esp_err_t ret = settings_get_i32(key, out_val);
    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        *out_val = default_val;
        return ESP_OK;
    }
    return ret;
}

esp_err_t settings_get_profile_num(int32_t *out_val)
{
    esp_err_t ret = get_i32_with_default(SETTINGS_KEY_PROFILE_NUM, SETTINGS_DEFAULT_PROFILE_NUM, out_val);
    if (ret != ESP_OK) return ret;
    if (*out_val < SETTINGS_MIN_PROFILE_NUM) *out_val = SETTINGS_MIN_PROFILE_NUM;
    if (*out_val > SETTINGS_MAX_PROFILE_NUM) *out_val = SETTINGS_MAX_PROFILE_NUM;
    return ESP_OK;
}

esp_err_t settings_set_profile_num(int32_t val)
{
    if (val < SETTINGS_MIN_PROFILE_NUM || val > SETTINGS_MAX_PROFILE_NUM)
    {
        ESP_LOGE(TAG, "profile_num %ld out of range [%ld, %ld]",
                 (long)val, SETTINGS_MIN_PROFILE_NUM, SETTINGS_MAX_PROFILE_NUM);
        return ESP_ERR_INVALID_ARG;
    }
    return settings_set_i32(SETTINGS_KEY_PROFILE_NUM, val);
}

esp_err_t settings_get_precapture_ms(int32_t *out_val)
{
    esp_err_t ret = get_i32_with_default(SETTINGS_KEY_PRECAPTURE_MS, SETTINGS_DEFAULT_PRECAPTURE_MS, out_val);
    if (ret != ESP_OK) return ret;
    if (*out_val < SETTINGS_MIN_PRECAPTURE_MS) *out_val = SETTINGS_MIN_PRECAPTURE_MS;
    if (*out_val > SETTINGS_MAX_PRECAPTURE_MS) *out_val = SETTINGS_MAX_PRECAPTURE_MS;
    return ESP_OK;
}

esp_err_t settings_set_precapture_ms(int32_t val)
{
    if (val < SETTINGS_MIN_PRECAPTURE_MS || val > SETTINGS_MAX_PRECAPTURE_MS)
    {
        ESP_LOGE(TAG, "precapture_ms %ld out of range [%ld, %ld]",
                 (long)val, SETTINGS_MIN_PRECAPTURE_MS, SETTINGS_MAX_PRECAPTURE_MS);
        return ESP_ERR_INVALID_ARG;
    }
    return settings_set_i32(SETTINGS_KEY_PRECAPTURE_MS, val);
}

esp_err_t settings_get_capture_ms(int32_t *out_val)
{
    esp_err_t ret = get_i32_with_default(SETTINGS_KEY_CAPTURE_MS, SETTINGS_DEFAULT_CAPTURE_MS, out_val);
    if (ret != ESP_OK) return ret;
    if (*out_val < SETTINGS_MIN_CAPTURE_MS) *out_val = SETTINGS_MIN_CAPTURE_MS;
    if (*out_val > SETTINGS_MAX_CAPTURE_MS) *out_val = SETTINGS_MAX_CAPTURE_MS;
    return ESP_OK;
}

esp_err_t settings_set_capture_ms(int32_t val)
{
    if (val < SETTINGS_MIN_CAPTURE_MS || val > SETTINGS_MAX_CAPTURE_MS)
    {
        ESP_LOGE(TAG, "capture_ms %ld out of range [%ld, %ld]",
                 (long)val, SETTINGS_MIN_CAPTURE_MS, SETTINGS_MAX_CAPTURE_MS);
        return ESP_ERR_INVALID_ARG;
    }
    return settings_set_i32(SETTINGS_KEY_CAPTURE_MS, val);
}

/* ------------------------------------------------------------------ wifi credentials */

esp_err_t settings_get_wifi_ssid(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = settings_get_str(SETTINGS_KEY_WIFI_SSID, buf, buf_len);
    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        buf[0] = '\0';
        return ESP_OK;
    }
    return ret;
}

esp_err_t settings_set_wifi_ssid(const char *ssid)
{
    if (!ssid) return ESP_ERR_INVALID_ARG;
    return settings_set_str(SETTINGS_KEY_WIFI_SSID, ssid);
}

esp_err_t settings_get_wifi_pass(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = settings_get_str(SETTINGS_KEY_WIFI_PASS, buf, buf_len);
    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        buf[0] = '\0';
        return ESP_OK;
    }
    return ret;
}

esp_err_t settings_set_wifi_pass(const char *pass)
{
    if (!pass) return ESP_ERR_INVALID_ARG;
    return settings_set_str(SETTINGS_KEY_WIFI_PASS, pass);
}
