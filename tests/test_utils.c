//
//  test_utils.c
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#include "unity.h"
#include "lsd_utils.h"
#include <stdlib.h>
#include <string.h>

// ============================================================
// UTF-16 dup
// ============================================================

void test_utf16_dup_basic(void) {
    uint16_t str[] = {0x0048, 0x0065, 0x006C, 0x006C, 0x006F, 0}; // "Hello"
    uint16_t *dup = lsd_utf16_dup(str, 5);
    TEST_ASSERT_NOT_NULL(dup);
    TEST_ASSERT_EQUAL_UINT16(0x0048, dup[0]);
    TEST_ASSERT_EQUAL_UINT16(0x006F, dup[4]);
    TEST_ASSERT_EQUAL_UINT16(0, dup[5]);
    free(dup);
}

void test_utf16_dup_null(void) {
    uint16_t *dup = lsd_utf16_dup(NULL, 0);
    TEST_ASSERT_NULL(dup);
}

// ============================================================
// UTF-16 concat
// ============================================================

void test_utf16_concat_basic(void) {
    uint16_t a[] = {0x0041, 0x0042, 0}; // "AB"
    uint16_t b[] = {0x0043, 0x0044, 0}; // "CD"
    size_t out_len = 0;
    uint16_t *result = lsd_utf16_concat(a, 2, b, 2, &out_len);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_size_t(4, out_len);
    TEST_ASSERT_EQUAL_UINT16(0x0041, result[0]);
    TEST_ASSERT_EQUAL_UINT16(0x0044, result[3]);
    TEST_ASSERT_EQUAL_UINT16(0, result[4]);
    free(result);
}

void test_utf16_concat_empty_first(void) {
    uint16_t b[] = {0x0043, 0};
    size_t out_len = 0;
    uint16_t *result = lsd_utf16_concat(NULL, 0, b, 1, &out_len);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_size_t(1, out_len);
    free(result);
}

// ============================================================
// UTF-16 compare
// ============================================================

void test_utf16_cmp_equal(void) {
    uint16_t a[] = {0x0041, 0x0042};
    uint16_t b[] = {0x0041, 0x0042};
    TEST_ASSERT_EQUAL(0, lsd_utf16_cmp(a, 2, b, 2));
}

void test_utf16_cmp_less(void) {
    uint16_t a[] = {0x0041};
    uint16_t b[] = {0x0042};
    TEST_ASSERT_TRUE(lsd_utf16_cmp(a, 1, b, 1) < 0);
}

void test_utf16_cmp_case_insensitive(void) {
    uint16_t a[] = {0x0041}; // 'A'
    uint16_t b[] = {0x0061}; // 'a'
    // casecmp: case-insensitive, should be equal
    TEST_ASSERT_EQUAL(0, lsd_utf16_casecmp(a, 1, b, 1));
    // cmp: tie-break on codepoint, 'A'(0x41) < 'a'(0x61)
    TEST_ASSERT_TRUE(lsd_utf16_cmp(a, 1, b, 1) < 0);
}

// ============================================================
// UTF-16 to UTF-8
// ============================================================

void test_utf16_to_utf8_ascii(void) {
    uint16_t str[] = {0x0048, 0x0069, 0}; // "Hi"
    char *utf8 = NULL;
    TEST_ASSERT_EQUAL(0, lsd_utf16_to_utf8(str, 2, &utf8));
    TEST_ASSERT_EQUAL_STRING("Hi", utf8);
    free(utf8);
}

void test_utf16_to_utf8_chinese(void) {
    uint16_t str[] = {0x4F60, 0x597D, 0}; // "你好"
    char *utf8 = NULL;
    TEST_ASSERT_EQUAL(0, lsd_utf16_to_utf8(str, 2, &utf8));
    TEST_ASSERT_NOT_NULL(utf8);
    // "你好" in UTF-8: E4 BD A0 E5 A5 BD
    TEST_ASSERT_EQUAL_UINT8(0xE4, (uint8_t)utf8[0]);
    free(utf8);
}

void test_utf16_to_utf8_null(void) {
    char *utf8 = NULL;
    TEST_ASSERT_NOT_EQUAL(0, lsd_utf16_to_utf8(NULL, 0, &utf8));
}

// ============================================================
// UTF-8 to UTF-16
// ============================================================

void test_utf8_to_utf16_ascii(void) {
    uint16_t *utf16 = NULL;
    size_t len = 0;
    TEST_ASSERT_EQUAL(0, lsd_utf8_to_utf16("Hi", &utf16, &len));
    TEST_ASSERT_EQUAL_size_t(2, len);
    TEST_ASSERT_EQUAL_UINT16(0x0048, utf16[0]);
    TEST_ASSERT_EQUAL_UINT16(0x0069, utf16[1]);
    free(utf16);
}

void test_utf8_to_utf16_null(void) {
    uint16_t *utf16 = NULL;
    size_t len = 0;
    TEST_ASSERT_NOT_EQUAL(0, lsd_utf8_to_utf16(NULL, &utf16, &len));
}

// ============================================================
// Round-trip
// ============================================================

void test_utf16_utf8_roundtrip(void) {
    uint16_t original[] = {0x0041, 0x0042, 0x4E2D, 0x6587, 0}; // "AB中文"
    char *utf8 = NULL;
    TEST_ASSERT_EQUAL(0, lsd_utf16_to_utf8(original, 4, &utf8));

    uint16_t *back = NULL;
    size_t back_len = 0;
    TEST_ASSERT_EQUAL(0, lsd_utf8_to_utf16(utf8, &back, &back_len));
    TEST_ASSERT_EQUAL_size_t(4, back_len);
    TEST_ASSERT_EQUAL_UINT16(original[0], back[0]);
    TEST_ASSERT_EQUAL_UINT16(original[3], back[3]);

    free(utf8);
    free(back);
}

// ============================================================
// Normalize
// ============================================================

void test_normalize_basic(void) {
    uint16_t str[] = {0x0048, 0x0069, 0}; // "Hi"
    uint16_t *result = lsd_utf16_normalize(str);
    TEST_ASSERT_EQUAL_PTR(str, result);
    TEST_ASSERT_EQUAL_size_t(2, lsd_utf16_len(result));
    TEST_ASSERT_EQUAL_UINT16(0x0048, result[0]);
    TEST_ASSERT_EQUAL_UINT16(0x0069, result[1]);
}

void test_normalize_trim_leading(void) {
    uint16_t str[] = {0x0020, 0x0009, 0x0048, 0x0069, 0}; // "  \tHi"
    uint16_t *result = lsd_utf16_normalize(str);
    TEST_ASSERT_EQUAL_PTR(str, result);
    TEST_ASSERT_EQUAL_size_t(2, lsd_utf16_len(result));
    TEST_ASSERT_EQUAL_UINT16(0x0048, result[0]);
    TEST_ASSERT_EQUAL_UINT16(0x0069, result[1]);
}

void test_normalize_trim_trailing(void) {
    uint16_t str[] = {0x0048, 0x0069, 0x0020, 0x0009, 0}; // "Hi \t"
    uint16_t *result = lsd_utf16_normalize(str);
    TEST_ASSERT_EQUAL_PTR(str, result);
    TEST_ASSERT_EQUAL_size_t(2, lsd_utf16_len(result));
    TEST_ASSERT_EQUAL_UINT16(0x0048, result[0]);
    TEST_ASSERT_EQUAL_UINT16(0x0069, result[1]);
}

void test_normalize_collapse_spaces(void) {
    uint16_t str[] = {0x0048, 0x0020, 0x0009, 0x0020, 0x0069, 0}; // "H  \t i"
    uint16_t *result = lsd_utf16_normalize(str);
    TEST_ASSERT_EQUAL_PTR(str, result);
    TEST_ASSERT_EQUAL_size_t(3, lsd_utf16_len(result));
    TEST_ASSERT_EQUAL_UINT16(0x0048, result[0]);
    TEST_ASSERT_EQUAL_UINT16(0x0020, result[1]);
    TEST_ASSERT_EQUAL_UINT16(0x0069, result[2]);
}

void test_normalize_all_combined(void) {
    // "  hello   world  " → "hello world"
    uint16_t str[] = {0x0020, 0x0068, 0x0065, 0x006C, 0x006C, 0x006F,
                      0x0020, 0x0009, 0x0020,
                      0x0077, 0x006F, 0x0072, 0x006C, 0x0064,
                      0x0020, 0x0009, 0};
    uint16_t *result = lsd_utf16_normalize(str);
    TEST_ASSERT_EQUAL_PTR(str, result);
    TEST_ASSERT_EQUAL_size_t(11, lsd_utf16_len(result));
    uint16_t expected[] = {0x0068, 0x0065, 0x006C, 0x006C, 0x006F,
                           0x0020,
                           0x0077, 0x006F, 0x0072, 0x006C, 0x0064, 0};
    for (int i = 0; i < 11; i++) {
        TEST_ASSERT_EQUAL_UINT16(expected[i], result[i]);
    }
}

void test_normalize_empty(void) {
    uint16_t str[] = {0x0020, 0x0009, 0x0020, 0}; // all whitespace
    uint16_t *result = lsd_utf16_normalize(str);
    TEST_ASSERT_EQUAL_PTR(str, result);
    TEST_ASSERT_EQUAL_size_t(0, lsd_utf16_len(result));
}

void test_normalize_null(void) {
    TEST_ASSERT_NULL(lsd_utf16_normalize(NULL));
}

// ============================================================
// Test Runner
// ============================================================

void run_utils_tests(void) {
    UnityBegin("test_utils.c");

    RUN_TEST(test_utf16_dup_basic);
    RUN_TEST(test_utf16_dup_null);

    RUN_TEST(test_utf16_concat_basic);
    RUN_TEST(test_utf16_concat_empty_first);

    RUN_TEST(test_utf16_cmp_equal);
    RUN_TEST(test_utf16_cmp_less);
    RUN_TEST(test_utf16_cmp_case_insensitive);

    RUN_TEST(test_utf16_to_utf8_ascii);
    RUN_TEST(test_utf16_to_utf8_chinese);
    RUN_TEST(test_utf16_to_utf8_null);

    RUN_TEST(test_utf8_to_utf16_ascii);
    RUN_TEST(test_utf8_to_utf16_null);

    RUN_TEST(test_utf16_utf8_roundtrip);

    RUN_TEST(test_normalize_basic);
    RUN_TEST(test_normalize_trim_leading);
    RUN_TEST(test_normalize_trim_trailing);
    RUN_TEST(test_normalize_collapse_spaces);
    RUN_TEST(test_normalize_all_combined);
    RUN_TEST(test_normalize_empty);
    RUN_TEST(test_normalize_null);

    UnityEnd();
}
