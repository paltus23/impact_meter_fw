#include "data_storage.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_littlefs.h"
#include "esp_log.h"

#define TAG "data_storage"

#define LFS_PARTITION_LABEL "log"
#define LFS_MOUNT_POINT "/lfs"

/* Enough room for "/lfs/" + name + '\0'. sizeof includes the '\0' of the literal. */
#define PATH_BUF_SIZE (sizeof(LFS_MOUNT_POINT) + 1 + DATA_STORAGE_MAX_NAME_LEN)

struct data_storage_file
{
    FILE *fp;
    size_t total_bytes; /* bytes already in the file (existing + written this session) */
};

/* ------------------------------------------------------------------ helpers */

static void make_path(char *buf, const char *name)
{
    snprintf(buf, PATH_BUF_SIZE, LFS_MOUNT_POINT "/%s", name);
}

static size_t file_size_on_disk(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
    {
        return 0;
    }
    return (size_t)st.st_size;
}

/* ------------------------------------------------------------------ public */

esp_err_t data_storage_init(void)
{
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = LFS_MOUNT_POINT,
        .partition_label = LFS_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "mount failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info(LFS_PARTITION_LABEL, &total, &used);
    ESP_LOGI(TAG, "mounted: total=%u B  used=%u B", (unsigned)total, (unsigned)used);

    return ESP_OK;
}

esp_err_t data_storage_open_profile(const char *name, data_storage_file_t *out_handle)
{
    if (!name || !out_handle)
    {
        return ESP_ERR_INVALID_ARG;
    }

    char path[PATH_BUF_SIZE];
    make_path(path, name);

    size_t existing = file_size_on_disk(path);
    if (existing >= DATA_STORAGE_MAX_FILE_SIZE)
    {
        ESP_LOGE(TAG, "'%s' already at max size (%u B)", name, DATA_STORAGE_MAX_FILE_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp)
    {
        ESP_LOGE(TAG, "fopen failed for '%s'", path);
        return ESP_FAIL;
    }

    data_storage_file_t h = malloc(sizeof(struct data_storage_file));
    if (!h)
    {
        fclose(fp);
        return ESP_ERR_NO_MEM;
    }

    h->fp = fp;
    h->total_bytes = existing;
    *out_handle = h;

    ESP_LOGD(TAG, "opened '%s' for append (existing=%u B)", name, (unsigned)existing);
    return ESP_OK;
}

esp_err_t data_storage_write(data_storage_file_t handle, const void *data, size_t len)
{
    if (!handle || !data)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0)
    {
        return ESP_OK;
    }
    if (handle->total_bytes + len > DATA_STORAGE_MAX_FILE_SIZE)
    {
        ESP_LOGE(TAG, "write rejected: would exceed %u B limit (%u + %u)",
                 DATA_STORAGE_MAX_FILE_SIZE, (unsigned)handle->total_bytes, (unsigned)len);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t written = fwrite(data, 1, len, handle->fp);
    if (written != len)
    {
        ESP_LOGE(TAG, "fwrite: wrote %u of %u bytes", (unsigned)written, (unsigned)len);
        return ESP_FAIL;
    }

    handle->total_bytes += written;
    return ESP_OK;
}

esp_err_t data_storage_close(data_storage_file_t handle)
{
    if (!handle)
    {
        return ESP_ERR_INVALID_ARG;
    }

    fclose(handle->fp);
    free(handle);
    return ESP_OK;
}

esp_err_t data_storage_list_profiles(char (*names)[DATA_STORAGE_MAX_NAME_LEN],
                                     size_t max_count, size_t *out_count)
{
    if (!names || !out_count)
    {
        return ESP_ERR_INVALID_ARG;
    }

    DIR *dir = opendir(LFS_MOUNT_POINT);
    if (!dir)
    {
        ESP_LOGE(TAG, "opendir failed");
        return ESP_FAIL;
    }

    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < max_count)
    {
        if (entry->d_type == DT_REG)
        {
            snprintf(names[count], DATA_STORAGE_MAX_NAME_LEN, "%s", (char *)entry->d_name);
            count++;
        }
    }

    closedir(dir);
    *out_count = count;
    return ESP_OK;
}

esp_err_t data_storage_log_profiles(void)
{

    DIR *dir = opendir(LFS_MOUNT_POINT);
    if (!dir)
    {
        ESP_LOGE(TAG, "opendir failed");
        return ESP_FAIL;
    }

    struct dirent *entry;
    ESP_LOGI(TAG, "---  List of files  ---");
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG)
        {
            ESP_LOGI(TAG, "%s", entry->d_name);
        }
    }

    closedir(dir);

    return ESP_OK;
}

esp_err_t data_storage_get_profile_size(const char *name, size_t *out_size)
{
    if (!name || !out_size)
    {
        return ESP_ERR_INVALID_ARG;
    }

    char path[PATH_BUF_SIZE];
    make_path(path, name);

    struct stat st;
    if (stat(path, &st) != 0)
    {
        return ESP_ERR_NOT_FOUND;
    }

    *out_size = (size_t)st.st_size;
    return ESP_OK;
}

esp_err_t data_storage_read_profile(const char *name, size_t offset,
                                    void *buf, size_t len, size_t *out_bytes_read)
{
    if (!name || !buf)
    {
        return ESP_ERR_INVALID_ARG;
    }

    char path[PATH_BUF_SIZE];
    make_path(path, name);

    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        return ESP_ERR_NOT_FOUND;
    }

    if (fseek(fp, (long)offset, SEEK_SET) != 0)
    {
        fclose(fp);
        return ESP_FAIL;
    }

    size_t n = fread(buf, 1, len, fp);
    fclose(fp);

    if (out_bytes_read)
    {
        *out_bytes_read = n;
    }
    return ESP_OK;
}

esp_err_t data_storage_pwrite(data_storage_file_t handle, size_t offset,
                               const void *data, size_t len)
{
    if (!handle || !data)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0)
    {
        return ESP_OK;
    }
    if (offset + len > handle->total_bytes)
    {
        ESP_LOGE(TAG, "pwrite: offset %u + len %u exceeds total_bytes %u",
                 (unsigned)offset, (unsigned)len, (unsigned)handle->total_bytes);
        return ESP_ERR_INVALID_ARG;
    }

    long saved_pos = ftell(handle->fp);
    if (fseek(handle->fp, (long)offset, SEEK_SET) != 0)
    {
        ESP_LOGE(TAG, "pwrite: fseek to %u failed", (unsigned)offset);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, len, handle->fp);

    /* Restore file position regardless of outcome. */
    (void)fseek(handle->fp, saved_pos, SEEK_SET);

    if (written != len)
    {
        ESP_LOGE(TAG, "pwrite: wrote %u of %u bytes at offset %u",
                 (unsigned)written, (unsigned)len, (unsigned)offset);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t data_storage_delete_profile(const char *name)
{
    if (!name)
    {
        return ESP_ERR_INVALID_ARG;
    }

    char path[PATH_BUF_SIZE];
    make_path(path, name);

    if (remove(path) != 0)
    {
        ESP_LOGE(TAG, "remove failed for '%s'", path);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "deleted '%s'", name);
    return ESP_OK;
}
