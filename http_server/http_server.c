#include "http_server.h"
#include "data_storage.h"
#include "settings.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "http_server";

/* Symbols injected by the linker from the embedded HTML file. */
extern const char accel_vis_html_start[] asm("_binary_accelerometer_visualizer_html_start");
extern const char accel_vis_html_end[]   asm("_binary_accelerometer_visualizer_html_end");

/* GET /  →  serve the visualizer HTML page */
static esp_err_t home_handler(httpd_req_t *req)
{
    size_t html_len = (size_t)(accel_vis_html_end - accel_vis_html_start);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, accel_vis_html_start, (ssize_t)html_len);
    return ESP_OK;
}

/* GET /api/files  →  JSON array of profile names */
static esp_err_t list_files_handler(httpd_req_t *req)
{
#define MAX_PROFILES 64
    char names[MAX_PROFILES][DATA_STORAGE_MAX_NAME_LEN];
    size_t count = 0;

    esp_err_t err = data_storage_list_profiles(names, MAX_PROFILES, &count);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to list files");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "[");
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            httpd_resp_sendstr_chunk(req, ",");
        }
        httpd_resp_sendstr_chunk(req, "\"");
        httpd_resp_sendstr_chunk(req, names[i]);
        httpd_resp_sendstr_chunk(req, "\"");
    }
    httpd_resp_sendstr_chunk(req, "]");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
#undef MAX_PROFILES
}

/* GET /api/files/<name>  →  raw file contents (binary) */
static esp_err_t get_file_handler(httpd_req_t *req)
{
    static const char *prefix = "/api/files/";
    const char *name = req->uri + strlen(prefix);

    if (*name == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing file name");
        return ESP_OK;
    }

    size_t file_size = 0;
    esp_err_t err = data_storage_get_profile_size(name, &file_size);
    if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get file size");
        return ESP_OK;
    }

    /* Suggest a download filename to the browser. */
    char disposition[DATA_STORAGE_MAX_NAME_LEN + 32];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", name);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);

    /* Stream the file in 2 kB chunks to keep stack usage low. */
#define CHUNK_SIZE 2048
    uint8_t *buf = malloc(CHUNK_SIZE);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_OK;
    }

    size_t offset = 0;
    while (offset < file_size) {
        size_t to_read = file_size - offset;
        if (to_read > CHUNK_SIZE) {
            to_read = CHUNK_SIZE;
        }
        size_t bytes_read = 0;
        err = data_storage_read_profile(name, offset, buf, to_read, &bytes_read);
        if (err != ESP_OK || bytes_read == 0) {
            break;
        }
        if (httpd_resp_send_chunk(req, (char *)buf, (ssize_t)bytes_read) != ESP_OK) {
            ESP_LOGW(TAG, "Client disconnected while sending '%s'", name);
            break;
        }
        offset += bytes_read;
    }

    free(buf);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
#undef CHUNK_SIZE
}

/* DELETE /api/files/<name>  →  delete a specific profile */
static esp_err_t delete_file_handler(httpd_req_t *req)
{
    static const char *prefix = "/api/files/";
    const char *name = req->uri + strlen(prefix);

    if (*name == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing file name");
        return ESP_OK;
    }

    esp_err_t err = data_storage_delete_profile(name);
    if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete file");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deleted profile '%s'", name);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"deleted\":true}");
    return ESP_OK;
}

/* DELETE /api/files  →  delete all profiles */
static esp_err_t delete_all_files_handler(httpd_req_t *req)
{
#define MAX_PROFILES 64
    char names[MAX_PROFILES][DATA_STORAGE_MAX_NAME_LEN];
    size_t count = 0;

    esp_err_t err = data_storage_list_profiles(names, MAX_PROFILES, &count);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to list files");
        return ESP_OK;
    }

    size_t deleted = 0;
    for (size_t i = 0; i < count; i++) {
        err = data_storage_delete_profile(names[i]);
        if (err == ESP_OK) {
            deleted++;
        } else {
            ESP_LOGW(TAG, "Failed to delete profile '%s': %s", names[i], esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "Deleted %u/%u profiles", (unsigned)deleted, (unsigned)count);

    char resp[32];
    snprintf(resp, sizeof(resp), "{\"deleted\":%u}", (unsigned)deleted);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
#undef MAX_PROFILES
}

/* ------------------------------------------------------------------ /api/settings */

/* Build and send the current settings as a JSON object. */
static esp_err_t send_settings_json(httpd_req_t *req)
{
    int32_t profile_num  = 0;
    int32_t precapture_ms = 0;
    int32_t capture_ms   = 0;

    settings_get_profile_num(&profile_num);
    settings_get_precapture_ms(&precapture_ms);
    settings_get_capture_ms(&capture_ms);

    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_OK;
    }

    cJSON_AddNumberToObject(root, SETTINGS_KEY_PROFILE_NUM,   (double)profile_num);
    cJSON_AddNumberToObject(root, SETTINGS_KEY_PRECAPTURE_MS, (double)precapture_ms);
    cJSON_AddNumberToObject(root, SETTINGS_KEY_CAPTURE_MS,    (double)capture_ms);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!body)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, body);
    free(body);
    return ESP_OK;
}

/* GET /api/settings  →  current values as JSON */
static esp_err_t get_settings_handler(httpd_req_t *req)
{
    return send_settings_json(req);
}

/* POST /api/settings  →  update one or more settings from a JSON body */
static esp_err_t post_settings_handler(httpd_req_t *req)
{
#define SETTINGS_BODY_MAX 256
    if (req->content_len == 0 || req->content_len > SETTINGS_BODY_MAX)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body missing or too large");
        return ESP_OK;
    }

    char body[SETTINGS_BODY_MAX + 1];
    int received = httpd_req_recv(req, body, req->content_len);
    if (received <= 0)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read body");
        return ESP_OK;
    }
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    bool changed = false;
    cJSON *item;

    item = cJSON_GetObjectItem(root, SETTINGS_KEY_PROFILE_NUM);
    if (item && cJSON_IsNumber(item))
    {
        settings_set_profile_num((int32_t)item->valueint);
        changed = true;
    }

    item = cJSON_GetObjectItem(root, SETTINGS_KEY_PRECAPTURE_MS);
    if (item && cJSON_IsNumber(item))
    {
        settings_set_precapture_ms((int32_t)item->valueint);
        changed = true;
    }

    item = cJSON_GetObjectItem(root, SETTINGS_KEY_CAPTURE_MS);
    if (item && cJSON_IsNumber(item))
    {
        settings_set_capture_ms((int32_t)item->valueint);
        changed = true;
    }

    cJSON_Delete(root);

    if (changed)
    {
        settings_notify_config_changed();
        ESP_LOGI(TAG, "settings updated, config-changed event fired");
    }

    return send_settings_json(req);
#undef SETTINGS_BODY_MAX
}

/* ------------------------------------------------------------------ init */

void http_server_init(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 10;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    static const httpd_uri_t home_uri = {
        .uri     = "/",
        .method  = HTTP_GET,
        .handler = home_handler,
    };
    httpd_register_uri_handler(server, &home_uri);

    static const httpd_uri_t list_uri = {
        .uri     = "/api/files",
        .method  = HTTP_GET,
        .handler = list_files_handler,
    };
    httpd_register_uri_handler(server, &list_uri);

    static const httpd_uri_t file_uri = {
        .uri     = "/api/files/*",
        .method  = HTTP_GET,
        .handler = get_file_handler,
    };
    httpd_register_uri_handler(server, &file_uri);

    static const httpd_uri_t delete_all_uri = {
        .uri     = "/api/files",
        .method  = HTTP_DELETE,
        .handler = delete_all_files_handler,
    };
    httpd_register_uri_handler(server, &delete_all_uri);

    static const httpd_uri_t delete_file_uri = {
        .uri     = "/api/files/*",
        .method  = HTTP_DELETE,
        .handler = delete_file_handler,
    };
    httpd_register_uri_handler(server, &delete_file_uri);

    static const httpd_uri_t get_settings_uri = {
        .uri     = "/api/settings",
        .method  = HTTP_GET,
        .handler = get_settings_handler,
    };
    httpd_register_uri_handler(server, &get_settings_uri);

    static const httpd_uri_t post_settings_uri = {
        .uri     = "/api/settings",
        .method  = HTTP_POST,
        .handler = post_settings_handler,
    };
    httpd_register_uri_handler(server, &post_settings_uri);

    ESP_LOGI(TAG, "HTTP server started");
}
