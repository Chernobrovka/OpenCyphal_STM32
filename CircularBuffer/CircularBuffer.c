/*
 * CircularBuffer.c
 *
 *  Created on: Oct 31, 2024
 *      Author: goman_dp
 */


#if defined STM32G474xx
#include "stm32g4xx_hal.h"
#endif
#include "fdcan.h"

#define BUFFER_SIZE 16

typedef struct {
    FDCAN_RxHeaderTypeDef header;
    uint8_t data[64];
} RxBufferItem;

RxBufferItem rxBuffer[BUFFER_SIZE];
uint8_t rxBufferHead = 0;
uint8_t rxBufferTail = 0;


static inline void enterCriticalSection(void);
static inline void exitCriticalSection(void);

uint8_t addToRxBuffer(FDCAN_RxHeaderTypeDef *header, uint8_t *data) {
    enterCriticalSection();
    uint8_t next = (rxBufferHead + 1) % BUFFER_SIZE;
    if (next != rxBufferTail) {
        rxBuffer[rxBufferHead].header = *header;
        memcpy(rxBuffer[rxBufferHead].data, data, 64);
        rxBufferHead = next;
        exitCriticalSection();
        return 1; // ok
    }
    exitCriticalSection();
    return 0; // fail
}

uint8_t getFromRxBuffer(FDCAN_RxHeaderTypeDef *header, uint8_t *data) {
    enterCriticalSection();
    if (rxBufferTail != rxBufferHead) {
        *header = rxBuffer[rxBufferTail].header;
        memcpy(data, rxBuffer[rxBufferTail].data, 64);
        rxBufferTail = (rxBufferTail + 1) % BUFFER_SIZE;
        exitCriticalSection();
        return 1;
    }
    exitCriticalSection();
    return 0;
}

static inline void enterCriticalSection(void) {
    __disable_irq();
}

static inline void exitCriticalSection(void) {
    __enable_irq();
}
