#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/common/crc.h"

/* Simple assert framework */
static int s_pass = 0;
static int s_fail = 0;

#define TEST_ASSERT(cond, name) \
    do { \
        if (cond) { \
            printf("  PASS: %s\n", name); \
            s_pass++; \
        } else { \
            printf("  FAIL: %s  (line %d)\n", name, __LINE__); \
            s_fail++; \
        } \
    } while (0)

static void test_crc16_known_vector(void)
{
    /* "123456789" -> 0x29B1 */
    const uint8_t data[] = "123456789";
    uint16_t result = crc16(data, 9);
    TEST_ASSERT(result == 0x29B1, "CRC16 known vector '123456789' == 0x29B1");
}

static void test_crc16_empty(void)
{
    uint16_t result = crc16(NULL, 0);
    /* CRC of empty input = init value 0xFFFF */
    TEST_ASSERT(result == 0xFFFF, "CRC16 empty input == 0xFFFF");
}

static void test_crc16_single_byte(void)
{
    const uint8_t data[] = { 0x00 };
    uint16_t r1 = crc16(data, 1);
    /* Two runs on same data must match */
    uint16_t r2 = crc16(data, 1);
    TEST_ASSERT(r1 == r2, "CRC16 deterministic on same input");
}

static void test_crc16_different_data(void)
{
    const uint8_t a[] = { 0x01, 0x02, 0x03 };
    const uint8_t b[] = { 0x01, 0x02, 0x04 };
    uint16_t ra = crc16(a, 3);
    uint16_t rb = crc16(b, 3);
    TEST_ASSERT(ra != rb, "CRC16 different data gives different CRC");
}

int main(void)
{
    printf("=== test_crc ===\n");
    test_crc16_known_vector();
    test_crc16_empty();
    test_crc16_single_byte();
    test_crc16_different_data();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
