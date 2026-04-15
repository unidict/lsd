//
//  dsl_reader.h
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#ifndef dsl_h
#define dsl_h

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

// Forward declaration (avoids exposing dsl_dictzip internal implementation and zlib dependency)
typedef struct dsl_dictzip dsl_dictzip;

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// DSL file encoding
// ============================================================
typedef enum {
    DSL_ENCODING_UNKNOWN,
    DSL_ENCODING_UTF8,
    DSL_ENCODING_UTF16LE,
    DSL_ENCODING_UTF16BE,
} dsl_encoding;

// ============================================================
// DSL file header information
// ============================================================
typedef struct dsl_header {
    char *name;              // Dictionary name
    char *index_language;    // Source language
    char *contents_language; // Target language
    char *metadata;          // Metadata (extracted from {{ ... }})
    dsl_encoding encoding;   // File encoding
} dsl_header;

// ============================================================
// DSL article
// ============================================================
typedef struct dsl_article {
    char *heading;           // Article heading
    size_t heading_length;   // Heading length
    char *definition;        // Article definition
    size_t definition_length;// Definition length
    size_t definition_offset;// Definition start position in the file
} dsl_article;

// ============================================================
// DSL reader
// ============================================================
typedef struct dsl_reader {
    FILE *file;              // File handle (for uncompressed files)
    dsl_dictzip *dz;         // dictzip handle (for DSL.dz files)
    const char *filename;    // File name

    dsl_header header;       // File header information

    size_t file_size;        // File size
    size_t data_offset;      // Data area start position
    size_t current_offset;   // Current read position (after decompression)
    bool is_dz;              // Whether the file is compressed

    // Read buffer (for dictzip)
    char *read_buffer;       // Read buffer
    size_t buffer_size;      // Buffer size
    size_t buffer_pos;       // Current buffer position
    size_t buffer_valid;     // Valid data length in buffer
} dsl_reader;

// ============================================================
// Creation and destruction
// ============================================================

/**
 * Open a DSL file
 * @param filename File path
 * @return DSL reader, or NULL on failure
 */
dsl_reader *dsl_reader_open(const char *filename);

/**
 * Close the DSL reader
 * @param reader DSL reader
 */
void dsl_reader_close(dsl_reader *reader);

// ============================================================
// Property access
// ============================================================

/**
 * Get file header information
 * @param reader DSL reader
 * @return Pointer to file header information
 */
const dsl_header *dsl_reader_get_header(const dsl_reader *reader);

/**
 * Get dictionary name
 * @param reader DSL reader
 * @param name Output name (UTF-8, caller must free)
 * @return 0 on success
 */
int dsl_reader_get_name(const dsl_reader *reader, char **name);

/**
 * Get source language code
 * @param reader DSL reader
 * @return Source language code string (do not free)
 */
const char *dsl_reader_get_source_language(const dsl_reader *reader);

/**
 * Get target language code
 * @param reader DSL reader
 * @return Target language code string (do not free)
 */
const char *dsl_reader_get_target_language(const dsl_reader *reader);

/**
 * Get metadata
 * @param reader DSL reader
 * @return Metadata string (do not free), extracted from double braces {{ ... }}
 */
const char *dsl_reader_get_metadata(const dsl_reader *reader);

/**
 * Get file encoding
 * @param reader DSL reader
 * @return File encoding type
 */
dsl_encoding dsl_reader_get_encoding(const dsl_reader *reader);

// ============================================================
// Article reading
// ============================================================

/**
 * Read the next article
 * @param reader DSL reader
 * @param article Article structure (caller must free)
 * @return true on success, false when end of file is reached
 */
bool dsl_reader_next_article(dsl_reader *reader, dsl_article *article);

/**
 * Free article resources
 * @param article Article structure
 */
void dsl_article_free(dsl_article *article);

// ============================================================
// Utility functions
// ============================================================

/**
 * Get encoding name
 * @param encoding Encoding type
 * @return Encoding name string
 */
const char *dsl_encoding_name(dsl_encoding encoding);

/**
 * Detect file encoding from BOM
 * @param reader DSL reader
 * @return Detected encoding type
 */
dsl_encoding dsl_detect_bom(dsl_reader *reader);

#ifdef __cplusplus
}
#endif

#endif /* dsl_h */
