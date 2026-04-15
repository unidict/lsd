//
//  lsd_utils.h
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#ifndef lsd_utils_h
#define lsd_utils_h

#include <stdio.h>
#include <stdint.h>

// ============================================================
// UTF-16 string utilities
// ============================================================

/**
 * Duplicate a UTF-16 string
 */
uint16_t *lsd_utf16_dup(const uint16_t *str, size_t len);

/**
 * Concatenate two UTF-16 strings
 */
uint16_t *lsd_utf16_concat(const uint16_t *str1, size_t len1,
                               const uint16_t *str2, size_t len2,
                               size_t *out_len);

/**
 * Get the length of a null-terminated UTF-16 string
 */
size_t lsd_utf16_len(const uint16_t *str);

/**
 * Normalize a null-terminated UTF-16 string in place:
 *   - Strip leading/trailing whitespace (space, tab)
 *   - Collapse consecutive whitespace into a single space
 * Returns str itself; returns NULL if str is NULL.
 * Length after normalization can be obtained with lsd_utf16_len().
 */
uint16_t *lsd_utf16_normalize(uint16_t *str);

/**
 * Case-insensitive comparison of UTF-16 strings (no codepoint tie-break)
 * For lookup scenarios
 */
int lsd_utf16_casecmp(const uint16_t *str1, size_t len1,
                          const uint16_t *str2, size_t len2);

/**
 * Two-phase comparison of UTF-16 strings (case-insensitive + codepoint tie-break)
 * For sorting scenarios
 */
int lsd_utf16_cmp(const uint16_t *str1, size_t len1,
                     const uint16_t *str2, size_t len2);

/**
 * Prefix comparison of UTF-16 strings (case-insensitive)
 */
int lsd_utf16_prefix_cmp(const uint16_t *str1, size_t len1,
                             const uint16_t *str2, size_t len2);

/**
 * Convert UTF-16 to UTF-8
 * @param utf16     UTF-16 string
 * @param utf16_len Length of the UTF-16 string
 * @param utf8      Output UTF-8 string (caller must free)
 * @return 0 on success
 */
int lsd_utf16_to_utf8(const uint16_t *utf16, size_t utf16_len, char **utf8);

/**
 * Convert UTF-8 to UTF-16
 * @param utf8      UTF-8 string
 * @param utf16     Output UTF-16 string (caller must free)
 * @param utf16_len Output length of the UTF-16 string
 * @return 0 on success
 */
int lsd_utf8_to_utf16(const char *utf8, uint16_t **utf16, size_t *utf16_len);
#endif /* lsd_utils_h */
