//
//  dsl_dictzip.c
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#include "dsl_dictzip.h"
#include "lsd_platform.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <limits.h>

// ============================================================
// Constants
// ============================================================
#define DICTZIP_MAGIC1     0x1f
#define DICTZIP_MAGIC2     0x8b
#define DICTZIP_FEXTRA     0x04
#define DICTZIP_FNAME      0x08
#define DICTZIP_COMMENT    0x10
#define DICTZIP_FHCRC      0x02
#define DICTZIP_RND_S1     'R'
#define DICTZIP_RND_S2     'A'

// Default cache size
#define DICTZIP_DEFAULT_CACHE_SIZE  5

// ============================================================
// Internal functions
// ============================================================

// Thread-local error message
static LSD_THREAD_LOCAL char error_message[1024];

/**
 * Set error message
 */
static void set_error(dsl_dictzip *dz, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(error_message, sizeof(error_message), fmt, args);
    va_end(args);
}

/**
 * Read a 16-bit little-endian integer
 */
static int read_le16(FILE *file) {
    int value = fgetc(file);
    value |= fgetc(file) << 8;
    return value;
}

/**
 * Read a 32-bit little-endian integer
 */
static uint32_t read_le32(FILE *file) {
    uint32_t value = (uint32_t)fgetc(file);
    value |= (uint32_t)fgetc(file) << 8;
    value |= (uint32_t)fgetc(file) << 16;
    value |= (uint32_t)fgetc(file) << 24;
    return value;
}

/**
 * Parse dictzip file header
 */
static int parse_dictzip_header(dsl_dictzip *dz) {
    FILE *file = dz->file;
    dictzip_header *header = &dz->header;

    // Read GZIP magic number
    int id1 = fgetc(file);
    int id2 = fgetc(file);

    if (id1 != DICTZIP_MAGIC1 || id2 != DICTZIP_MAGIC2) {
        set_error(dz, "Not a GZIP file");
        return -1;
    }

    header->method = fgetc(file);
    header->flags = fgetc(file);

    // Read mtime (4 bytes)
    header->mtime = read_le32(file);

    header->extra_flags = fgetc(file);
    header->os = fgetc(file);
    header->header_length = 10; // Base header length

    // Check extra field
    if (header->flags & DICTZIP_FEXTRA) {
        int extra_length = read_le16(file);
        header->header_length += extra_length + 2;

        int si1 = fgetc(file);
        int si2 = fgetc(file);
        extra_length -= 2;

        if (si1 == DICTZIP_RND_S1 && si2 == DICTZIP_RND_S2) {
            // dictzip format
            int sub_length = read_le16(file);
            extra_length -= 2;

            header->version = read_le16(file);
            if (header->version != 1) {
                set_error(dz, "Unsupported dictzip version: %d", header->version);
                return -1;
            }
            sub_length -= 2;

            header->chunk_length = read_le16(file);
            sub_length -= 2;

            header->chunk_count = read_le16(file);
            sub_length -= 2;

            if (header->chunk_count <= 0) {
                set_error(dz, "Invalid chunk count: %d", header->chunk_count);
                return -1;
            }

            // Allocate chunks array
            header->chunks = (int *)calloc(header->chunk_count, sizeof(int));
            if (!header->chunks) {
                set_error(dz, "Memory allocation failed for chunks");
                return -1;
            }

            // Read compressed size of each chunk
            for (int i = 0; i < header->chunk_count; i++) {
                header->chunks[i] = read_le16(file);
                sub_length -= 2;
            }

            // Skip remaining extra field bytes
            if (sub_length > 0) {
                lsd_fseek(file, sub_length, SEEK_CUR);
            }
        } else {
            set_error(dz, "Not a dictzip file (missing 'RA' signature)");
            return -1;
        }
    }

    // Skip original filename (if present)
    if (header->flags & DICTZIP_FNAME) {
        char buffer[256];
        int c;
        char *pt = buffer;
        while ((c = fgetc(file)) && c != EOF) {
            *pt++ = c;
        }
        *pt = '\0';
        strncpy(header->orig_filename, buffer, 255);
        header->header_length += strlen(buffer) + 1;
    } else {
        header->orig_filename[0] = '\0';
    }

    // Skip comment (if present)
    if (header->flags & DICTZIP_COMMENT) {
        char buffer[256];
        int c;
        char *pt = buffer;
        while ((c = fgetc(file)) && c != EOF) {
            *pt++ = c;
        }
        *pt = '\0';
        strncpy(header->comment, buffer, 255);
        header->header_length += strlen(buffer) + 1;
    } else {
        header->comment[0] = '\0';
    }

    // Skip CRC16 (if present)
    if (header->flags & DICTZIP_FHCRC) {
        fgetc(file);
        fgetc(file);
        header->header_length += 2;
    }

    // Read trailing CRC and length
    lsd_fseek(file, -8, SEEK_END);
    header->crc = read_le32(file);
    header->length = read_le32(file);

    header->compressed_length = lsd_ftell(file);

    // Compute offset of each chunk
    header->offsets = (uint64_t *)calloc(header->chunk_count,
                                              sizeof(uint64_t));
    if (!header->offsets) {
        set_error(dz, "Memory allocation failed for offsets");
        free(header->chunks);
        header->chunks = NULL;
        return -1;
    }

    uint64_t offset = header->header_length;
    for (int i = 0; i < header->chunk_count; i++) {
        header->offsets[i] = offset;
        offset += header->chunks[i];
    }

    return 0;
}

/**
 * Initialize zlib stream
 */
static int init_zstream(dsl_dictzip *dz) {
    if (dz->zstream_initialized) {
        return 0;
    }

    memset(&dz->zstream, 0, sizeof(z_stream));
    if (inflateInit2(&dz->zstream, -15) != Z_OK) {
        set_error(dz, "Failed to initialize zlib stream");
        return -1;
    }

    dz->zstream_initialized = true;
    return 0;
}

/**
 * Inflate (decompress) a single chunk
 */
static unsigned char *inflate_chunk(dsl_dictzip *dz, int chunk_index,
                                     size_t *out_size) {
    dictzip_header *header = &dz->header;

    if (chunk_index < 0 || chunk_index >= header->chunk_count) {
        set_error(dz, "Invalid chunk index: %d", chunk_index);
        return NULL;
    }

    // Seek to chunk start position
    if (lsd_fseek(dz->file, header->offsets[chunk_index], SEEK_SET) != 0) {
        set_error(dz, "Failed to seek to chunk %d: %s", chunk_index, strerror(errno));
        return NULL;
    }

    // Read compressed data
    int compressed_size = header->chunks[chunk_index];
    unsigned char *compressed_data = (unsigned char *)malloc(compressed_size);
    if (!compressed_data) {
        set_error(dz, "Memory allocation failed for compressed data");
        return NULL;
    }

    size_t read_size = fread(compressed_data, 1, compressed_size, dz->file);
    if (read_size != (size_t)compressed_size) {
        set_error(dz, "Failed to read compressed data");
        free(compressed_data);
        return NULL;
    }

    // Initialize zlib stream
    if (init_zstream(dz) != 0) {
        free(compressed_data);
        return NULL;
    }

    // Prepare output buffer (sized to chunk_length, the uncompressed size)
    unsigned char *out_buf = (unsigned char *)malloc(header->chunk_length);
    if (!out_buf) {
        set_error(dz, "Memory allocation failed for output buffer");
        free(compressed_data);
        return NULL;
    }

    // Set zlib parameters
    dz->zstream.next_in = compressed_data;
    dz->zstream.avail_in = compressed_size;
    dz->zstream.next_out = out_buf;
    dz->zstream.avail_out = header->chunk_length;

    // Inflate (use Z_PARTIAL_FLUSH since chunks are partial deflate blocks)
    int ret = inflate(&dz->zstream, Z_PARTIAL_FLUSH);
    free(compressed_data);

    if (ret != Z_OK && ret != Z_STREAM_END) {
        set_error(dz, "Failed to inflate chunk %d: %d", chunk_index, ret);
        inflateEnd(&dz->zstream);
        dz->zstream_initialized = false;
        free(out_buf);
        return NULL;
    }

    // Check for incomplete inflation
    if (dz->zstream.avail_in != 0) {
        set_error(dz, "Incomplete inflate: %d bytes remaining", dz->zstream.avail_in);
        inflateEnd(&dz->zstream);
        dz->zstream_initialized = false;
        free(out_buf);
        return NULL;
    }

    // Note: do not use inflateReset; keep inflate state continuous
    // dictzip chunks form a continuous deflate stream

    *out_size = header->chunk_length - dz->zstream.avail_out;
    return out_buf;
}

/**
 * Find a chunk in the cache, returning the cache index.
 * Also finds the LRU slot (smallest stamp).
 */
static int find_cache_entry(dsl_dictzip *dz, int chunk_index, int *lru_index) {
    int found_index = -1;
    int min_stamp = INT_MAX;

    for (int i = 0; i < dz->cache_size; i++) {
        // Check for cache hit
        if (dz->cache[i].chunk_index == chunk_index) {
            found_index = i;
        }

        // Find LRU entry (smallest stamp)
        if (dz->cache[i].stamp < min_stamp) {
            min_stamp = dz->cache[i].stamp;
            *lru_index = i;
        }
    }

    return found_index;
}

/**
 * Add a chunk to the cache using pre-allocated buffers and stamp-based LRU
 */
static dictzip_cache_entry *add_cache_entry(dsl_dictzip *dz, int cache_index,
                                               int chunk_index,
                                               unsigned char *data, size_t size) {
    dictzip_cache_entry *entry = &dz->cache[cache_index];

    // Allocate buffer if slot has none
    if (!entry->data) {
        entry->data = (unsigned char *)malloc(dz->header.chunk_length);
        if (!entry->data) {
            set_error(dz, "Memory allocation failed for cache buffer");
            return NULL;
        }
    } else if (entry->chunk_index != -1) {
        // Slot has existing data (evicted by LRU)
        // Note: no need to free entry->data since we reuse the buffer
    }

    // Copy decompressed data into cache buffer
    memcpy(entry->data, data, size);

    // Free the temporary decompression buffer
    free(data);

    // Update metadata
    entry->chunk_index = chunk_index;
    entry->size = size;
    entry->stamp = ++dz->stamp;

    // Prevent stamp overflow
    if (dz->stamp < 0) {
        dz->stamp = 0;
        for (int i = 0; i < dz->cache_size; i++) {
            dz->cache[i].stamp = -1;
        }
        // Re-update current entry stamp
        entry->stamp = ++dz->stamp;
    }

    return entry;
}

// ============================================================
// Public API implementation
// ============================================================

dsl_dictzip *dictzip_open(const char *filename) {
    if (!filename) {
        set_error(NULL, "filename is NULL");
        return NULL;
    }

    // Allocate dictzip structure
    dsl_dictzip *dz = (dsl_dictzip *)calloc(1, sizeof(dsl_dictzip));
    if (!dz) {
        set_error(NULL, "Memory allocation failed");
        return NULL;
    }

    // Save filename
    dz->filename = strdup(filename);
    if (!dz->filename) {
        set_error(NULL, "Memory allocation failed for filename");
        free(dz);
        return NULL;
    }

    // Open file
    dz->file = fopen(filename, "rb");
    if (!dz->file) {
        set_error(dz, "Failed to open file: %s", strerror(errno));
        free(dz->filename);
        free(dz);
        return NULL;
    }

    // Parse header
    if (parse_dictzip_header(dz) != 0) {
        fclose(dz->file);
        free(dz->filename);
        free(dz);
        return NULL;
    }

    // Initialize cache
    dz->cache_capacity = DICTZIP_DEFAULT_CACHE_SIZE;
    dz->cache_size = 0;
    dz->stamp = 0;

    dz->cache = (dictzip_cache_entry *)calloc(dz->cache_capacity,
                                                  sizeof(dictzip_cache_entry));
    if (!dz->cache) {
        set_error(dz, "Memory allocation failed for cache");
        free(dz->header.chunks);
        free(dz->header.offsets);
        fclose(dz->file);
        free(dz->filename);
        free(dz);
        return NULL;
    }

    // Initialize all cache slots
    for (int i = 0; i < dz->cache_capacity; i++) {
        dz->cache[i].chunk_index = -1;
        dz->cache[i].data = NULL;
        dz->cache[i].stamp = -1;
        dz->cache[i].size = 0;
    }

    dz->loaded = true;
    return dz;
}

void dictzip_close(dsl_dictzip *dz) {
    if (!dz) {
        return;
    }

    // Clear cache
    dictzip_clear_cache(dz);
    free(dz->cache);

    // Free header resources
    if (dz->header.chunks) {
        free(dz->header.chunks);
    }
    if (dz->header.offsets) {
        free(dz->header.offsets);
    }

    // Free zlib stream
    if (dz->zstream_initialized) {
        inflateEnd(&dz->zstream);
    }

    // Close file
    if (dz->file) {
        fclose(dz->file);
    }

    // Free filename
    if (dz->filename) {
        free(dz->filename);
    }

    free(dz);
}

unsigned char *dictzip_read(dsl_dictzip *dz, uint32_t offset, uint32_t size,
                             uint32_t *out_size) {
    if (!dz || !dz->loaded) {
        set_error(NULL, "Invalid dictzip handle");
        return NULL;
    }

    if (out_size) {
        *out_size = 0;
    }

    dictzip_header *header = &dz->header;

    // Check if offset exceeds file range
    if ((long long)offset >= header->length) {
        set_error(dz, "Offset beyond file size: %u >= %lld", offset, header->length);
        return NULL;
    }

    // Clamp size to remaining data
    uint32_t remaining = (uint32_t)(header->length - offset);
    if (size > remaining) {
        size = remaining;
    }

    // Allocate output buffer
    unsigned char *result = (unsigned char *)malloc(size);
    if (!result) {
        set_error(dz, "Memory allocation failed");
        return NULL;
    }

    uint32_t bytes_copied = 0;
    uint32_t current_offset = offset;

    while (bytes_copied < size) {
        // Compute current chunk index
        int chunk_index = current_offset / header->chunk_length;

        // Compute offset within the chunk
        int chunk_offset = current_offset % header->chunk_length;

        // Compute bytes to copy in this iteration
        int bytes_in_chunk = header->chunk_length - chunk_offset;
        int bytes_needed = size - bytes_copied;
        int bytes_to_copy = (bytes_in_chunk < bytes_needed) ? bytes_in_chunk : bytes_needed;

        // Look up cache; also find LRU slot
        int lru_index;
        int cache_index = find_cache_entry(dz, chunk_index, &lru_index);

        unsigned char *chunk_data = NULL;
        size_t chunk_size = 0;

        if (cache_index >= 0) {
            // Cache hit
            dictzip_cache_entry *cache_entry = &dz->cache[cache_index];
            chunk_data = cache_entry->data;
            chunk_size = cache_entry->size;

            // Update access timestamp
            cache_entry->stamp = ++dz->stamp;
            if (dz->stamp < 0) {
                dz->stamp = 0;
                for (int i = 0; i < dz->cache_size; i++) {
                    dz->cache[i].stamp = -1;
                }
                cache_entry->stamp = ++dz->stamp;
            }
        } else {
            // Cache miss — decompress chunk
            chunk_data = inflate_chunk(dz, chunk_index, &chunk_size);
            if (!chunk_data) {
                free(result);
                return NULL;
            }

            // Determine which cache slot to use
            int target_index;
            if (dz->cache_size < dz->cache_capacity) {
                // Cache not full — use a new slot
                target_index = dz->cache_size;
                dz->cache_size++;
            } else {
                // Cache full — evict LRU slot
                target_index = lru_index;
            }

            // Add to cache
            dictzip_cache_entry *cache_entry = add_cache_entry(dz, target_index,
                                                                 chunk_index,
                                                                 chunk_data, chunk_size);
            if (!cache_entry) {
                // Caching failed, but data is already decompressed
                memcpy(result + bytes_copied, chunk_data + chunk_offset, bytes_to_copy);
                bytes_copied += bytes_to_copy;
                current_offset += bytes_to_copy;
                free(chunk_data);
                continue;
            }

            chunk_data = cache_entry->data;
            chunk_size = cache_entry->size;
        }

        // Copy data
        memcpy(result + bytes_copied, chunk_data + chunk_offset, bytes_to_copy);
        bytes_copied += bytes_to_copy;
        current_offset += bytes_to_copy;
    }

    if (out_size) {
        *out_size = bytes_copied;
    }

    return result;
}

void dictzip_free(void *ptr) {
    free(ptr);
}

int dictzip_get_chunk_count(dsl_dictzip *dz) {
    if (!dz || !dz->loaded) {
        return -1;
    }
    return dz->header.chunk_count;
}

int dictzip_get_chunk_length(dsl_dictzip *dz) {
    if (!dz || !dz->loaded) {
        return 0;
    }
    return dz->header.chunk_length;
}

long long dictzip_get_uncompressed_size(dsl_dictzip *dz) {
    if (!dz || !dz->loaded) {
        return 0;
    }
    return dz->header.length;
}

long long dictzip_get_compressed_size(dsl_dictzip *dz) {
    if (!dz || !dz->loaded) {
        return 0;
    }
    return dz->header.compressed_length;
}

int dictzip_set_cache_size(dsl_dictzip *dz, int capacity) {
    if (!dz) {
        return -1;
    }

    if (capacity <= 0) {
        set_error(dz, "Invalid cache capacity: %d", capacity);
        return -1;
    }

    // Clear existing cache
    dictzip_clear_cache(dz);

    // Reallocate cache
    free(dz->cache);
    dz->cache = (dictzip_cache_entry *)calloc(capacity,
                                                  sizeof(dictzip_cache_entry));
    if (!dz->cache) {
        set_error(dz, "Memory allocation failed for cache");
        return -1;
    }

    dz->cache_capacity = capacity;
    dz->cache_size = 0;
    dz->stamp = 0;

    // Initialize all cache slots
    for (int i = 0; i < dz->cache_capacity; i++) {
        dz->cache[i].chunk_index = -1;
        dz->cache[i].data = NULL;
        dz->cache[i].stamp = -1;
        dz->cache[i].size = 0;
    }

    return 0;
}

void dictzip_clear_cache(dsl_dictzip *dz) {
    if (!dz) {
        return;
    }

    for (int i = 0; i < dz->cache_size; i++) {
        if (dz->cache[i].data) {
            free(dz->cache[i].data);
            dz->cache[i].data = NULL;
        }
    }

    dz->cache_size = 0;
}

const char *dictzip_get_error(dsl_dictzip *dz) {
    (void)dz;  // Unused parameter
    return error_message;
}
