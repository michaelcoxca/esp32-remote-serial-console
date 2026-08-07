// ringbuf.c - Bulk-capable lock-free ring buffer

#include "ringbuf.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

// Must be power of 2
#define MIN_BUF_SIZE (4096)     // 4 KB (2^12)
#define MAX_BUF_SIZE (65536)    // 64 KB (2^16)

static const char* TAG = "ringbuf";


struct ringbuf_s {
    volatile ringbuf_size_t head;   // next write index
    volatile ringbuf_size_t tail;   // next read index
    ringbuf_size_t size_mask;       // size - 1
    uint8_t* buffer;
};

ringbuf_t ringbuf_create(void) {
    size_t max_allocatable_chunk = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    //floor max memory allocation target
    size_t max_malloc = max_allocatable_chunk / 2;
    max_malloc |= max_malloc >> 1;
    max_malloc |= max_malloc >> 2;
    max_malloc |= max_malloc >> 4;
    max_malloc |= max_malloc >> 8;
    max_malloc |= max_malloc >> 16;
    max_malloc = (max_malloc + 1) >> 1;

    if (max_malloc < MIN_BUF_SIZE) {
        ESP_LOGE(TAG, "Failed to create buffer, not enough memory.");
        return NULL;
    }

    if (max_malloc >= MAX_BUF_SIZE) {
        max_malloc = MAX_BUF_SIZE;
    } else {
        ESP_LOGW(TAG, "Low memory headroom: allocated %zu bytes (below MAX_BUF_SIZE target)", max_malloc);
    }
    
    ESP_LOGI(TAG, "RINGBUFFER MALLOC: %zu bytes", max_malloc);
    if (max_malloc == 0 || (max_malloc & (max_malloc - 1)) != 0) {
        return NULL;
    }

    ringbuf_t rb = (ringbuf_t)calloc(1, sizeof(struct ringbuf_s));
    if (!rb) return NULL;

    rb->buffer = (uint8_t*)calloc(max_malloc, 1);
    if (!rb->buffer) {
        free(rb);
        return NULL;
    }

    rb->size_mask = max_malloc - 1;
    return rb;
}

void ringbuf_destroy(ringbuf_t rb) {
    if (rb) {
        free(rb->buffer);
        free(rb);
    }
}

ringbuf_size_t ringbuf_put_bytes(ringbuf_t rb, const uint8_t* data, ringbuf_size_t len) {
    if (!rb || !data || len == 0) return 0;

    ringbuf_size_t head = rb->head;
    ringbuf_size_t tail = rb->tail;
    ringbuf_size_t capacity = rb->size_mask + 1;
    ringbuf_size_t space = (tail > head) ? (tail - head - 1) : ((capacity - head) + tail - 1);


    // If not enough space, we will overwrite oldest
    if (len > capacity - 1) {
        // Truncate to max possible (we can never store full capacity due to empty/full ambiguity)
        len = capacity - 1;
    }

    // If buffer would overflow, advance tail to make room
    if (len > space) {
        ringbuf_size_t drop = len - space;
        tail = (tail + drop) & rb->size_mask;
        rb->tail = tail; // commit tail advance
    }

    // Write data in 1 or 2 chunks (handle wrap-around)
    ringbuf_size_t write_end = (head + len) & rb->size_mask;
    if (write_end <= head) {
        // Wraps around
        ringbuf_size_t chunk1 = capacity - head;
        memcpy(&rb->buffer[head], data, chunk1);
        memcpy(rb->buffer, data + chunk1, len - chunk1);
    } else {
        // Contiguous
        memcpy(&rb->buffer[head], data, len);
    }

    rb->head = (head + len) & rb->size_mask;
    return len;
}

ringbuf_size_t ringbuf_get_bytes(ringbuf_t rb, uint8_t* buf, ringbuf_size_t len) {
    if (!rb || !buf || len == 0) return 0;

    ringbuf_size_t head = rb->head;
    ringbuf_size_t tail = rb->tail;

    if (head == tail) {
        return 0; // empty
    }

    ringbuf_size_t available = (head >= tail) ? (head - tail) : (rb->size_mask + 1 - (tail - head));
    if (len > available) {
        len = available;
    }

    // Read in 1 or 2 chunks
    ringbuf_size_t read_end = (tail + len) & rb->size_mask;
    if (read_end <= tail) {
        // Wraps around
        ringbuf_size_t chunk1 = rb->size_mask + 1 - tail;
        memcpy(buf, &rb->buffer[tail], chunk1);
        memcpy(buf + chunk1, rb->buffer, len - chunk1);
    } else {
        // Contiguous
        memcpy(buf, &rb->buffer[tail], len);
    }

    rb->tail = (tail + len) & rb->size_mask;
    return len;
}

ringbuf_size_t ringbuf_count(ringbuf_t rb) {
    if (!rb) return 0;
    ringbuf_size_t head = rb->head;
    ringbuf_size_t tail = rb->tail;
    return (head >= tail) ? (head - tail) : (rb->size_mask + 1 - (tail - head));
}

ringbuf_size_t ringbuf_capacity(ringbuf_t rb) {
    return rb ? (rb->size_mask + 1) : 0;
}