//
//  dsl_dictzip.h
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#ifndef dictzip_h
#define dictzip_h

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <zlib.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Data structures
// ============================================================

/**
 * dictzip file information
 */
typedef struct {
    int header_length;       // Total header length
    int method;              // Compression method (8 = deflate)
    int flags;               // GZIP flags
    uint32_t mtime;          // Modification time
    int extra_flags;         // Extra flags
    int os;                  // Operating system
    int version;             // dictzip version
    int chunk_length;        // Pre-compression size of each chunk
    int chunk_count;         // Total number of chunks
    int *chunks;             // Array of compressed chunk sizes
    uint64_t *offsets;       // File offset of each chunk
    char orig_filename[256]; // Original filename
    char comment[256];       // Comment
    uint32_t crc;            // CRC32 checksum
    long long length;        // Uncompressed file size
    long long compressed_length; // Compressed file size
} dictzip_header;

/**
 * Chunk cache entry
 */
typedef struct {
    int chunk_index;         // Chunk index, -1 for unused
    unsigned char *data;     // Decompressed data
    size_t size;             // Actual data size
    int stamp;               // Timestamp (for LRU eviction)
} dictzip_cache_entry;

/**
 * dictzip file handle
 */
typedef struct dsl_dictzip {
    // File information
    char *filename;          // Filename
    FILE *file;              // File handle

    // Header information
    dictzip_header header;   // dictzip header

    // Cache
    dictzip_cache_entry *cache; // Cache array
    int cache_size;          // Cache size
    int cache_capacity;      // Cache capacity
    int stamp;               // Global timestamp counter

    // zlib inflate stream
    z_stream zstream;
    bool zstream_initialized;

    // Internal state
    bool loaded;
} dsl_dictzip;

// ============================================================
// API functions
// ============================================================

/**
 * Open a dictzip file
 * @param filename File path
 * @return dictzip handle on success, NULL on failure
 */
dsl_dictzip *dictzip_open(const char *filename);

/**
 * Close a dictzip file
 * @param dz dictzip handle
 */
void dictzip_close(dsl_dictzip *dz);

/**
 * Read and decompress data
 * @param dz dictzip handle
 * @param offset Data offset (relative to decompressed data)
 * @param size Data size
 * @param out_size Output: actual bytes read
 * @return Pointer to decompressed data (caller must free with dictzip_free), NULL on failure
 */
unsigned char *dictzip_read(dsl_dictzip *dz, uint32_t offset, uint32_t size,
                            uint32_t *out_size);

/**
 * Free memory returned by dictzip_read
 * @param ptr Data pointer
 */
void dictzip_free(void *ptr);

/**
 * Get total chunk count
 * @param dz dictzip handle
 * @return Total chunk count, -1 on failure
 */
int dictzip_get_chunk_count(dsl_dictzip *dz);

/**
 * Get chunk length (pre-compression size)
 * @param dz dictzip handle
 * @return Chunk length, 0 on failure
 */
int dictzip_get_chunk_length(dsl_dictzip *dz);

/**
 * Get uncompressed file size
 * @param dz dictzip handle
 * @return Uncompressed file size
 */
long long dictzip_get_uncompressed_size(dsl_dictzip *dz);

/**
 * Get compressed file size
 * @param dz dictzip handle
 * @return Compressed file size
 */
long long dictzip_get_compressed_size(dsl_dictzip *dz);

/**
 * Set cache capacity
 * @param dz dictzip handle
 * @param capacity Cache capacity (number of chunks)
 * @return 0 on success, -1 on failure
 */
int dictzip_set_cache_size(dsl_dictzip *dz, int capacity);

/**
 * Clear the cache
 * @param dz dictzip handle
 */
void dictzip_clear_cache(dsl_dictzip *dz);

/**
 * Get the last error message
 * @param dz dictzip handle
 * @return Error message string
 */
const char *dictzip_get_error(dsl_dictzip *dz);

#ifdef __cplusplus
}
#endif

#endif /* dictzip_h */
