#ifndef DATA_STORAGE_H
#define DATA_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** Maximum bytes that can be stored in a single profile file. */
#define DATA_STORAGE_MAX_FILE_SIZE 96000

/** Maximum length of a profile name, including the null terminator. */
#define DATA_STORAGE_MAX_NAME_LEN 32

/**
 * Opaque handle representing an open profile file.
 * Obtain via data_storage_open_profile(), release via data_storage_close().
 */
typedef struct data_storage_file *data_storage_file_t;

/**
 * Mount the LittleFS partition and make it ready for use.
 * Must be called once before any other data_storage_* function.
 * The partition is formatted automatically if mounting fails.
 */
esp_err_t data_storage_init(void);

/**
 * Open a named profile for appending.
 * If the file does not exist it is created; if it does exist subsequent
 * writes are appended after the existing content.
 * A single file may be opened and closed many times; each call picks up
 * where the previous one left off.
 *
 * @param name       Null-terminated profile name (no path separators).
 * @param out_handle Receives the opaque file handle on success.
 */
esp_err_t data_storage_open_profile(const char *name, data_storage_file_t *out_handle);

/**
 * Append @p len bytes from @p data to the profile represented by @p handle.
 * Returns ESP_ERR_INVALID_SIZE if the write would push the file past
 * DATA_STORAGE_MAX_FILE_SIZE.  The file is never partially written past the
 * limit; the call is rejected in its entirety.
 */
esp_err_t data_storage_write(data_storage_file_t handle, const void *data, size_t len);

/**
 * Flush and close the profile file, then free the handle.
 * @p handle must not be used after this call.
 */
esp_err_t data_storage_close(data_storage_file_t handle);

/**
 * Enumerate profile files stored in the filesystem.
 *
 * @param names     Caller-supplied 2-D array; each row is DATA_STORAGE_MAX_NAME_LEN bytes.
 * @param max_count Number of rows in @p names.
 * @param out_count Receives the number of entries written into @p names.
 */
esp_err_t data_storage_list_profiles(char (*names)[DATA_STORAGE_MAX_NAME_LEN],
                                     size_t max_count, size_t *out_count);

/**
 * Return the current size (in bytes) of the named profile.
 * Returns ESP_ERR_NOT_FOUND if the file does not exist.
 */
esp_err_t data_storage_get_profile_size(const char *name, size_t *out_size);

/**
 * Read @p len bytes from the named profile starting at byte @p offset.
 * The actual number of bytes copied into @p buf is written to @p out_bytes_read
 * (may be less than @p len when reading near the end of the file).
 */
esp_err_t data_storage_read_profile(const char *name, size_t offset,
                                    void *buf, size_t len, size_t *out_bytes_read);

/**
 * Permanently delete the named profile.
 * Returns ESP_ERR_NOT_FOUND if the file does not exist.
 */
esp_err_t data_storage_delete_profile(const char *name);

#endif /* DATA_STORAGE_H */
