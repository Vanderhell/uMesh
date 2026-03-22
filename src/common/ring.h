#ifndef UMESH_RING_H
#define UMESH_RING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * ISR-safe SPSC (Single-Producer Single-Consumer) ring buffer.
 * Buffer storage is provided by the caller — no dynamic allocation.
 */
typedef struct {
    uint8_t  *buf;
    uint16_t  capacity;
    volatile uint16_t head; /* write index */
    volatile uint16_t tail; /* read index  */
} ring_t;

void ring_init(ring_t *r, uint8_t *buf, uint16_t capacity);
bool ring_push(ring_t *r, uint8_t byte);
bool ring_pop(ring_t *r, uint8_t *byte);
bool ring_is_empty(const ring_t *r);
bool ring_is_full(const ring_t *r);
uint16_t ring_count(const ring_t *r);

#endif /* UMESH_RING_H */
