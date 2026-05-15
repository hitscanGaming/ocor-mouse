#ifndef __RING_BUFFER_H
#define __RING_BUFFER_H

#include "stdint.h"

/* Ring Buffer Implementation */
#define RING_BUFFER_SIZE 16 // Increased size
#define MOUSE_REPORT_SIZE 7

typedef struct {
    uint8_t buffer[RING_BUFFER_SIZE][MOUSE_REPORT_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
} RingBuffer;

extern volatile RingBuffer packetVal;

extern void RB_Init(volatile RingBuffer *rb);

extern int RB_IsEmpty(volatile RingBuffer *rb);

extern int RB_IsFull(volatile RingBuffer *rb);

extern void RB_Push(volatile RingBuffer *rb, volatile uint8_t *data);

extern volatile uint8_t* RB_Pop(volatile RingBuffer *rb);

#endif