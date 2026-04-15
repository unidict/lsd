//
//  lsd_bitstream.c
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#include "lsd_bitstream.h"
#include "lsd_platform.h"
#include <stdlib.h>
#include <string.h>

// ============================================================
// Internal structure definitions
// ============================================================

#define LSD_READ_CACHE_SIZE 8192

struct lsd_bitstream {
    FILE *file;           // File handle
    size_t byte_pos;      // Current logical byte position
    uint8_t bit_pos;      // Current bit position (0-7, from MSB to LSB)
    uint8_t current_byte; // Byte being consumed bit by bit

    // Memory mode
    uint8_t *data;      // Memory buffer
    size_t data_size;   // Buffer size
    bool is_memory;       // Whether reading from memory

    // File read buffer (file mode only)
    // file_buffer caches data in the file range [buffer_offset, buffer_offset + buffer_len),
    // file position byte_pos corresponds to file_buffer[byte_pos - buffer_offset]
    uint8_t *file_buffer;      // Read buffer
    size_t buffer_offset;      // File start offset corresponding to the buffer
    size_t buffer_size;         // Number of valid bytes in the buffer

    // XOR decryption state
    bool xor_enabled;     // Whether XOR decryption is enabled
    uint8_t xor_key;      // Current XOR key
};

// ============================================================
// XOR decryption table (same as used by Lingvo)
// ============================================================
static const uint8_t xor_pad[256] = {
    0x9C, 0xDF, 0x9B, 0xF3, 0xBE, 0x3A, 0x83, 0xD8,
    0xC9, 0xF5, 0x50, 0x98, 0x35, 0x4E, 0x7F, 0xBB,
    0x89, 0xC7, 0xE9, 0x6B, 0xC4, 0xC8, 0x4F, 0x85,
    0x1A, 0x10, 0x43, 0x66, 0x65, 0x57, 0x55, 0x54,
    0xB4, 0xFF, 0xD7, 0x17, 0x06, 0x31, 0xAC, 0x4B,
    0x42, 0x53, 0x5A, 0x46, 0xC5, 0xF8, 0xCA, 0x5E,
    0x18, 0x38, 0x5D, 0x91, 0xAA, 0xA5, 0x58, 0x23,
    0x67, 0xBF, 0x30, 0x3C, 0x8C, 0xCF, 0xD5, 0xA8,
    0x20, 0xEE, 0x0B, 0x8E, 0xA6, 0x5B, 0x49, 0x3F,
    0xC0, 0xF4, 0x13, 0x80, 0xCB, 0x7B, 0xA7, 0x1D,
    0x81, 0x8B, 0x01, 0xDD, 0xE3, 0x4C, 0x9A, 0xCE,
    0x40, 0x72, 0xDE, 0x0F, 0x26, 0xBD, 0x3B, 0xA3,
    0x05, 0x37, 0xE1, 0x5F, 0x9D, 0x1E, 0xCD, 0x69,
    0x6E, 0xAB, 0x6D, 0x6C, 0xC3, 0x71, 0x1F, 0xA9,
    0x84, 0x63, 0x45, 0x76, 0x25, 0x70, 0xD6, 0x8F,
    0xFD, 0x04, 0x2E, 0x2A, 0x22, 0xF0, 0xB8, 0xF2,
    0xB6, 0xD0, 0xDA, 0x62, 0x75, 0xB7, 0x77, 0x34,
    0xA2, 0x41, 0xB9, 0xB1, 0x74, 0xE4, 0x95, 0x1B,
    0x3E, 0xE7, 0x00, 0xBC, 0x93, 0x7A, 0xE8, 0x86,
    0x59, 0xA0, 0x92, 0x11, 0xF7, 0xFE, 0x03, 0x2F,
    0x28, 0xFA, 0x27, 0x02, 0xE5, 0x39, 0x21, 0x96,
    0x33, 0xD1, 0xB2, 0x7C, 0xB3, 0x73, 0xC6, 0xE6,
    0xA1, 0x52, 0xFB, 0xD4, 0x9E, 0xB0, 0xE2, 0x16,
    0x97, 0x08, 0xF6, 0x4A, 0x78, 0x29, 0x14, 0x12,
    0x4D, 0xC1, 0x99, 0xBA, 0x0D, 0x3D, 0xEF, 0x19,
    0xAF, 0xF9, 0x6F, 0x0A, 0x6A, 0x47, 0x36, 0x82,
    0x07, 0x9F, 0x7D, 0xA4, 0xEA, 0x44, 0x09, 0x5C,
    0x8D, 0xCC, 0x87, 0x88, 0x2D, 0x8A, 0xEB, 0x2C,
    0xB5, 0xE0, 0x32, 0xAD, 0xD3, 0x61, 0xAE, 0x15,
    0x60, 0xF1, 0x48, 0x0E, 0x7E, 0x94, 0x51, 0x0C,
    0xEC, 0xDB, 0xD2, 0x64, 0xDC, 0xFC, 0xC2, 0x56,
    0x24, 0xED, 0x2B, 0xD9, 0x1C, 0x68, 0x90, 0x79
};

// ============================================================
// Create and destroy
// ============================================================

lsd_bitstream *lsd_bitstream_create_from_file(FILE *file) {
    if (!file) return NULL;

    lsd_bitstream *bstr = calloc(1, sizeof(lsd_bitstream));
    if (!bstr) return NULL;

    bstr->file_buffer = malloc(LSD_READ_CACHE_SIZE);
    if (!bstr->file_buffer) {
        free(bstr);
        return NULL;
    }

    bstr->file = file;

    return bstr;
}

lsd_bitstream *lsd_bitstream_create_from_memory(const uint8_t *buffer, size_t size) {
    if (!buffer || size == 0) return NULL;

    lsd_bitstream *bstr = calloc(1, sizeof(lsd_bitstream));
    if (!bstr) return NULL;

    bstr->data = malloc(size);
    if (!bstr->data) {
        free(bstr);
        return NULL;
    }
    memcpy(bstr->data, buffer, size);

    bstr->data_size = size;
    bstr->is_memory = true;

    return bstr;
}

void lsd_bitstream_destroy(lsd_bitstream *bstr) {
    if (!bstr) return;

    if (bstr->is_memory) {
        free(bstr->data);
    } else {
        free(bstr->file_buffer);
    }

    free(bstr);
}

// ============================================================
// Internal helper functions
// ============================================================

/**
 * Fill file read buffer
 */
static bool file_buffer_refill(lsd_bitstream *bstr, size_t file_offset) {
    lsd_fseek(bstr->file, file_offset, SEEK_SET);
    bstr->buffer_offset = file_offset;
    bstr->buffer_size = fread(bstr->file_buffer, 1, LSD_READ_CACHE_SIZE, bstr->file);
    return bstr->buffer_size > 0;
}

/**
 * Read one byte from data source and advance position (without XOR)
 */
static uint8_t bitstream_fetch_byte(lsd_bitstream *bstr) {
    if (bstr->is_memory) {
        if (bstr->byte_pos < bstr->data_size) {
            return bstr->data[bstr->byte_pos++];
        }
        return 0;
    }

    // File mode: check cache hit
    if (bstr->byte_pos >= bstr->buffer_offset &&
        bstr->byte_pos < bstr->buffer_offset + bstr->buffer_size) {
        return bstr->file_buffer[bstr->byte_pos++ - bstr->buffer_offset];
    }

    // Cache miss, refill
    if (!file_buffer_refill(bstr, bstr->byte_pos)) return 0;
    return bstr->file_buffer[bstr->byte_pos++ - bstr->buffer_offset];
}

/**
 * Batch read bytes from data source and advance position (without XOR)
 */
static size_t bitstream_read_raw(lsd_bitstream *bstr, uint8_t *dst, size_t count) {
    if (bstr->is_memory) {
        size_t available = bstr->data_size - bstr->byte_pos;
        size_t to_read = (count < available) ? count : available;
        memcpy(dst, bstr->data + bstr->byte_pos, to_read);
        bstr->byte_pos += to_read;
        return to_read;
    }

    // File mode: read through cache
    size_t total = 0;
    while (total < count) {
        if (bstr->byte_pos >= bstr->buffer_offset &&
            bstr->byte_pos < bstr->buffer_offset + bstr->buffer_size) {
            size_t off = bstr->byte_pos - bstr->buffer_offset;
            size_t avail = bstr->buffer_size - off;
            size_t n = (count - total < avail) ? (count - total) : avail;
            memcpy(dst + total, bstr->file_buffer + off, n);
            bstr->byte_pos += n;
            total += n;
        } else {
            if (!file_buffer_refill(bstr, bstr->byte_pos)) break;
        }
    }
    return total;
}

static uint8_t bitstream_read_single_bit(lsd_bitstream *bstr) {
    // If at byte boundary, read a new byte
    if (bstr->bit_pos == 0) {
        uint8_t raw_byte = bitstream_fetch_byte(bstr);

        if (bstr->xor_enabled) {
            bstr->current_byte = raw_byte ^ bstr->xor_key;
            bstr->xor_key = xor_pad[raw_byte];
        } else {
            bstr->current_byte = raw_byte;
        }
    }

    // Read from MSB to LSB
    uint8_t bit = (bstr->current_byte >> (7 - bstr->bit_pos)) & 1;
    bstr->bit_pos = (bstr->bit_pos + 1) % 8;

    return bit;
}

// ============================================================
// Bit operations (MSB-first)
// ============================================================

uint32_t lsd_bitstream_read(lsd_bitstream *bstr, uint8_t bit_count) {
    if (!bstr || bit_count == 0 || bit_count > 32) return 0;

    uint32_t result = 0;
    for (uint8_t i = 0; i < bit_count; i++) {
        result <<= 1;
        result |= bitstream_read_single_bit(bstr);
    }

    return result;
}

size_t lsd_bitstream_read_bytes(lsd_bitstream *bstr, void *dest, size_t byte_count) {
    if (!bstr || !dest || byte_count == 0) return 0;

    // Align to byte boundary first
    lsd_bitstream_align_to_byte(bstr);

    uint8_t *dst = (uint8_t *)dest;
    size_t bytes_read = bitstream_read_raw(bstr, dst, byte_count);

    // If XOR is enabled, decrypt the bytes read
    if (bstr->xor_enabled) {
        for (size_t i = 0; i < bytes_read; i++) {
            uint8_t original_byte = dst[i];
            dst[i] ^= bstr->xor_key;
            bstr->xor_key = xor_pad[original_byte];
        }
    }

    return bytes_read;
}

void lsd_bitstream_align_to_byte(lsd_bitstream *bstr) {
    if (bstr) {
        bstr->bit_pos = 0;
    }
}

// ============================================================
// Position operations
// ============================================================

void lsd_bitstream_seek(lsd_bitstream *bstr, size_t byte_pos) {
    if (!bstr) return;

    if (bstr->is_memory) {
        bstr->byte_pos = (byte_pos <= bstr->data_size) ? byte_pos : bstr->data_size;
    } else {
        bstr->byte_pos = byte_pos;
    }

    bstr->bit_pos = 0;
    bstr->current_byte = 0;

    if (bstr->xor_enabled) {
        bstr->xor_key = 0x7F;
    }
}

size_t lsd_bitstream_tell(lsd_bitstream *bstr) {
    if (!bstr) return 0;

    // If not at byte boundary, return the previous byte's position
    if (bstr->bit_pos != 0) {
        return bstr->byte_pos - 1;
    }
    return bstr->byte_pos;
}

// ============================================================
// XOR decryption mode
// ============================================================

void lsd_bitstream_enable_xor(lsd_bitstream *bstr) {
    if (!bstr) return;
    bstr->xor_enabled = true;
    bstr->xor_key = 0x7F;
    bstr->bit_pos = 0;
}

void lsd_bitstream_disable_xor(lsd_bitstream *bstr) {
    if (!bstr) return;
    bstr->xor_enabled = false;
    bstr->bit_pos = 0;
}

// ============================================================
// Utility functions
// ============================================================

uint8_t lsd_bit_length(uint32_t num) {
    uint8_t len = 1;
    while ((num >>= 1) != 0) {
        len++;
    }
    return len;
}

uint16_t lsd_reverse16(uint16_t n) {
    return (uint16_t)((n >> 8) | (n << 8));
}

uint32_t lsd_reverse32(uint32_t n) {
    return ((n >> 24) & 0x000000FF) |
           ((n >> 8)  & 0x0000FF00) |
           ((n << 8)  & 0x00FF0000) |
           ((n << 24) & 0xFF000000);
}

uint16_t *lsd_bitstream_read_utf16_string(lsd_bitstream *bstr, int len, bool big_endian, size_t *out_len) {
    if (!bstr || len <= 0 || !out_len) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    uint16_t *str = malloc((len + 1) * sizeof(uint16_t));
    if (!str) {
        *out_len = 0;
        return NULL;
    }

    lsd_bitstream_read_bytes(bstr, str, len * sizeof(uint16_t));

    if (big_endian) {
        for (int i = 0; i < len; i++) {
            str[i] = lsd_reverse16(str[i]);
        }
    }
    str[len] = 0;

    *out_len = len;
    return str;
}
