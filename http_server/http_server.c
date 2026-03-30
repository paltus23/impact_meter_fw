#include "http_server.h"
#include "data_storage.h"
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

void http_server_init(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

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

    ESP_LOGI(TAG, "HTTP server started");
}
