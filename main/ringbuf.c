// ringbuf.c - Bulk-capable lock-free ring buffer

#include "ringbuf.h"
#include <stdlib.h>
#include <string.h>

struct ringbuf_s {
    volatile uint32_t head;   // next write index
    volatile uint32_t tail;   // next read index
    uint32_t size_mask;       // size - 1
    uint8_t* buffer;
};

ringbuf_t ringbuf_create(uint32_t size) {
    if (size == 0 || (size & (size - 1)) != 0) {
        return NULL;
    }

    ringbuf_t rb = (ringbuf_t)calloc(1, sizeof(struct ringbuf_s));
    if (!rb) return NULL;

    rb->buffer = (uint8_t*)calloc(size, 1);
    if (!rb->buffer) {
        free(rb);
        return NULL;
    }

    rb->size_mask = size - 1;
    return rb;
}

void ringbuf_destroy(ringbuf_t rb) {
    if (rb) {
        free(rb->buffer);
        free(rb);
    }
}

size_t ringbuf_put_bytes(ringbuf_t rb, const uint8_t* data, size_t len) {
    if (!rb || !data || len == 0) return 0;

    uint32_t head = rb->head;
    uint32_t tail = rb->tail;
    uint32_t capacity = rb->size_mask + 1;
    uint32_t space = (tail > head) ? (tail - head - 1) : (capacity - (head - tail) - 1);

    // If not enough space, we will overwrite oldest
    if (len > capacity - 1) {
        // Truncate to max possible (we can never store full capacity due to empty/full ambiguity)
        len = capacity - 1;
    }

    // If buffer would overflow, advance tail to make room
    if (len > space) {
        uint32_t drop = len - space;
        tail = (tail + drop) & rb->size_mask;
        rb->tail = tail; // commit tail advance
    }

    // Write data in 1 or 2 chunks (handle wrap-around)
    uint32_t write_end = (head + len) & rb->size_mask;
    if (write_end <= head) {
        // Wraps around
        uint32_t chunk1 = capacity - head;
        memcpy(&rb->buffer[head], data, chunk1);
        memcpy(rb->buffer, data + chunk1, len - chunk1);
    } else {
        // Contiguous
        memcpy(&rb->buffer[head], data, len);
    }

    rb->head = (head + len) & rb->size_mask;
    return len;
}

size_t ringbuf_get_bytes(ringbuf_t rb, uint8_t* buf, size_t len) {
    if (!rb || !buf || len == 0) return 0;

    uint32_t head = rb->head;
    uint32_t tail = rb->tail;

    if (head == tail) {
        return 0; // empty
    }

    uint32_t available = (head >= tail) ? (head - tail) : (rb->size_mask + 1 - (tail - head));
    if (len > available) {
        len = available;
    }

    // Read in 1 or 2 chunks
    uint32_t read_end = (tail + len) & rb->size_mask;
    if (read_end <= tail) {
        // Wraps around
        uint32_t chunk1 = rb->size_mask + 1 - tail;
        memcpy(buf, &rb->buffer[tail], chunk1);
        memcpy(buf + chunk1, rb->buffer, len - chunk1);
    } else {
        // Contiguous
        memcpy(buf, &rb->buffer[tail], len);
    }

    rb->tail = (tail + len) & rb->size_mask;
    return len;
}

uint32_t ringbuf_count(ringbuf_t rb) {
    if (!rb) return 0;
    uint32_t head = rb->head;
    uint32_t tail = rb->tail;
    return (head >= tail) ? (head - tail) : (rb->size_mask + 1 - (tail - head));
}

uint32_t ringbuf_capacity(ringbuf_t rb) {
    return rb ? (rb->size_mask + 1) : 0;
}