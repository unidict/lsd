//
//  test_bitstream.c
//  libud
//
//  Created by kejinlu on 2026/04/12.
//

#include "unity.h"
#include "lsd_bitstream.h"
#include <stdlib.h>
#include <string.h>

// ============================================================
// 测试数据（与 C++ lingvo-tests.cpp bitStreamTest 一致）
// ============================================================

static const uint8_t test_buf[] = { 0x13, 0xF0, 0xF9, 0x11, 0x12, 0x45 };

// ============================================================
// 创建和销毁
// ============================================================

void test_bitstream_create_null_buffer(void) {
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(NULL, 6);
    TEST_ASSERT_NULL(bstr);
}

void test_bitstream_create_zero_size(void) {
    uint8_t buf[1] = {0};
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(buf, 0);
    TEST_ASSERT_NULL(bstr);
}

void test_bitstream_create_valid(void) {
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));
    TEST_ASSERT_NOT_NULL(bstr);
    lsd_bitstream_destroy(bstr);
}

void test_bitstream_destroy_null(void) {
    // 不应崩溃
    lsd_bitstream_destroy(NULL);
}

// ============================================================
// 位级读取（对应 C++ bitStreamTest 的逐位读取）
// ============================================================

void test_bitstream_read_bits_reconstruct_byte(void) {
    // C++ 测试：read(3), read(4), read(1) → 组合后等于 0x13
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    uint32_t b1 = lsd_bitstream_read(bstr, 3);   // 高 3 位
    uint32_t b2 = lsd_bitstream_read(bstr, 4);   // 中 4 位
    uint32_t b3 = lsd_bitstream_read(bstr, 1);   // 低 1 位

    TEST_ASSERT_EQUAL_UINT32((b1 << 5) | (b2 << 1) | b3, 0x13);

    lsd_bitstream_destroy(bstr);
}

void test_bitstream_read_8bit(void) {
    // 读完第一字节后再读 8 bit 应得到 0xF0
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    lsd_bitstream_read(bstr, 8);                  // 跳过第一字节
    uint32_t val = lsd_bitstream_read(bstr, 8);   // 读第二字节
    TEST_ASSERT_EQUAL_UINT32(0xF0, val);

    lsd_bitstream_destroy(bstr);
}

void test_bitstream_read_32bit(void) {
    // C++ 测试：seek(0) 后 read(32) → 0x13F0F911
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    uint32_t val = lsd_bitstream_read(bstr, 32);
    TEST_ASSERT_EQUAL_UINT32(0x13F0F911, val);

    lsd_bitstream_destroy(bstr);
}

void test_bitstream_read_32bit_from_offset(void) {
    // C++ 测试：seek(2) 后 read(32) → 0xF9111245
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    lsd_bitstream_seek(bstr, 2);
    uint32_t val = lsd_bitstream_read(bstr, 32);
    TEST_ASSERT_EQUAL_UINT32(0xF9111245, val);

    lsd_bitstream_destroy(bstr);
}

void test_bitstream_read_single_bit(void) {
    // 0x13 = 0001 0011
    // 第一位应为 0，第二位应为 0，第三位应为 0，第四位应为 1
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    TEST_ASSERT_EQUAL_UINT32(0, lsd_bitstream_read(bstr, 1));
    TEST_ASSERT_EQUAL_UINT32(0, lsd_bitstream_read(bstr, 1));
    TEST_ASSERT_EQUAL_UINT32(0, lsd_bitstream_read(bstr, 1));
    TEST_ASSERT_EQUAL_UINT32(1, lsd_bitstream_read(bstr, 1));

    lsd_bitstream_destroy(bstr);
}

void test_bitstream_read_zero_bits(void) {
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));
    TEST_ASSERT_EQUAL_UINT32(0, lsd_bitstream_read(bstr, 0));
    lsd_bitstream_destroy(bstr);
}

// ============================================================
// seek / tell（对应 C++ bitStreamTest 的 seek 和 tell）
// ============================================================

void test_bitstream_tell_after_read_bits(void) {
    // 读 8 bit 后 tell 应为 1
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    lsd_bitstream_read(bstr, 8);
    TEST_ASSERT_EQUAL_size_t(1, lsd_bitstream_tell(bstr));

    lsd_bitstream_read(bstr, 8);
    TEST_ASSERT_EQUAL_size_t(2, lsd_bitstream_tell(bstr));

    lsd_bitstream_destroy(bstr);
}

void test_bitstream_tell_initial(void) {
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));
    TEST_ASSERT_EQUAL_size_t(0, lsd_bitstream_tell(bstr));
    lsd_bitstream_destroy(bstr);
}

void test_bitstream_seek_and_read(void) {
    // C++ 测试：seek(0), read(4), seek(1), read(4) → 0xF
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    lsd_bitstream_seek(bstr, 0);
    lsd_bitstream_read(bstr, 4);
    lsd_bitstream_seek(bstr, 1);
    uint32_t val = lsd_bitstream_read(bstr, 4);
    TEST_ASSERT_EQUAL_UINT32(0xF, val);

    lsd_bitstream_destroy(bstr);
}

void test_bitstream_seek_resets_bit_pos(void) {
    // 读 3 bit（未对齐），然后 seek，应该从新位置的字节边界开始
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    lsd_bitstream_read(bstr, 3);     // 未对齐
    lsd_bitstream_seek(bstr, 0);     // seek 应重置位偏移
    uint32_t val = lsd_bitstream_read(bstr, 8);
    TEST_ASSERT_EQUAL_UINT32(0x13, val);  // 应从字节 0 开始读完整字节

    lsd_bitstream_destroy(bstr);
}

// ============================================================
// 字节读取 read_bytes
// ============================================================

void test_bitstream_read_bytes_basic(void) {
    // C++ 测试：seek(0), readSome(b, 2) → tell == 2
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    uint8_t out[2] = {0};
    size_t n = lsd_bitstream_read_bytes(bstr, out, 2);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_UINT8(0x13, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0xF0, out[1]);
    TEST_ASSERT_EQUAL_size_t(2, lsd_bitstream_tell(bstr));

    lsd_bitstream_destroy(bstr);
}

void test_bitstream_read_bytes_all(void) {
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    uint8_t out[6] = {0};
    size_t n = lsd_bitstream_read_bytes(bstr, out, 6);
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(test_buf, out, 6);

    lsd_bitstream_destroy(bstr);
}

void test_bitstream_read_bytes_past_end(void) {
    // 请求超出缓冲区范围，应只返回可用数据
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    lsd_bitstream_seek(bstr, 4);
    uint8_t out[4] = {0};
    size_t n = lsd_bitstream_read_bytes(bstr, out, 4);
    TEST_ASSERT_EQUAL_size_t(2, n);  // 只剩 2 字节
    TEST_ASSERT_EQUAL_UINT8(0x12, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x45, out[1]);

    lsd_bitstream_destroy(bstr);
}

void test_bitstream_read_bytes_after_bits(void) {
    // 先读 3 bit（未对齐），read_bytes 应先对齐再读
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    lsd_bitstream_read(bstr, 3);     // 未对齐，consume byte 0
    uint8_t out[1] = {0};
    size_t n = lsd_bitstream_read_bytes(bstr, out, 1);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT8(0xF0, out[0]);  // 应从 byte 1 开始读

    lsd_bitstream_destroy(bstr);
}

// ============================================================
// 字节对齐 align_to_byte
// ============================================================

void test_bitstream_align_when_unaligned(void) {
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    lsd_bitstream_read(bstr, 3);  // 3 bit consumed, bit_pos = 3
    lsd_bitstream_align_to_byte(bstr);
    // 对齐后应到下一个字节
    TEST_ASSERT_EQUAL_size_t(1, lsd_bitstream_tell(bstr));

    lsd_bitstream_destroy(bstr);
}

void test_bitstream_align_when_already_aligned(void) {
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    lsd_bitstream_read(bstr, 8);  // 完整一字节，已对齐
    lsd_bitstream_align_to_byte(bstr);
    TEST_ASSERT_EQUAL_size_t(1, lsd_bitstream_tell(bstr));  // 位置不变

    lsd_bitstream_destroy(bstr);
}

// ============================================================
// UTF-16 字符串读取
// ============================================================

void test_bitstream_read_utf16_string_ascii(void) {
    // "AB" in big-endian UTF-16: 0x00 0x41  0x00 0x42
    uint8_t buf[] = {0x00, 0x41, 0x00, 0x42};
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(buf, sizeof(buf));

    size_t out_len = 0;
    uint16_t *str = lsd_bitstream_read_utf16_string(bstr, 2, true, &out_len);
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_EQUAL_size_t(2, out_len);
    TEST_ASSERT_EQUAL_UINT16(0x0041, str[0]);  // 'A'
    TEST_ASSERT_EQUAL_UINT16(0x0042, str[1]);  // 'B'
    TEST_ASSERT_EQUAL_UINT16(0, str[2]);        // null terminator

    free(str);
    lsd_bitstream_destroy(bstr);
}

void test_bitstream_read_utf16_string_little_endian(void) {
    // "AB" in little-endian UTF-16: 0x41 0x00  0x42 0x00
    uint8_t buf[] = {0x41, 0x00, 0x42, 0x00};
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(buf, sizeof(buf));

    size_t out_len = 0;
    uint16_t *str = lsd_bitstream_read_utf16_string(bstr, 2, false, &out_len);
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_EQUAL_UINT16(0x0041, str[0]);  // 'A'
    TEST_ASSERT_EQUAL_UINT16(0x0042, str[1]);  // 'B'

    free(str);
    lsd_bitstream_destroy(bstr);
}

// ============================================================
// 辅助函数
// ============================================================

void test_bitstream_bit_length(void) {
    TEST_ASSERT_EQUAL_UINT8(1, lsd_bit_length(0));
    TEST_ASSERT_EQUAL_UINT8(1, lsd_bit_length(1));
    TEST_ASSERT_EQUAL_UINT8(2, lsd_bit_length(2));
    TEST_ASSERT_EQUAL_UINT8(2, lsd_bit_length(3));
    TEST_ASSERT_EQUAL_UINT8(3, lsd_bit_length(4));
    TEST_ASSERT_EQUAL_UINT8(3, lsd_bit_length(7));
    TEST_ASSERT_EQUAL_UINT8(8, lsd_bit_length(128));
    TEST_ASSERT_EQUAL_UINT8(8, lsd_bit_length(255));
}

void test_bitstream_reverse16(void) {
    TEST_ASSERT_EQUAL_UINT16(0x3412, lsd_reverse16(0x1234));
    TEST_ASSERT_EQUAL_UINT16(0x0000, lsd_reverse16(0x0000));
    TEST_ASSERT_EQUAL_UINT16(0xFF00, lsd_reverse16(0x00FF));
}

void test_bitstream_reverse32(void) {
    TEST_ASSERT_EQUAL_UINT32(0x78563412, lsd_reverse32(0x12345678));
    TEST_ASSERT_EQUAL_UINT32(0x00000000, lsd_reverse32(0x00000000));
    TEST_ASSERT_EQUAL_UINT32(0xFF000000, lsd_reverse32(0x000000FF));
}

// ============================================================
// 边界条件
// ============================================================

void test_bitstream_seek_beyond_end(void) {
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    // seek 超出缓冲区，后续读取应返回 0
    lsd_bitstream_seek(bstr, 100);
    uint32_t val = lsd_bitstream_read(bstr, 8);
    TEST_ASSERT_EQUAL_UINT32(0, val);

    lsd_bitstream_destroy(bstr);
}

void test_bitstream_read_past_end(void) {
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));

    lsd_bitstream_seek(bstr, 5);
    lsd_bitstream_read(bstr, 8);  // 读最后一个字节 0x45
    uint32_t val = lsd_bitstream_read(bstr, 8);  // 越界，应返回 0
    TEST_ASSERT_EQUAL_UINT32(0, val);

    lsd_bitstream_destroy(bstr);
}

void test_bitstream_read_bytes_null_params(void) {
    lsd_bitstream *bstr = lsd_bitstream_create_from_memory(test_buf, sizeof(test_buf));
    uint8_t out[1];

    TEST_ASSERT_EQUAL_size_t(0, lsd_bitstream_read_bytes(NULL, out, 1));
    TEST_ASSERT_EQUAL_size_t(0, lsd_bitstream_read_bytes(bstr, NULL, 1));
    TEST_ASSERT_EQUAL_size_t(0, lsd_bitstream_read_bytes(bstr, out, 0));

    lsd_bitstream_destroy(bstr);
}

// ============================================================
// Test Runner
// ============================================================

void run_bitstream_tests(void) {
    UnityBegin("test_bitstream.c");

    // 创建和销毁
    RUN_TEST(test_bitstream_create_null_buffer);
    RUN_TEST(test_bitstream_create_zero_size);
    RUN_TEST(test_bitstream_create_valid);
    RUN_TEST(test_bitstream_destroy_null);

    // 位级读取
    RUN_TEST(test_bitstream_read_bits_reconstruct_byte);
    RUN_TEST(test_bitstream_read_8bit);
    RUN_TEST(test_bitstream_read_32bit);
    RUN_TEST(test_bitstream_read_32bit_from_offset);
    RUN_TEST(test_bitstream_read_single_bit);
    RUN_TEST(test_bitstream_read_zero_bits);

    // seek / tell
    RUN_TEST(test_bitstream_tell_after_read_bits);
    RUN_TEST(test_bitstream_tell_initial);
    RUN_TEST(test_bitstream_seek_and_read);
    RUN_TEST(test_bitstream_seek_resets_bit_pos);

    // 字节读取
    RUN_TEST(test_bitstream_read_bytes_basic);
    RUN_TEST(test_bitstream_read_bytes_all);
    RUN_TEST(test_bitstream_read_bytes_past_end);
    RUN_TEST(test_bitstream_read_bytes_after_bits);

    // 字节对齐
    RUN_TEST(test_bitstream_align_when_unaligned);
    RUN_TEST(test_bitstream_align_when_already_aligned);

    // UTF-16 字符串
    RUN_TEST(test_bitstream_read_utf16_string_ascii);
    RUN_TEST(test_bitstream_read_utf16_string_little_endian);

    // 辅助函数
    RUN_TEST(test_bitstream_bit_length);
    RUN_TEST(test_bitstream_reverse16);
    RUN_TEST(test_bitstream_reverse32);

    // 边界条件
    RUN_TEST(test_bitstream_seek_beyond_end);
    RUN_TEST(test_bitstream_read_past_end);
    RUN_TEST(test_bitstream_read_bytes_null_params);

    UnityEnd();
}
