/*
 * CircularBuffer.h
 *
 *  Created on: Oct 31, 2024
 *      Author: goman_dp
 */

#ifndef CIRCULARBUFFER_CIRCULARBUFFER_H_
#define CIRCULARBUFFER_CIRCULARBUFFER_H_

#include "fdcan.h"

uint8_t addToRxBuffer(FDCAN_RxHeaderTypeDef *header, uint8_t *data);
uint8_t getFromRxBuffer(FDCAN_RxHeaderTypeDef *header, uint8_t *data);

#endif /* CIRCULARBUFFER_CIRCULARBUFFER_H_ */
