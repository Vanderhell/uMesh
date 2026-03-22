#include "ring.h"

/*
 * ISR-safe SPSC ring buffer.
 * Uses power-of-two capacity is NOT required — works with any capacity.
 * head = write index, tail = read index.
 * Empty: head == tail
 * Full:  (head + 1) % capacity == tail
 * Therefore max usable slots = capacity - 1.
 */

void ring_init(ring_t *r, uint8_t *buf, uint16_t capacity)
{
    r->buf      = buf;
    r->capacity = capacity;
    r->head     = 0;
    r->tail     = 0;
}

bool ring_push(ring_t *r, uint8_t byte)
{
    uint16_t next_head = (uint16_t)((r->head + 1u) % r->capacity);
    if (next_head == r->tail) {
        return false; /* full */
    }
    r->buf[r->head] = byte;
    r->head = next_head;
    return true;
}

bool ring_pop(ring_t *r, uint8_t *byte)
{
    if (r->head == r->tail) {
        return false; /* empty */
    }
    *byte = r->buf[r->tail];
    r->tail = (uint16_t)((r->tail + 1u) % r->capacity);
    return true;
}

bool ring_is_empty(const ring_t *r)
{
    return r->head == r->tail;
}

bool ring_is_full(const ring_t *r)
{
    return ((r->head + 1u) % r->capacity) == r->tail;
}

uint16_t ring_count(const ring_t *r)
{
    if (r->head >= r->tail) {
        return (uint16_t)(r->head - r->tail);
    }
    return (uint16_t)(r->capacity - r->tail + r->head);
}
