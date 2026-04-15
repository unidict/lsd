//
//  dsl_reader.c
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#include "dsl_reader.h"
#include "dsl_dictzip.h"
#include "lsd_platform.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

// ============================================================
// Constants
// ============================================================
#define READ_BUFFER_SIZE (64 * 1024)  // 64KB read buffer
#define LINE_BUFFER_SIZE 4096         // Line buffer size

// ============================================================
// Internal helper functions
// ============================================================

/**
 * Convert Unicode codepoint to UTF-8
 * Return UTF-8 byte count, 0 for invalid codepoint
 */
static int unicode_to_utf8(uint32_t codepoint, char *utf8_buf) {
    if (codepoint <= 0x7F) {
        // 1 byte
        utf8_buf[0] = (char)codepoint;
        return 1;
    } else if (codepoint <= 0x7FF) {
        // 2 bytes
        utf8_buf[0] = 0xC0 | (char)((codepoint >> 6) & 0x1F);
        utf8_buf[1] = 0x80 | (char)(codepoint & 0x3F);
        return 2;
    } else if (codepoint <= 0xFFFF) {
        // 3 bytes
        utf8_buf[0] = 0xE0 | (char)((codepoint >> 12) & 0x0F);
        utf8_buf[1] = 0x80 | (char)((codepoint >> 6) & 0x3F);
        utf8_buf[2] = 0x80 | (char)(codepoint & 0x3F);
        return 3;
    } else if (codepoint <= 0x10FFFF) {
        // 4 bytes
        utf8_buf[0] = 0xF0 | (char)((codepoint >> 18) & 0x07);
        utf8_buf[1] = 0x80 | (char)((codepoint >> 12) & 0x3F);
        utf8_buf[2] = 0x80 | (char)((codepoint >> 6) & 0x3F);
        utf8_buf[3] = 0x80 | (char)(codepoint & 0x3F);
        return 4;
    }
    return 0;  // Invalid codepoint
}

/**
 * Read a character from buffer or file
 * Return a single byte (for UTF-8)
 */
static int dsl_getc(dsl_reader *reader) {
    if (!reader) return EOF;

    if (reader->is_dz) {
        // Read from dictzip
        if (reader->buffer_pos >= reader->buffer_valid) {
            // Need to refill buffer
            uint32_t bytes_read;
            size_t read_size = READ_BUFFER_SIZE;
            unsigned char *data = dictzip_read(reader->dz,
                                              (uint32_t)reader->current_offset,
                                              (uint32_t)read_size,
                                              &bytes_read);

            if (!data || bytes_read == 0) {
                return EOF;
            }

            // If buffer is not large enough, reallocate
            if (reader->buffer_size < bytes_read) {
                reader->read_buffer = realloc(reader->read_buffer, bytes_read);
                reader->buffer_size = bytes_read;
            }

            memcpy(reader->read_buffer, data, bytes_read);
            dictzip_free(data);

            reader->buffer_pos = 0;
            reader->buffer_valid = bytes_read;
            reader->current_offset += bytes_read;
        }

        return (unsigned char)reader->read_buffer[reader->buffer_pos++];
    } else {
        // Read from plain file
        if (reader->file) {
            return fgetc(reader->file);
        }
        return EOF;
    }
}

/**
 * Read a Unicode codepoint from UTF-16 file
 * Return Unicode codepoint, EOF for end of file
 */
static int dsl_get_utf16_char(dsl_reader *reader) {
    if (!reader) return EOF;

    int byte1, byte2;

    if (reader->is_dz) {
        // Read from dictzip
        if (reader->buffer_pos + 1 >= reader->buffer_valid) {
            // Need to refill buffer
            uint32_t bytes_read;
            size_t read_size = READ_BUFFER_SIZE;
            unsigned char *data = dictzip_read(reader->dz,
                                              (uint32_t)reader->current_offset,
                                              (uint32_t)read_size,
                                              &bytes_read);

            if (!data || bytes_read == 0) {
                return EOF;
            }

            // If buffer is not large enough, reallocate
            if (reader->buffer_size < bytes_read) {
                reader->read_buffer = realloc(reader->read_buffer, bytes_read);
                reader->buffer_size = bytes_read;
            }

            memcpy(reader->read_buffer, data, bytes_read);
            dictzip_free(data);

            reader->buffer_pos = 0;
            reader->buffer_valid = bytes_read;
            reader->current_offset += bytes_read;
        }

        if (reader->buffer_pos + 1 >= reader->buffer_valid) {
            return EOF;
        }

        unsigned char *buf = (unsigned char *)reader->read_buffer;
        if (reader->header.encoding == DSL_ENCODING_UTF16LE) {
            byte1 = buf[reader->buffer_pos++];
            byte2 = buf[reader->buffer_pos++];
        } else {  // UTF16BE
            byte2 = buf[reader->buffer_pos++];
            byte1 = buf[reader->buffer_pos++];
        }
    } else {
        // Read from plain file
        if (!reader->file) return EOF;

        byte1 = fgetc(reader->file);
        if (byte1 == EOF) return EOF;

        byte2 = fgetc(reader->file);
        if (byte2 == EOF) return EOF;

        if (reader->header.encoding == DSL_ENCODING_UTF16BE) {
            int temp = byte1;
            byte1 = byte2;
            byte2 = temp;
        }
    }

    uint16_t utf16_char = (uint16_t)((byte2 << 8) | byte1);

    // Check for surrogate pair
    if (utf16_char >= 0xD800 && utf16_char <= 0xDBFF) {
        // High surrogate, need to read low surrogate
        int byte3, byte4;

        if (reader->is_dz) {
            if (reader->buffer_pos + 1 >= reader->buffer_valid) {
                return EOF;  // Incomplete data
            }

            unsigned char *buf = (unsigned char *)reader->read_buffer;
            if (reader->header.encoding == DSL_ENCODING_UTF16LE) {
                byte3 = buf[reader->buffer_pos++];
                byte4 = buf[reader->buffer_pos++];
            } else {
                byte4 = buf[reader->buffer_pos++];
                byte3 = buf[reader->buffer_pos++];
            }
        } else {
            byte3 = fgetc(reader->file);
            if (byte3 == EOF) return EOF;
            byte4 = fgetc(reader->file);
            if (byte4 == EOF) return EOF;

            if (reader->header.encoding == DSL_ENCODING_UTF16BE) {
                int temp = byte3;
                byte3 = byte4;
                byte4 = temp;
            }
        }

        uint16_t utf16_char2 = (uint16_t)((byte4 << 8) | byte3);

        // Calculate full Unicode codepoint
        uint32_t high_surrogate = utf16_char - 0xD800;
        uint32_t low_surrogate = utf16_char2 - 0xDC00;
        return (int)((high_surrogate << 10) + low_surrogate + 0x10000);
    }

    return (int)utf16_char;
}

/**
 * Read a line from buffer or file
 * Return value must be freed by caller
 * If file is UTF-16 encoded, auto-convert to UTF-8
 */
static char *dsl_read_line(dsl_reader *reader) {
    if (!reader) return NULL;

    char buffer[LINE_BUFFER_SIZE];
    size_t offset = 0;
    int ch = 0;

    // Check if UTF-16 encoded
    bool is_utf16 = (reader->header.encoding == DSL_ENCODING_UTF16LE ||
                     reader->header.encoding == DSL_ENCODING_UTF16BE);

    if (is_utf16) {
        // UTF-16 encoding, read and convert to UTF-8
        while (offset < sizeof(buffer) - 4) {  // Reserve space for UTF-8 chars
            ch = dsl_get_utf16_char(reader);
            if (ch == EOF) break;

            // Check for newline (\n in UTF-16 is 0x000A)
            if (ch == '\n') break;
            if (ch == '\r') continue;  // Skip \r

            // Convert Unicode codepoint to UTF-8
            char utf8_buf[4];
            int utf8_len = unicode_to_utf8((uint32_t)ch, utf8_buf);
            if (utf8_len > 0 && offset + utf8_len < sizeof(buffer)) {
                memcpy(buffer + offset, utf8_buf, utf8_len);
                offset += utf8_len;
            }
        }

        if (offset == 0 && ch == EOF) {
            return NULL;
        }

        buffer[offset] = '\0';
        return strdup(buffer);
    } else {
        // UTF-8 encoding
        while (offset < sizeof(buffer) - 1) {
            ch = dsl_getc(reader);
            if (ch == EOF || ch == '\n') break;
            if (ch != '\r') {
                buffer[offset++] = (char)ch;
            }
        }

        if (offset == 0 && ch == EOF) {
            return NULL;
        }

        buffer[offset] = '\0';
        return strdup(buffer);
    }
}

/**
 * Push back a line (used when next heading is detected)
 * Note: This is a simple implementation; dictzip files may not fully support seeking back
 */
static void dsl_unread_line(dsl_reader *reader, const char *line) {
    if (!reader || !line) return;

    if (!reader->is_dz && reader->file) {
        // For plain files, simply use fseek
        size_t len = strlen(line);
        lsd_fseek(reader->file, -(long)(len + 1), SEEK_CUR);
    }
    // For dictzip, we need more complex logic
    // Skip for now, assume sequential reading
}

/**
 * Detect file encoding (via BOM)
 */
static dsl_encoding detect_encoding_from_bom(FILE *file) {
    unsigned char bom[4];
    size_t n = fread(bom, 1, 4, file);
    rewind(file);

    if (n >= 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
        return DSL_ENCODING_UTF8;
    }

    if (n >= 2) {
        if (bom[0] == 0xFF && bom[1] == 0xFE) {
            return DSL_ENCODING_UTF16LE;
        }
        if (bom[0] == 0xFE && bom[1] == 0xFF) {
            return DSL_ENCODING_UTF16BE;
        }
    }

    // If no BOM, try to detect
    if (n >= 2) {
        // UTF-16LE is typically 0, non-0
        if (bom[0] != 0 && bom[1] == 0) {
            return DSL_ENCODING_UTF16LE;
        }
        // UTF-16BE is typically non-0, 0
        if (bom[0] == 0 && bom[1] != 0) {
            return DSL_ENCODING_UTF16BE;
        }
    }

    // Default to UTF-8
    return DSL_ENCODING_UTF8;
}

/**
 * Check if file is gzip compressed
 */
static bool is_gzip_file(FILE *file) {
    unsigned char magic[2];
    size_t n = fread(magic, 1, 2, file);
    rewind(file);

    return (n >= 2 && magic[0] == 0x1F && magic[1] == 0x8B);
}

/**
 * Read a line (encoding-aware)
 */
static char *read_line_encoded(FILE *file, dsl_encoding encoding, size_t *line_length) {
    char buffer[4096];
    size_t offset = 0;
    int ch;

    if (encoding == DSL_ENCODING_UTF8) {
        // UTF-8 encoding
        while (offset < sizeof(buffer) - 1) {
            ch = fgetc(file);
            if (ch == EOF || ch == '\n') break;
            buffer[offset++] = (char)ch;
        }
        buffer[offset] = '\0';
    } else {
        // UTF-16 encoding (simplified for now)
        while (offset < sizeof(buffer) - 2) {
            ch = fgetc(file);
            if (ch == EOF) break;
            buffer[offset++] = (char)ch;

            // UTF-16LE/BE read
            if (offset >= 2) {
                unsigned char* ptr = (unsigned char*)buffer;
                // Simple newline detection
                if (encoding == DSL_ENCODING_UTF16LE) {
                    if (ptr[offset-2] == 0x0A && ptr[offset-1] == 0x00) break;
                } else {
                    if (ptr[offset-2] == 0x00 && ptr[offset-1] == 0x0A) break;
                }
            }
        }
        buffer[offset] = '\0';
    }

    if (line_length) {
        *line_length = offset;
    }

    if (offset > 0) {
        return strdup(buffer);
    }
    return NULL;
}

/**
 * Clean heading string (remove { } markers and escape chars)
 */
static char *clean_heading(const char *src) {
    size_t len = strlen(src);
    char *str = malloc(len + 1);
    int idx = 0;
    int opened = 0;

    for (size_t i = 0; i < len; i++) {
        if (src[i] == '{') {
            opened = 1;
        } else if (src[i] == '}') {
            opened = 0;
        } else if (!opened && src[i] != '\n' && src[i] != '\r') {
            // Handle escape characters
            if (i > 0 && src[i-1] == '\\' && src[i-2] != '\\') {
                str[idx-1] = src[i];
            } else if (i == 1 && src[0] == '\\') {
                str[idx-1] = src[i];
            } else {
                str[idx++] = src[i];
            }
        }
    }
    str[idx] = '\0';
    return str;
}

// ============================================================
// Create and destroy
// ============================================================

dsl_reader *dsl_reader_open(const char *filename) {
    if (!filename) return NULL;

    // Check if it's a .dz file
    size_t len = strlen(filename);
    bool is_dz = (len >= 3 && strcmp(filename + len - 3, ".dz") == 0);

    dsl_reader *reader = calloc(1, sizeof(dsl_reader));
    if (!reader) {
        return NULL;
    }

    reader->filename = filename;
    reader->is_dz = is_dz;
    reader->current_offset = 0;
    reader->buffer_pos = 0;
    reader->buffer_valid = 0;
    reader->buffer_size = 0;
    reader->read_buffer = NULL;

    if (is_dz) {
        // Open with dictzip
        reader->dz = dictzip_open(filename);
        if (!reader->dz) {
            fprintf(stderr, "Failed to open dictzip file: %s\n", filename);
            fprintf(stderr, "Error: %s\n", dictzip_get_error(NULL));
            free(reader);
            return NULL;
        }

        reader->file_size = dictzip_get_uncompressed_size(reader->dz);
        reader->file = NULL;
    } else {
        // Open as plain file
        reader->file = fopen(filename, "rb");
        if (!reader->file) {
            fprintf(stderr, "Failed to open file: %s\n", filename);
            free(reader);
            return NULL;
        }

        // Get file size
        lsd_fseek(reader->file, 0, SEEK_END);
        reader->file_size = lsd_ftell(reader->file);
        rewind(reader->file);

        reader->dz = NULL;
    }

    // Detect file encoding
    size_t bom_size = 0;  // BOM size
    if (reader->file) {
        reader->header.encoding = detect_encoding_from_bom(reader->file);
        // Skip BOM
        switch (reader->header.encoding) {
            case DSL_ENCODING_UTF8:
                bom_size = 3;
                break;
            case DSL_ENCODING_UTF16LE:
            case DSL_ENCODING_UTF16BE:
                bom_size = 2;
                break;
            default:
                bom_size = 0;
                break;
        }
        if (bom_size > 0) {
            lsd_fseek(reader->file, bom_size, SEEK_SET);
        }
    } else {
        // For dictzip files, read first few bytes to detect BOM
        unsigned char bom[4];
        uint32_t bytes_read;
        unsigned char *data = dictzip_read(reader->dz, 0, 4, &bytes_read);
        if (data && bytes_read >= 3) {
            memcpy(bom, data, bytes_read);
            dictzip_free(data);

            if (bytes_read >= 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
                reader->header.encoding = DSL_ENCODING_UTF8;
                bom_size = 3;
            } else if (bytes_read >= 2 && bom[0] == 0xFF && bom[1] == 0xFE) {
                reader->header.encoding = DSL_ENCODING_UTF16LE;
                bom_size = 2;
            } else if (bytes_read >= 2 && bom[0] == 0xFE && bom[1] == 0xFF) {
                reader->header.encoding = DSL_ENCODING_UTF16BE;
                bom_size = 2;
            } else {
                reader->header.encoding = DSL_ENCODING_UTF8;
                bom_size = 0;
            }
        } else {
            reader->header.encoding = DSL_ENCODING_UTF8;
            bom_size = 0;
        }

        // Skip BOM, set read position
        reader->current_offset = bom_size;
    }

    // Initialize header info
    reader->header.name = NULL;
    reader->header.index_language = NULL;
    reader->header.contents_language = NULL;
    reader->header.metadata = NULL;

    // Read file header (comment lines and double-brace metadata)
    char *line;

    while ((line = dsl_read_line(reader)) != NULL) {
        size_t line_len = strlen(line);

        // Skip empty lines
        if (line_len == 0) {
            free(line);
            continue;
        }

        // Check for double-brace metadata {{ ... }}
        if (line_len >= 4 && line[0] == '{' && line[1] == '{') {
            // Find closing double-brace
            char *end = strstr(line, "}}");
            if (end) {
                // Extract content inside double-braces
                size_t content_len = end - (line + 2);
                char *content = strndup(line + 2, content_len);

                // Strip leading/trailing spaces
                char *start = content;
                char *finish = content + content_len - 1;
                while (start <= finish && (*start == ' ' || *start == '\t')) start++;
                while (finish >= start && (*finish == ' ' || *finish == '\t')) finish--;
                *(finish + 1) = '\0';

                // Save to metadata field (usually first double-brace is metadata)
                if (!reader->header.metadata && strlen(start) > 0) {
                    reader->header.metadata = strdup(start);
                }

                free(content);
            }
            free(line);
            continue;
        }

        // Check if comment line
        if (line[0] != '#') {
            // Not a comment line, save for later processing
            // For sequential reading, simply buffer this line
            // Simplified: dictzip files do not support seeking back
            free(line);
            break;
        }

        // Parse comment line
        char *key = strtok(line + 1, " \t\"");
        char *value = strtok(NULL, "\"");

        if (key && value) {
            if (strcmp(key, "NAME") == 0) {
                reader->header.name = strdup(value);
            } else if (strcmp(key, "INDEX_LANGUAGE") == 0) {
                reader->header.index_language = strdup(value);
            } else if (strcmp(key, "CONTENTS_LANGUAGE") == 0) {
                reader->header.contents_language = strdup(value);
            }
        }

        free(line);
    }

    // Record data area start position
    reader->data_offset = reader->current_offset;

    if (!reader->is_dz && reader->file) {
        reader->data_offset = lsd_ftell(reader->file);
    }

    return reader;
}

void dsl_reader_close(dsl_reader *reader) {
    if (!reader) return;

    if (reader->file) {
        fclose(reader->file);
    }

    if (reader->dz) {
        dictzip_close(reader->dz);
    }

    if (reader->read_buffer) {
        free(reader->read_buffer);
    }

    if (reader->header.name) free(reader->header.name);
    if (reader->header.index_language) free(reader->header.index_language);
    if (reader->header.contents_language) free(reader->header.contents_language);
    if (reader->header.metadata) free(reader->header.metadata);

    free(reader);
}

// ============================================================
// Property access
// ============================================================

const dsl_header *dsl_reader_get_header(const dsl_reader *reader) {
    return reader ? &reader->header : NULL;
}

int dsl_reader_get_name(const dsl_reader *reader, char **name) {
    if (!reader || !name) return -1;

    if (reader->header.name) {
        *name = strdup(reader->header.name);
        return 0;
    }
    return -1;
}

const char *dsl_reader_get_source_language(const dsl_reader *reader) {
    return reader ? reader->header.index_language : NULL;
}

const char *dsl_reader_get_target_language(const dsl_reader *reader) {
    return reader ? reader->header.contents_language : NULL;
}

const char *dsl_reader_get_metadata(const dsl_reader *reader) {
    return reader ? reader->header.metadata : NULL;
}

dsl_encoding dsl_reader_get_encoding(const dsl_reader *reader) {
    return reader ? reader->header.encoding : DSL_ENCODING_UNKNOWN;
}

// ============================================================
// Heading reading
// ============================================================

bool dsl_reader_next_article(dsl_reader *reader, dsl_article *article) {
    if (!reader || !article) return false;

    char heading[4096] = {0};
    char definition[8192] = {0};
    size_t heading_len = 0;
    size_t definition_len = 0;
    size_t article_offset = 0;

    // Read heading line (non-indented line)
    char *line;
    while ((line = dsl_read_line(reader)) != NULL) {
        size_t len = strlen(line);

        // Skip empty lines and comments
        if (len == 0 || line[0] == '#') {
            free(line);
            continue;
        }

        // Check if heading line (line does not start with space or tab)
        if (line[0] != ' ' && line[0] != '\t') {
            // Clean heading
            char *cleaned = clean_heading(line);
            strncpy(heading, cleaned, sizeof(heading) - 1);
            heading_len = strlen(heading);
            free(cleaned);

            // Record offset
            if (!reader->is_dz && reader->file) {
                article_offset = lsd_ftell(reader->file) - len;
            } else {
                article_offset = reader->current_offset;
            }

            free(line);
            break;
        }

        free(line);
    }

    if (heading_len == 0) {
        // No heading found, reached end of file
        return false;
    }

    // Read definition (indented lines)
    while ((line = dsl_read_line(reader)) != NULL) {
        size_t len = strlen(line);

        // Check if next heading (non-indented line)
        if (len > 0 && line[0] != ' ' && line[0] != '\t') {
            // For dictzip, simply ignore seeking back, since it's sequential reading
            // For plain files, can seek back
            if (!reader->is_dz && reader->file) {
                dsl_unread_line(reader, line);
            }
            free(line);
            break;
        }

        // Append to definition
        if (definition_len + len < sizeof(definition) - 1) {
            strcpy(definition + definition_len, line);
            definition_len += len;
            strcpy(definition + definition_len, "\n");
            definition_len++;
        }

        free(line);
    }

    // Fill article structure
    article->heading = strdup(heading);
    article->heading_length = heading_len;
    article->definition = strdup(definition);
    article->definition_length = definition_len;
    article->definition_offset = article_offset;

    return true;
}

void dsl_article_free(dsl_article *article) {
    if (!article) return;

    if (article->heading) free(article->heading);
    if (article->definition) free(article->definition);

    article->heading = NULL;
    article->definition = NULL;
    article->heading_length = 0;
    article->definition_length = 0;
    article->definition_offset = 0;
}

// ============================================================
// Utility functions
// ============================================================

const char *dsl_encoding_name(dsl_encoding encoding) {
    switch (encoding) {
        case DSL_ENCODING_UTF8:
            return "UTF-8";
        case DSL_ENCODING_UTF16LE:
            return "UTF-16LE";
        case DSL_ENCODING_UTF16BE:
            return "UTF-16BE";
        default:
            return "UNKNOWN";
    }
}

dsl_encoding dsl_detect_bom(dsl_reader *reader) {
    if (!reader) return DSL_ENCODING_UNKNOWN;

    // Re-detect encoding
    rewind(reader->file);
    dsl_encoding encoding = detect_encoding_from_bom(reader->file);
    rewind(reader->file);

    return encoding;
}
