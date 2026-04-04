#include "rtc_server.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "rtc_server";

static void sntp_sync_cb(struct timeval *tv)
{
    time_t now = tv->tv_sec;
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "Time synced: %s UTC", buf);
}

void rtc_server_init(void)
{
    setenv("TZ", "UTC0", 1);
    tzset();
    sntp_set_time_sync_notification_cb(sntp_sync_cb);
    ESP_LOGI(TAG, "RTC initialized");

    rtc_server_sync_from_host("pool.ntp.org");
}

void rtc_server_sync_from_host(const char *ip_str)
{
    ESP_LOGI(TAG, "Starting NTP sync from %s", ip_str);

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ip_str);
    esp_sntp_init();
}

time_t rtc_server_get_time(void)
{
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    return tv_now.tv_sec;
}