//
//  lsd_decoder.h
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#ifndef lingvo_decoder_h
#define lingvo_decoder_h

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
typedef struct lsd_bitstream lsd_bitstream;

// ============================================================
// Dictionary decoder (opaque type)
// ============================================================
typedef struct lsd_decoder lsd_decoder;

// ============================================================
// Create and destroy
// ============================================================

/**
 * Create a decoder based on the version number
 * @param version LSD version number
 * @return Decoder, or NULL on failure
 */
lsd_decoder *lsd_decoder_create(uint32_t version);

/**
 * Destroy the decoder
 */
void lsd_decoder_destroy(lsd_decoder *decoder);

// ============================================================
// Read decoder data
// ============================================================

/**
 * Read decoder data from a bitstream
 * @param decoder Decoder
 * @param bstr Bitstream reader
 * @return true on success
 */
bool lsd_decoder_load(lsd_decoder *decoder, lsd_bitstream *bstr);

// ============================================================
// Decode operations
// ============================================================

/**
 * Decode prefix length
 * @param decoder Decoder
 * @param bstr Bitstream reader
 * @param length Output length
 * @return true on success
 */
bool lsd_decoder_decode_prefix_len(lsd_decoder *decoder, 
                                          lsd_bitstream *bstr, 
                                          uint32_t *length);

/**
 * Decode postfix length
 * @param decoder Decoder
 * @param bstr Bitstream reader
 * @param length Output length
 * @return true on success
 */
bool lsd_decoder_decode_postfix_len(lsd_decoder *decoder, 
                                           lsd_bitstream *bstr, 
                                           uint32_t *length);

/**
 * Decode heading (entry name)
 * @param decoder Decoder
 * @param bstr Bitstream reader
 * @param len Heading length (number of characters)
 * @param result Output UTF-16 string (caller must free)
 * @param result_len Output string length
 * @return true on success
 */
bool lsd_decoder_decode_heading(lsd_decoder *decoder, 
                                       lsd_bitstream *bstr,
                                       uint32_t len,
                                       uint16_t **result,
                                       size_t *result_len);

/**
 * Decode article (entry content)
 * @param decoder Decoder
 * @param bstr Bitstream reader
 * @param result Output UTF-16 string (caller must free)
 * @param result_len Output string length
 * @return true on success
 */
bool lsd_decoder_decode_article(lsd_decoder *decoder, 
                                       lsd_bitstream *bstr,
                                       uint16_t **result,
                                       size_t *result_len);

/**
 * Read reference 1 (used for B+ tree nodes)
 * @param decoder Decoder
 * @param bstr Bitstream reader
 * @param reference Output reference value
 * @return true on success
 */
bool lsd_decoder_read_reference1(lsd_decoder *decoder, 
                                        lsd_bitstream *bstr, 
                                        uint32_t *reference);

/**
 * Read reference 2 (used for article references)
 * @param decoder Decoder
 * @param bstr Bitstream reader
 * @param reference Output reference value
 * @return true on success
 */
bool lsd_decoder_read_reference2(lsd_decoder *decoder, 
                                        lsd_bitstream *bstr, 
                                        uint32_t *reference);

/**
 * Get the prefix string
 * @param decoder Decoder
 * @return Prefix string (UTF-16)
 */
const uint16_t *lsd_decoder_get_prefix(lsd_decoder *decoder);

/**
 * Get the prefix string length
 */
size_t lsd_decoder_get_prefix_length(lsd_decoder *decoder);

// ============================================================
// Version helper functions
// ============================================================

/**
 * Check if the version is supported
 * @param version LSD version number
 * @return true if supported
 */
bool lsd_is_version_supported(uint32_t version);

#ifdef __cplusplus
}
#endif

#endif /* lingvo_decoder_h */
