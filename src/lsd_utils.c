//
//  lsd_utils.c
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#include "lsd_utils.h"
#include <unicase.h>
#include <unistr.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// UTF-16 string helpers
// ============================================================

uint16_t *lsd_utf16_dup(const uint16_t *str, size_t len) {
    if (!str) return NULL;

    uint16_t *dup = malloc((len + 1) * sizeof(uint16_t));
    if (dup) {
        memcpy(dup, str, len * sizeof(uint16_t));
        dup[len] = 0;
    }
    return dup;
}

uint16_t *lsd_utf16_concat(const uint16_t *str1, size_t len1,
                               const uint16_t *str2, size_t len2,
                               size_t *out_len) {
    size_t total_len = len1 + len2;
    uint16_t *result = malloc((total_len + 1) * sizeof(uint16_t));
    if (!result) {
        *out_len = 0;
        return NULL;
    }

    if (str1 && len1 > 0) {
        memcpy(result, str1, len1 * sizeof(uint16_t));
    }
    if (str2 && len2 > 0) {
        memcpy(result + len1, str2, len2 * sizeof(uint16_t));
    }
    result[total_len] = 0;

    *out_len = total_len;
    return result;
}

size_t lsd_utf16_len(const uint16_t *str) {
    if (!str) return 0;
    size_t len = 0;
    while (str[len] != 0) len++;
    return len;
}

static bool is_ws(uint16_t c) {
    return c == 0x0020 || c == 0x0009;
}

uint16_t *lsd_utf16_normalize(uint16_t *str) {
    if (!str) return NULL;

    size_t len = lsd_utf16_len(str);
    size_t i = 0, j = 0;

    // Skip leading whitespace
    while (i < len && is_ws(str[i])) i++;

    while (i < len) {
        if (is_ws(str[i])) {
            // Collapse consecutive whitespace into a single space
            str[j++] = 0x0020;
            while (i < len && is_ws(str[i])) i++;
        } else {
            str[j++] = str[i++];
        }
    }

    // Strip trailing space
    if (j > 0 && is_ws(str[j - 1])) j--;

    str[j] = 0;
    return str;
}

/**
 * Pure case-insensitive comparison (no codepoint tie-break)
 * For lookup: haus == Haus
 */
int lsd_utf16_casecmp(const uint16_t *str1, size_t len1,
                          const uint16_t *str2, size_t len2) {
    size_t i1 = 0, i2 = 0;

    while (i1 < len1 && i2 < len2) {
        ucs4_t cp1, cp2;
        int n1 = u16_mbtouc(&cp1, str1 + i1, len1 - i1);
        int n2 = u16_mbtouc(&cp2, str2 + i2, len2 - i2);

        ucs4_t lc1 = uc_tolower(cp1);
        ucs4_t lc2 = uc_tolower(cp2);
        if (lc1 != lc2)
            return lc1 < lc2 ? -1 : 1;

        i1 += n1;
        i2 += n2;
    }

    if (i1 < len1) return 1;
    if (i2 < len2) return -1;
    return 0;
}

/**
 * Two-phase comparison (case-insensitive + codepoint tie-break)
 * For sorting: Haus < haus (by Unicode codepoint)
 */
int lsd_utf16_cmp(const uint16_t *str1, size_t len1,
                     const uint16_t *str2, size_t len2) {
    // Phase 1: case-insensitive comparison
    int ci = lsd_utf16_casecmp(str1, len1, str2, len2);
    if (ci != 0) return ci;

    // Phase 2: when only case differs, tie-break by original codepoint
    size_t i1 = 0, i2 = 0;
    while (i1 < len1 && i2 < len2) {
        ucs4_t cp1, cp2;
        int n1 = u16_mbtouc(&cp1, str1 + i1, len1 - i1);
        int n2 = u16_mbtouc(&cp2, str2 + i2, len2 - i2);

        if (cp1 != cp2)
            return cp1 < cp2 ? -1 : 1;

        i1 += n1;
        i2 += n2;
    }

    return 0;
}

int lsd_utf16_prefix_cmp(const uint16_t *str1, size_t len1,
                             const uint16_t *str2, size_t len2) {
    // str2 is the prefix; check if str1 starts with str2
    if (len2 > len1) {
        return lsd_utf16_casecmp(str1, len1, str2, len2);
    }

    size_t i1 = 0, i2 = 0;
    while (i2 < len2) {
        ucs4_t cp1, cp2;
        int n1 = u16_mbtouc(&cp1, str1 + i1, len1 - i1);
        int n2 = u16_mbtouc(&cp2, str2 + i2, len2 - i2);

        ucs4_t lc1 = uc_tolower(cp1);
        ucs4_t lc2 = uc_tolower(cp2);
        if (lc1 != lc2)
            return lc1 < lc2 ? -1 : 1;

        i1 += n1;
        i2 += n2;
    }

    return 0;  // Prefix match
}

int lsd_utf16_to_utf8(const uint16_t *utf16, size_t utf16_len, char **utf8) {
    if (!utf16 || !utf8) return -1;

    size_t result_len = 0;
    uint8_t *result = u16_to_u8(utf16, utf16_len, NULL, &result_len);
    if (!result) return -1;

    // u16_to_u8 does not guarantee null-termination; append a byte
    char *str = realloc(result, result_len + 1);
    if (!str) {
        free(result);
        return -1;
    }
    str[result_len] = '\0';

    *utf8 = str;
    return 0;
}

int lsd_utf8_to_utf16(const char *utf8, uint16_t **utf16, size_t *utf16_len) {
    if (!utf8 || !utf16 || !utf16_len) return -1;

    size_t utf8_bytes = strlen(utf8);
    size_t result_len = 0;
    uint16_t *result = u8_to_u16((const uint8_t *)utf8, utf8_bytes, NULL, &result_len);
    if (!result) return -1;

    // Append null terminator
    uint16_t *str = realloc(result, (result_len + 1) * sizeof(uint16_t));
    if (!str) {
        free(result);
        return -1;
    }
    str[result_len] = 0;

    *utf16 = str;
    *utf16_len = result_len;
    return 0;
}
