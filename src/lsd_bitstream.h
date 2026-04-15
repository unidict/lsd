//
//  lsd_bitstream.h
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#ifndef lsd_bitstream_h
#define lsd_bitstream_h

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Bitstream reader (opaque type)
// ============================================================
typedef struct lsd_bitstream lsd_bitstream;

// ============================================================
// Creation and destruction
// ============================================================

/**
 * Create a bitstream reader from a file
 * @param file File handle (caller is responsible for managing its lifetime)
 * @return Bitstream reader, or NULL on failure
 */
lsd_bitstream *lsd_bitstream_create_from_file(FILE *file);

/**
 * Create a bitstream reader from memory
 * @param buffer Memory buffer
 * @param size Buffer size
 * @return Bitstream reader, or NULL on failure
 */
lsd_bitstream *lsd_bitstream_create_from_memory(const uint8_t *buffer, size_t size);

/**
 * Destroy a bitstream reader
 * @param bstr Bitstream reader
 */
void lsd_bitstream_destroy(lsd_bitstream *bstr);

// ============================================================
// Bit operations
// ============================================================

/**
 * Read an unsigned integer with the specified number of bits
 * @param bstr Bitstream reader
 * @param bit_count Number of bits (1-32)
 * @return The value read
 */
uint32_t lsd_bitstream_read(lsd_bitstream *bstr, uint8_t bit_count);

/**
 * Read 1 bit
 */
static inline uint8_t lsd_bitstream_read_bit(lsd_bitstream *bstr) {
    return (uint8_t)lsd_bitstream_read(bstr, 1);
}

/**
 * Read byte data
 * @param bstr Bitstream reader
 * @param dest Destination buffer
 * @param byte_count Number of bytes
 * @return Actual number of bytes read
 */
size_t lsd_bitstream_read_bytes(lsd_bitstream *bstr, void *dest, size_t byte_count);

/**
 * Align to byte boundary
 */
void lsd_bitstream_align_to_byte(lsd_bitstream *bstr);

// ============================================================
// Position operations
// ============================================================

/**
 * Seek to the specified byte position
 */
void lsd_bitstream_seek(lsd_bitstream *bstr, size_t byte_pos);

/**
 * Get the current byte position
 */
size_t lsd_bitstream_tell(lsd_bitstream *bstr);

// ============================================================
// XOR decryption mode
// ============================================================

/**
 * Enable XOR decryption mode
 * Once enabled, all read operations will automatically apply XOR decryption
 * @param bstr Bitstream reader
 */
void lsd_bitstream_enable_xor(lsd_bitstream *bstr);

/**
 * Disable XOR decryption mode
 * @param bstr Bitstream reader
 */
void lsd_bitstream_disable_xor(lsd_bitstream *bstr);

// ============================================================
// Utility functions
// ============================================================

/**
 * Calculate the number of bits required to represent a number
 * @param num Number
 * @return Number of bits
 */
uint8_t lsd_bit_length(uint32_t num);

/**
 * Reverse byte order (16-bit)
 */
uint16_t lsd_reverse16(uint16_t n);

/**
 * Reverse byte order (32-bit)
 */
uint32_t lsd_reverse32(uint32_t n);

/**
 * Read a UTF-16 string from the bitstream
 * @param bstr Bitstream reader
 * @param len Number of characters
 * @param big_endian Whether the file uses big-endian byte order
 * @param out_len Output string length
 * @return UTF-16 string (host byte order), caller must free it. Returns NULL on failure
 */
uint16_t *lsd_bitstream_read_utf16_string(lsd_bitstream *bstr, int len, bool big_endian, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* lsd_bitstream_h */
