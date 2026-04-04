#ifndef PROFILE_HEADER_H
#define PROFILE_HEADER_H

#include <stddef.h>
#include <stdint.h>

/**
 * Magic word stored at offset 0 of every capture profile.
 * Little-endian layout on disk: 0x41 0x43 0x45 0x4C → ASCII "ACEL".
 */
#define PROFILE_HEADER_MAGIC    0x4C454341UL

/** Current header format version. Increment when the layout changes. */
#define PROFILE_HEADER_VERSION  1U

/** Maximum length of the free-text device comment, including the null terminator. */
#define PROFILE_HEADER_COMMENT_LEN 128U

/**
 * Extendable binary header written at offset 0 of every capture profile.
 *
 * Memory layout (all multi-byte fields are little-endian):
 *
 *   Offset  Size  Field
 *   ------  ----  -----
 *    0       4    magic         — PROFILE_HEADER_MAGIC
 *    4       1    version       — PROFILE_HEADER_VERSION
 *    5       2    header_size   — total header size in bytes; sample data starts here
 *    7       4    odr_hz        — IEEE-754 single-precision ODR in Hz
 *   11       4    data_size     — sample data payload in bytes (0 until file is closed)
 *   15       4    time_created  — Unix epoch seconds (UTC) when the file was created
 *   19       2    hw_version    — hardware revision (high byte = major, low byte = minor)
 *   21       4    sn            — device serial number
 *   25     128    comment       — null-terminated free-text device comment
 *
 * Extensibility rule: a reader that finds header_size > sizeof(profile_header_t)
 * must skip the extra bytes and begin reading samples at offset header_size.
 * A reader that finds header_size < sizeof(profile_header_t) should treat
 * the missing fields as zero / unknown.
 */
typedef struct __attribute__((packed))
{
    uint32_t magic;                          /**< PROFILE_HEADER_MAGIC                              */
    uint8_t  version;                        /**< PROFILE_HEADER_VERSION                            */
    uint16_t header_size;                    /**< Total header size in bytes                        */
    float    odr_hz;                         /**< Output data rate in Hz                            */
    uint32_t data_size;                      /**< Sample payload size in bytes (filled on close)    */
    uint32_t time_created;                   /**< File creation time, Unix epoch seconds (UTC)      */
    uint16_t hw_version;                     /**< HW revision: high byte = major, low byte = minor  */
    uint32_t sn;                             /**< Device serial number                              */
    char     comment[PROFILE_HEADER_COMMENT_LEN]; /**< Free-text device comment, null-terminated   */
} profile_header_t;

#endif /* PROFILE_HEADER_H */
