/*
 * UavCan.h
 *
 *  Created on: October 21, 2024
 *      Author: goman_dp
 */

#ifndef INC_UAVCAN_H_
#define INC_UAVCAN_H_

#include "o1heap/o1heap.h"
#include "libcanard/canard.h"
#include "nunavut/support/serialization.h"
#include "tim.h"

/* -----------------------------------------------------------------------------------------------------*/
/* USER MCU DEFINED */
#define DESTANATION_NODE_ID 42

#define STANDART_PORT_ID 44

#define CURRENT_PORT_ID STANDART_PORT_ID
/* USER MCU DEFINES END */
/* -----------------------------------------------------------------------------------------------------*/

/**
 * @brief Initializes the UAVCAN application.
 *
 * This function initializes the UAVCAN application by setting up the CANard instance,
 * configuring the CAN filters, and starting the CAN interface.
 *
 * @return uint8_t Returns 1 if initialization is successful, 0 otherwise.
 */
uint8_t uavcanAppInit(TIM_HandleTypeDef *htim);

/**
 * @brief Processes the UAVCAN application.
 *
 * This function processes the UAVCAN application by handling received messages
 * and sending heartbeat messages.
 */
void uavcanAppProcess(void);


/* -----------------------------------------------------------------------------------------------------*/
/* USER CODE MESSAGES TRANSMIT */

void sendHeartbeatMessage(void);

void sendReal32ArrayMessage(void);

void sendExecuteCommandRequest(void);

void sendGyroCoefsMessage(void);
/* USER CODE MESSAGES TRANSMIT END */
/* -----------------------------------------------------------------------------------------------------*/

#endif /*INC_UAVCAN_H_*/
