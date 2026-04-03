#include "wifi.h"
#include "settings.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

#define AP_SSID "impact_meter"
#define AP_MAX_CONN 4
#define STA_TIMEOUT_MS 5000

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "wifi";

static EventGroupHandle_t s_event_group;
static bool s_sta_connected = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        s_sta_connected = false;
        xEventGroupSetBits(s_event_group, WIFI_FAIL_BIT);
        ESP_LOGI(TAG, "STA disconnected");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&e->ip_info.ip));
        s_sta_connected = true;
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Client " MACSTR " joined AP, AID=%d", MAC2STR(e->mac), e->aid);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Client " MACSTR " left AP, AID=%d", MAC2STR(e->mac), e->aid);
    }
}

static void start_ap_mode(void)
{
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = 1,
            .password = "",
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "AP mode — SSID: \"%s\", open network, IP: 192.168.4.1", AP_SSID);
}

void wifi_init(void)
{
    s_event_group = xEventGroupCreate();

    /* NVS is already initialised by settings_init(), but the call is idempotent. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Create both netif objects; only the active mode's interface will be used. */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    /* Load saved WiFi credentials. */
    char ssid[SETTINGS_WIFI_SSID_MAX_LEN] = {0};
    char pass[SETTINGS_WIFI_PASS_MAX_LEN] = {0};

    settings_get_wifi_ssid(ssid, sizeof(ssid));
    settings_get_wifi_pass(pass, sizeof(pass));

    if (ssid[0] != '\0')
    {
        /* Try connecting to the saved station. */
        wifi_config_t sta_cfg = {0};
        strlcpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid));
        strlcpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password));
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "Trying STA — SSID: \"%s\" password: \"%s\" (timeout %d ms)", ssid, pass, STA_TIMEOUT_MS);

        EventBits_t bits = xEventGroupWaitBits(s_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                               pdFALSE, pdFALSE,
                                               pdMS_TO_TICKS(STA_TIMEOUT_MS));

        if (bits & WIFI_CONNECTED_BIT)
        {
            ESP_LOGI(TAG, "STA connected to \"%s\"", ssid);
            return;
        }

        ESP_LOGW(TAG, "STA connection to \"%s\" failed or timed out — switching to AP", ssid);
        ESP_ERROR_CHECK(esp_wifi_stop());
    }
    else
    {
        ESP_LOGI(TAG, "No saved WiFi credentials — starting in AP mode");
    }

    start_ap_mode();
}

bool wifi_is_sta_connected(void)
{
    return s_sta_connected;
}
