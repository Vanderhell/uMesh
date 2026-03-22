#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/common/ring.h"

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

static void test_ring_init(void)
{
    uint8_t buf[8];
    ring_t r;
    ring_init(&r, buf, 8);
    TEST_ASSERT(ring_is_empty(&r), "init: buffer is empty");
    TEST_ASSERT(!ring_is_full(&r), "init: buffer not full");
    TEST_ASSERT(ring_count(&r) == 0, "init: count == 0");
}

static void test_ring_push_pop(void)
{
    uint8_t buf[8];
    ring_t r;
    uint8_t val = 0;

    ring_init(&r, buf, 8);

    TEST_ASSERT(ring_push(&r, 0xAB), "push: first byte");
    TEST_ASSERT(ring_push(&r, 0xCD), "push: second byte");
    TEST_ASSERT(!ring_is_empty(&r), "push: not empty after push");
    TEST_ASSERT(ring_count(&r) == 2, "push: count == 2");

    TEST_ASSERT(ring_pop(&r, &val), "pop: first pop succeeds");
    TEST_ASSERT(val == 0xAB, "pop: first value correct");

    TEST_ASSERT(ring_pop(&r, &val), "pop: second pop succeeds");
    TEST_ASSERT(val == 0xCD, "pop: second value correct");

    TEST_ASSERT(ring_is_empty(&r), "pop: empty after all pops");
}

static void test_ring_overflow(void)
{
    /* capacity 4 → max 3 usable slots */
    uint8_t buf[4];
    ring_t r;
    ring_init(&r, buf, 4);

    TEST_ASSERT(ring_push(&r, 1), "overflow: push 1");
    TEST_ASSERT(ring_push(&r, 2), "overflow: push 2");
    TEST_ASSERT(ring_push(&r, 3), "overflow: push 3");
    TEST_ASSERT(ring_is_full(&r), "overflow: full after 3 pushes");
    TEST_ASSERT(!ring_push(&r, 4), "overflow: 4th push fails (full)");
    TEST_ASSERT(ring_count(&r) == 3, "overflow: count still 3");
}

static void test_ring_underflow(void)
{
    uint8_t buf[4];
    ring_t r;
    uint8_t val = 0;

    ring_init(&r, buf, 4);
    TEST_ASSERT(!ring_pop(&r, &val), "underflow: pop on empty fails");
}

static void test_ring_wrap_around(void)
{
    /* Push/pop several times to force index wrap-around */
    uint8_t buf[4];
    ring_t r;
    uint8_t val = 0;
    int i;

    ring_init(&r, buf, 4);

    for (i = 0; i < 10; i++) {
        uint8_t pushed = (uint8_t)(i & 0xFF);
        TEST_ASSERT(ring_push(&r, pushed), "wrap: push");
        TEST_ASSERT(ring_pop(&r, &val), "wrap: pop");
        TEST_ASSERT(val == pushed, "wrap: value matches");
    }
    TEST_ASSERT(ring_is_empty(&r), "wrap: empty at end");
}

static void test_ring_fifo_order(void)
{
    uint8_t buf[8];
    ring_t r;
    uint8_t val = 0;
    int i;

    ring_init(&r, buf, 8);
    for (i = 0; i < 7; i++) {
        ring_push(&r, (uint8_t)i);
    }
    for (i = 0; i < 7; i++) {
        ring_pop(&r, &val);
        TEST_ASSERT(val == (uint8_t)i, "fifo: order preserved");
    }
}

int main(void)
{
    printf("=== test_ring ===\n");
    test_ring_init();
    test_ring_push_pop();
    test_ring_overflow();
    test_ring_underflow();
    test_ring_wrap_around();
    test_ring_fifo_order();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
