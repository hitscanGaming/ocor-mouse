#include "ring_buffer.h"
#include <string.h>
#include "debug.h"
#include <stddef.h>

volatile RingBuffer packetVal;

void RB_Init(volatile RingBuffer *rb) {
    rb->head = 0;
    rb->tail = 0;
}

int RB_IsEmpty(volatile RingBuffer *rb) {
    return (rb->head == rb->tail);
}

int RB_IsFull(volatile RingBuffer *rb) {
    return ((rb->head + 1) % RING_BUFFER_SIZE) == rb->tail;
}

void RB_Push(volatile RingBuffer *rb, volatile uint8_t *data) {
    if (!RB_IsFull(rb)) {
        for(int i = 0; i < MOUSE_REPORT_SIZE; i++) {
            rb->buffer[rb->head][i] = data[i];
        }
        rb->head = (rb->head + 1) % RING_BUFFER_SIZE;
    } else {
        printf("full \r\n");
    }
}

volatile uint8_t* RB_Pop(volatile RingBuffer *rb) {
    volatile uint8_t *p = NULL;
    if (!RB_IsEmpty(rb)) {
        p = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
    }
    return p;
}

