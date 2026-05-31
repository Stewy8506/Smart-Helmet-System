#include "ring_buffer.h"

void RingBuffer_Init(RingBuffer_t *rb) {
    if (rb == NULL) return;
    rb->head = 0;
    rb->tail = 0;
}

uint32_t RingBuffer_GetCount(RingBuffer_t *rb) {
    if (rb == NULL) return 0;
    uint32_t h = rb->head;
    uint32_t t = rb->tail;
    if (h >= t) {
        return h - t;
    } else {
        return RING_BUFFER_SIZE - t + h;
    }
}

uint32_t RingBuffer_GetFreeSpace(RingBuffer_t *rb) {
    if (rb == NULL) return 0;
    // Capacity is RING_BUFFER_SIZE - 1 to distinguish full from empty
    return (RING_BUFFER_SIZE - 1) - RingBuffer_GetCount(rb);
}

bool RingBuffer_Write(RingBuffer_t *rb, const int16_t *data, uint32_t length) {
    if (rb == NULL || data == NULL || length == 0) return false;
    
    if (RingBuffer_GetFreeSpace(rb) < length) {
        return false; // Buffer overflow
    }
    
    for (uint32_t i = 0; i < length; i++) {
        rb->buffer[rb->head] = data[i];
        rb->head = (rb->head + 1) % RING_BUFFER_SIZE;
    }
    
    return true;
}

bool RingBuffer_Read(RingBuffer_t *rb, int16_t *data, uint32_t length) {
    if (rb == NULL || data == NULL || length == 0) return false;
    
    if (RingBuffer_GetCount(rb) < length) {
        return false; // Buffer underflow
    }
    
    for (uint32_t i = 0; i < length; i++) {
        data[i] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
    }
    
    return true;
}

void RingBuffer_Clear(RingBuffer_t *rb) {
    if (rb == NULL) return;
    rb->head = 0;
    rb->tail = 0;
}
