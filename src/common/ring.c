#include "ring.h"

/* Stub — will be fully implemented in Step 4 */
void ring_init(ring_t *r, uint8_t *buf, uint16_t capacity)
{
    r->buf      = buf;
    r->capacity = capacity;
    r->head     = 0;
    r->tail     = 0;
}

bool ring_push(ring_t *r, uint8_t byte)
{
    (void)r;
    (void)byte;
    return false;
}

bool ring_pop(ring_t *r, uint8_t *byte)
{
    (void)r;
    (void)byte;
    return false;
}

bool ring_is_empty(const ring_t *r)
{
    return r->head == r->tail;
}

bool ring_is_full(const ring_t *r)
{
    return ((r->head + 1) % r->capacity) == r->tail;
}

uint16_t ring_count(const ring_t *r)
{
    if (r->head >= r->tail) {
        return r->head - r->tail;
    }
    return r->capacity - r->tail + r->head;
}
