// ringbuf.h - Lock-free ring buffer with bulk operations
// Single producer / single consumer only.

#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct ringbuf_s* ringbuf_t;

// Create ring buffer. Size must be power of two.
ringbuf_t ringbuf_create(uint32_t size);

// Destroy buffer
void ringbuf_destroy(ringbuf_t rb);

// Put up to 'len' bytes. Overwrites oldest data if needed.
// Returns number of bytes actually written (<= len).
size_t ringbuf_put_bytes(ringbuf_t rb, const uint8_t* data, size_t len);

// Get up to 'len' bytes.
// Returns number of bytes actually read (<= len).
size_t ringbuf_get_bytes(ringbuf_t rb, uint8_t* buf, size_t len);

// Get current fill level
uint32_t ringbuf_count(ringbuf_t rb);

// Get total capacity
uint32_t ringbuf_capacity(ringbuf_t rb);

#endif // RINGBUF_H