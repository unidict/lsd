//
//  lsd_decoder.c
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#include "lsd_decoder.h"
#include "lsd_bitstream.h"
#include "lsd_huffman.h"
#include <stdlib.h>
#include <string.h>

// ============================================================
// Internal type definitions
// ============================================================
typedef enum {
    LSD_DECODER_TYPE_USER,         // User dictionary
    LSD_DECODER_TYPE_SYSTEM,       // System dictionary
    LSD_DECODER_TYPE_ABBREVIATION, // Abbreviation dictionary
    LSD_DECODER_TYPE_UNKNOWN       // Unknown type
} lsd_decoder_type;

struct lsd_decoder {
    lsd_decoder_type type;         // Decoder type
    bool use_xoring;                     // Whether to use XOR encryption
    bool legacy_system;                  // Whether it is a legacy system dictionary

    // Symbol tables
    uint32_t *article_symbols;           // Article symbol table
    size_t article_symbol_count;

    uint32_t *heading_symbols;           // Heading symbol table
    size_t heading_symbol_count;

    // Huffman trees
    lsd_huffman_tree *lt_articles;        // Article Huffman tree
    lsd_huffman_tree *lt_headings;        // Heading Huffman tree
    lsd_huffman_tree *lt_prefix_lengths;  // Prefix length Huffman tree
    lsd_huffman_tree *lt_postfix_lengths; // Postfix length Huffman tree

    // Reference encoding parameters
    uint32_t huffman1_number;
    uint32_t huffman2_number;

    // Prefix string
    uint16_t *prefix;                    // UTF-16 prefix
    size_t prefix_length;
};

// ============================================================
// Version constants
// ============================================================

#define LSD_VERSION_USER_11       0x110001
#define LSD_VERSION_USER_12       0x120001
#define LSD_VERSION_USER_LEGACY   0x131001
#define LSD_VERSION_USER_13       0x132001
#define LSD_VERSION_USER_14       0x142001
#define LSD_VERSION_USER_15       0x152001

#define LSD_VERSION_SYSTEM_14     0x141004
#define LSD_VERSION_ABBR_14       0x145001
#define LSD_VERSION_SYSTEM_15     0x151005
#define LSD_VERSION_ABBR_15       0x155001

// ============================================================
// Version helper functions
// ============================================================

bool lsd_is_version_supported(uint32_t version) {
    switch (version) {
        case LSD_VERSION_USER_13:
        case LSD_VERSION_USER_14:
        case LSD_VERSION_USER_15:
        case LSD_VERSION_SYSTEM_14:
        case LSD_VERSION_USER_LEGACY:
        case LSD_VERSION_ABBR_14:
        case LSD_VERSION_ABBR_15:
        case LSD_VERSION_SYSTEM_15:
        case LSD_VERSION_USER_12:
        case LSD_VERSION_USER_11:
            return true;
        default:
            return false;
    }
}

// ============================================================
// Create and destroy
// ============================================================

lsd_decoder *lsd_decoder_create(uint32_t version) {
    lsd_decoder *decoder = calloc(1, sizeof(lsd_decoder));
    if (!decoder) return NULL;
    
    // Determine decoder type based on version
    switch (version) {
        case LSD_VERSION_USER_11:
        case LSD_VERSION_USER_12:
        case LSD_VERSION_USER_13:
        case LSD_VERSION_USER_14:
        case LSD_VERSION_USER_15:
            decoder->type = LSD_DECODER_TYPE_USER;
            decoder->use_xoring = false;
            decoder->legacy_system = false;
            break;
            
        case LSD_VERSION_USER_LEGACY:
            decoder->type = LSD_DECODER_TYPE_USER;
            decoder->use_xoring = false;
            decoder->legacy_system = true;
            break;
            
        case LSD_VERSION_SYSTEM_14:
            decoder->type = LSD_DECODER_TYPE_SYSTEM;
            decoder->use_xoring = false;
            decoder->legacy_system = false;
            break;
            
        case LSD_VERSION_SYSTEM_15:
            decoder->type = LSD_DECODER_TYPE_SYSTEM;
            decoder->use_xoring = true;
            decoder->legacy_system = false;
            break;
            
        case LSD_VERSION_ABBR_14:
        case LSD_VERSION_ABBR_15:
            decoder->type = LSD_DECODER_TYPE_ABBREVIATION;
            decoder->use_xoring = false;
            decoder->legacy_system = false;
            break;
            
        default:
            decoder->type = LSD_DECODER_TYPE_UNKNOWN;
            break;
    }
    
    // Create Huffman trees
    decoder->lt_articles = lsd_huffman_tree_create();
    decoder->lt_headings = lsd_huffman_tree_create();
    decoder->lt_prefix_lengths = lsd_huffman_tree_create();
    decoder->lt_postfix_lengths = lsd_huffman_tree_create();
    
    if (!decoder->lt_articles || !decoder->lt_headings || 
        !decoder->lt_prefix_lengths || !decoder->lt_postfix_lengths) {
        lsd_decoder_destroy(decoder);
        return NULL;
    }
    
    return decoder;
}

void lsd_decoder_destroy(lsd_decoder *decoder) {
    if (!decoder) return;
    
    if (decoder->article_symbols) free(decoder->article_symbols);
    if (decoder->heading_symbols) free(decoder->heading_symbols);
    if (decoder->prefix) free(decoder->prefix);
    
    if (decoder->lt_articles) lsd_huffman_tree_destroy(decoder->lt_articles);
    if (decoder->lt_headings) lsd_huffman_tree_destroy(decoder->lt_headings);
    if (decoder->lt_prefix_lengths) lsd_huffman_tree_destroy(decoder->lt_prefix_lengths);
    if (decoder->lt_postfix_lengths) lsd_huffman_tree_destroy(decoder->lt_postfix_lengths);
    
    free(decoder);
}

// ============================================================
// Internal helper functions
// ============================================================

/**
 * Read Unicode string
 */
/**
 * Read symbol table
 */
static uint32_t *read_symbols(lsd_bitstream *bstr, size_t *count) {
    int len = (int)lsd_bitstream_read(bstr, 32);
    int bits_per_symbol = (int)lsd_bitstream_read(bstr, 8);
    
    if (len <= 0) {
        *count = 0;
        return NULL;
    }
    
    uint32_t *symbols = malloc(len * sizeof(uint32_t));
    if (!symbols) {
        *count = 0;
        return NULL;
    }
    
    for (int i = 0; i < len; i++) {
        symbols[i] = lsd_bitstream_read(bstr, bits_per_symbol);
    }
    
    *count = len;
    return symbols;
}

/**
 * Read XOR-encrypted symbol table (for abbreviation dictionaries)
 */
static uint32_t *read_xored_symbols(lsd_bitstream *bstr, size_t *count) {
    int len = (int)lsd_bitstream_read(bstr, 32);
    int bits_per_symbol = (int)lsd_bitstream_read(bstr, 8);
    
    if (len <= 0) {
        *count = 0;
        return NULL;
    }
    
    uint32_t *symbols = malloc(len * sizeof(uint32_t));
    if (!symbols) {
        *count = 0;
        return NULL;
    }
    
    for (int i = 0; i < len; i++) {
        symbols[i] = lsd_bitstream_read(bstr, bits_per_symbol) ^ 0x1325;
    }
    
    *count = len;
    return symbols;
}

/**
 * Read XOR-encrypted prefix (for abbreviation dictionaries)
 */
static uint16_t *read_xored_prefix(lsd_bitstream *bstr, int len, size_t *out_len) {
    if (len <= 0) {
        *out_len = 0;
        return NULL;
    }
    
    uint16_t *str = malloc((len + 1) * sizeof(uint16_t));
    if (!str) {
        *out_len = 0;
        return NULL;
    }
    
    for (int i = 0; i < len; i++) {
        str[i] = (uint16_t)(lsd_bitstream_read(bstr, 16) ^ 0x879A);
    }
    str[len] = 0;
    
    *out_len = len;
    return str;
}

/**
 * Read reference value
 */
static bool read_reference(lsd_bitstream *bstr, uint32_t *reference, uint32_t huffman_number) {
    int code = (int)lsd_bitstream_read(bstr, 2);
    
    if (code == 3) {
        *reference = lsd_bitstream_read(bstr, 32);
        return true;
    }
    
    int bitlen = lsd_bit_length(huffman_number);
    if (bitlen < 2) bitlen = 2;
    
    *reference = (code << (bitlen - 2)) | lsd_bitstream_read(bstr, bitlen - 2);
    return true;
}

// ============================================================
// Read decoder data
// ============================================================

/**
 * Read user dictionary decoder
 */
static bool load_user_decoder(lsd_decoder *decoder, lsd_bitstream *bstr) {
    // Read prefix
    int prefix_len = (int)lsd_bitstream_read(bstr, 32);
    decoder->prefix = lsd_bitstream_read_utf16_string(bstr, prefix_len, true, &decoder->prefix_length);

    // Read symbol tables
    decoder->article_symbols = read_symbols(bstr, &decoder->article_symbol_count);
    decoder->heading_symbols = read_symbols(bstr, &decoder->heading_symbol_count);

    // Read Huffman trees
    if (!lsd_huffman_tree_read(decoder->lt_articles, bstr)) return false;
    if (!lsd_huffman_tree_read(decoder->lt_headings, bstr)) return false;
    if (!lsd_huffman_tree_read(decoder->lt_prefix_lengths, bstr)) return false;
    if (!lsd_huffman_tree_read(decoder->lt_postfix_lengths, bstr)) return false;

    // Read reference encoding parameters
    decoder->huffman1_number = lsd_bitstream_read(bstr, 32);
    decoder->huffman2_number = lsd_bitstream_read(bstr, 32);

    return true;
}

/**
 * Read system dictionary decoder (with XOR encryption option)
 */
static bool load_system_decoder(lsd_decoder *decoder, lsd_bitstream *bstr) {
    if (decoder->use_xoring) {
        lsd_bitstream_enable_xor(bstr);
    }

    bool ok = false;

    // Read prefix
    int prefix_len = (int)lsd_bitstream_read(bstr, 32);
    decoder->prefix = lsd_bitstream_read_utf16_string(bstr, prefix_len, true, &decoder->prefix_length);

    // Read symbol tables
    decoder->article_symbols = read_symbols(bstr, &decoder->article_symbol_count);
    decoder->heading_symbols = read_symbols(bstr, &decoder->heading_symbol_count);

    // Read Huffman trees
    if (!lsd_huffman_tree_read(decoder->lt_articles, bstr)) goto cleanup;
    if (!lsd_huffman_tree_read(decoder->lt_headings, bstr)) goto cleanup;
    if (!lsd_huffman_tree_read(decoder->lt_postfix_lengths, bstr)) goto cleanup;

    // Skip a 32-bit value
    lsd_bitstream_read(bstr, 32);

    if (!lsd_huffman_tree_read(decoder->lt_prefix_lengths, bstr)) goto cleanup;

    // Read reference encoding parameters
    decoder->huffman1_number = lsd_bitstream_read(bstr, 32);
    decoder->huffman2_number = lsd_bitstream_read(bstr, 32);

    ok = true;

cleanup:
    if (decoder->use_xoring) {
        lsd_bitstream_disable_xor(bstr);
    }
    return ok;
}

/**
 * Read abbreviation dictionary decoder
 */
static bool load_abbreviation_decoder(lsd_decoder *decoder, lsd_bitstream *bstr) {
    // Read XOR-encrypted prefix
    int prefix_len = (int)lsd_bitstream_read(bstr, 32);
    decoder->prefix = read_xored_prefix(bstr, prefix_len, &decoder->prefix_length);

    // Read XOR-encrypted symbol tables
    decoder->article_symbols = read_xored_symbols(bstr, &decoder->article_symbol_count);
    decoder->heading_symbols = read_xored_symbols(bstr, &decoder->heading_symbol_count);

    // Read Huffman trees
    if (!lsd_huffman_tree_read(decoder->lt_articles, bstr)) return false;
    if (!lsd_huffman_tree_read(decoder->lt_headings, bstr)) return false;
    if (!lsd_huffman_tree_read(decoder->lt_prefix_lengths, bstr)) return false;
    if (!lsd_huffman_tree_read(decoder->lt_postfix_lengths, bstr)) return false;

    // Read reference encoding parameters
    decoder->huffman1_number = lsd_bitstream_read(bstr, 32);
    decoder->huffman2_number = lsd_bitstream_read(bstr, 32);
    
    return true;
}

bool lsd_decoder_load(lsd_decoder *decoder, lsd_bitstream *bstr) {
    if (!decoder || !bstr) return false;

    switch (decoder->type) {
        case LSD_DECODER_TYPE_USER:
            return load_user_decoder(decoder, bstr);

        case LSD_DECODER_TYPE_SYSTEM:
            return load_system_decoder(decoder, bstr);

        case LSD_DECODER_TYPE_ABBREVIATION:
            return load_abbreviation_decoder(decoder, bstr);

        default:
            return false;
    }
}

// ============================================================
// Decode operations
// ============================================================

bool lsd_decoder_decode_prefix_len(lsd_decoder *decoder, 
                                          lsd_bitstream *bstr, 
                                          uint32_t *length) {
    if (!decoder || !bstr || !length) return false;
    return lsd_huffman_tree_decode(decoder->lt_prefix_lengths, bstr, length) >= 0;
}

bool lsd_decoder_decode_postfix_len(lsd_decoder *decoder, 
                                           lsd_bitstream *bstr, 
                                           uint32_t *length) {
    if (!decoder || !bstr || !length) return false;
    return lsd_huffman_tree_decode(decoder->lt_postfix_lengths, bstr, length) >= 0;
}

bool lsd_decoder_decode_heading(lsd_decoder *decoder, 
                                       lsd_bitstream *bstr,
                                       uint32_t len,
                                       uint16_t **result,
                                       size_t *result_len) {
    if (!decoder || !bstr || !result || !result_len) return false;
    
    if (len == 0) {
        *result = malloc(sizeof(uint16_t));
        if (*result) (*result)[0] = 0;
        *result_len = 0;
        return true;
    }
    
    uint16_t *str = malloc((len + 1) * sizeof(uint16_t));
    if (!str) return false;
    
    for (uint32_t i = 0; i < len; i++) {
        uint32_t sym_idx;
        if (lsd_huffman_tree_decode(decoder->lt_headings, bstr, &sym_idx) < 0) {
            free(str);
            return false;
        }
        
        if (sym_idx >= decoder->heading_symbol_count) {
            free(str);
            return false;
        }
        
        uint32_t sym = decoder->heading_symbols[sym_idx];
        if (sym > 0xFFFF) {
            // Invalid character
            str[i] = '?';
        } else {
            str[i] = (uint16_t)sym;
        }
    }
    str[len] = 0;
    
    *result = str;
    *result_len = len;
    return true;
}

/**
 * User dictionary article decoding
 */
static bool decode_user_article(lsd_decoder *decoder, 
                                 lsd_bitstream *bstr,
                                 uint16_t **result,
                                 size_t *result_len) {
    // Read article length
    uint32_t len = lsd_bitstream_read(bstr, 16);
    if (len == 0xFFFF) {
        len = lsd_bitstream_read(bstr, 32);
    }
    
    if (len == 0) {
        *result = malloc(sizeof(uint16_t));
        if (*result) (*result)[0] = 0;
        *result_len = 0;
        return true;
    }
    
    // Pre-allocate result buffer
    size_t capacity = len + 256;
    uint16_t *str = malloc(capacity * sizeof(uint16_t));
    if (!str) return false;

    size_t pos = 0;

    while (pos < len) {
        uint32_t sym_idx;
        if (lsd_huffman_tree_decode(decoder->lt_articles, bstr, &sym_idx) < 0) {
            free(str);
            return false;
        }

        if (sym_idx >= decoder->article_symbol_count) {
            free(str);
            return false;
        }

        uint32_t sym = decoder->article_symbols[sym_idx];

        if (sym >= 0x10000) {
            // Special encoding: reference to prefix or previous content
            if (sym >= 0x10040) {
                // Reference to previous content
                uint32_t start_idx = lsd_bitstream_read(bstr, lsd_bit_length(len));
                uint32_t copy_len = sym - 0x1003d;

                // Ensure sufficient space
                if (pos + copy_len > capacity) {
                    capacity = pos + copy_len + 256;
                    uint16_t *new_str = realloc(str, capacity * sizeof(uint16_t));
                    if (!new_str) {
                        free(str);
                        return false;
                    }
                    str = new_str;
                }
                
                // Copy previous content
                for (uint32_t i = 0; i < copy_len && start_idx + i < pos; i++) {
                    str[pos++] = str[start_idx + i];
                }
            } else {
                // Reference to prefix
                uint32_t start_idx = lsd_bitstream_read(bstr, lsd_bit_length((uint32_t)decoder->prefix_length));
                uint32_t copy_len = sym - 0xfffd;
                
                // Ensure sufficient space
                if (pos + copy_len > capacity) {
                    capacity = pos + copy_len + 256;
                    uint16_t *new_str = realloc(str, capacity * sizeof(uint16_t));
                    if (!new_str) {
                        free(str);
                        return false;
                    }
                    str = new_str;
                }

                // Copy from prefix
                for (uint32_t i = 0; i < copy_len && start_idx + i < decoder->prefix_length; i++) {
                    str[pos++] = decoder->prefix[start_idx + i];
                }
            }
        } else {
            // Ordinary character
            if (pos >= capacity) {
                capacity *= 2;
                uint16_t *new_str = realloc(str, capacity * sizeof(uint16_t));
                if (!new_str) {
                    free(str);
                    return false;
                }
                str = new_str;
            }
            str[pos++] = (uint16_t)sym;
        }
    }

    // Add null terminator
    str[pos] = 0;

    *result = str;
    *result_len = pos;
    return true;
}

/**
 * System dictionary article decoding
 */
static bool decode_system_article(lsd_decoder *decoder,
                                   lsd_bitstream *bstr,
                                   uint16_t **result,
                                   size_t *result_len) {
    if (decoder->use_xoring) {
        lsd_bitstream_enable_xor(bstr);
    }

    bool ok = false;
    uint16_t *str = NULL;

    // Read article length
    uint32_t maxlen = lsd_bitstream_read(bstr, 16);
    if (maxlen == 0xFFFF) {
        maxlen = lsd_bitstream_read(bstr, 32);
    }

    if (maxlen == 0) {
        str = malloc(sizeof(uint16_t));
        if (!str) goto cleanup;
        str[0] = 0;
        *result = str;
        *result_len = 0;
        ok = true;
        goto cleanup;
    }

    // Pre-allocate result buffer
    size_t capacity = maxlen + 256;
    str = malloc(capacity * sizeof(uint16_t));
    if (!str) goto cleanup;

    size_t pos = 0;

    while (pos < maxlen) {
        uint32_t sym_idx;

        if (lsd_huffman_tree_decode(decoder->lt_articles, bstr, &sym_idx) < 0) goto cleanup;
        if (sym_idx >= decoder->article_symbol_count) goto cleanup;

        uint32_t sym = decoder->article_symbols[sym_idx];

        // System dictionary uses a different encoding scheme
        // sym < 0x80 indicates a reference (to prefix or already decoded content)
        // sym >= 0x80 indicates an ordinary character
        if (sym < 0x80) {
            if (sym <= 0x3F) {
                // Reference to prefix
                uint32_t start_idx = lsd_bitstream_read(bstr, lsd_bit_length((uint32_t)decoder->prefix_length));
                uint32_t copy_len = sym + 3;

                if (pos + copy_len > capacity) {
                    capacity = pos + copy_len + 256;
                    uint16_t *new_str = realloc(str, capacity * sizeof(uint16_t));
                    if (!new_str) goto cleanup;
                    str = new_str;
                }

                for (uint32_t i = 0; i < copy_len && start_idx + i < decoder->prefix_length; i++) {
                    str[pos++] = decoder->prefix[start_idx + i];
                }
            } else {
                // Reference to previous content
                uint32_t start_idx = lsd_bitstream_read(bstr, lsd_bit_length(maxlen));
                uint32_t copy_len = sym - 0x3d;

                if (pos + copy_len > capacity) {
                    capacity = pos + copy_len + 256;
                    uint16_t *new_str = realloc(str, capacity * sizeof(uint16_t));
                    if (!new_str) goto cleanup;
                    str = new_str;
                }

                for (uint32_t i = 0; i < copy_len && start_idx + i < pos; i++) {
                    str[pos++] = str[start_idx + i];
                }
            }
        } else {
            // Ordinary character
            if (pos >= capacity) {
                capacity *= 2;
                uint16_t *new_str = realloc(str, capacity * sizeof(uint16_t));
                if (!new_str) goto cleanup;
                str = new_str;
            }
            str[pos++] = (uint16_t)(sym - 0x80);
        }
    }

    // Add null terminator
    str[pos] = 0;
    *result = str;
    *result_len = pos;
    str = NULL;  // Prevent freeing in cleanup
    ok = true;

cleanup:
    if (decoder->use_xoring) {
        lsd_bitstream_disable_xor(bstr);
    }
    free(str);  // Free on failure; on success str=NULL, no-op
    return ok;
}

bool lsd_decoder_decode_article(lsd_decoder *decoder, 
                                       lsd_bitstream *bstr,
                                       uint16_t **result,
                                       size_t *result_len) {
    if (!decoder || !bstr || !result || !result_len) return false;
    
    switch (decoder->type) {
        case LSD_DECODER_TYPE_USER:
            if (decoder->legacy_system) {
                return decode_system_article(decoder, bstr, result, result_len);
            }
            return decode_user_article(decoder, bstr, result, result_len);
            
        case LSD_DECODER_TYPE_SYSTEM:
            return decode_system_article(decoder, bstr, result, result_len);
            
        case LSD_DECODER_TYPE_ABBREVIATION:
            return decode_user_article(decoder, bstr, result, result_len);
            
        default:
            return false;
    }
}

bool lsd_decoder_read_reference1(lsd_decoder *decoder, 
                                        lsd_bitstream *bstr, 
                                        uint32_t *reference) {
    if (!decoder || !bstr || !reference) return false;
    return read_reference(bstr, reference, decoder->huffman1_number);
}

bool lsd_decoder_read_reference2(lsd_decoder *decoder, 
                                        lsd_bitstream *bstr, 
                                        uint32_t *reference) {
    if (!decoder || !bstr || !reference) return false;
    return read_reference(bstr, reference, decoder->huffman2_number);
}

const uint16_t *lsd_decoder_get_prefix(lsd_decoder *decoder) {
    return decoder ? decoder->prefix : NULL;
}

size_t lsd_decoder_get_prefix_length(lsd_decoder *decoder) {
    return decoder ? decoder->prefix_length : 0;
}
