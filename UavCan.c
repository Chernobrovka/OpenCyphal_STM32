/*
 * UavCan.c
 *
 *  Created on: October 21, 2024
 *      Author: goman_dp
 */
#include "UavCan.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "fdcan.h"
#include "usart.h"
#include "tim.h"
#include "gpio.h"

#include "o1heap/o1heap.h"
#include "libcanard/canard.h"
#include "nunavut/support/serialization.h"
#include "CircularBuffer/CircularBuffer.h"
/* -----------------------------------------------------------------------------------------------------*/
/* USER CODE INCLUDES */
#include "uavcan/node/Heartbeat_1_0.h"
#include "uavcan/node/ExecuteCommand_1_1.h"
#include "uavcan/primitive/array/Real32_1_0.h"
#include "uavcan/node/GyroCoefs_1_0.h"
/* USER CODE END INCLUDES */
/* -----------------------------------------------------------------------------------------------------*/
/* USER MCU INCLUDE */
#if defined STM32G474xx
#include "stm32g4xx_hal.h"
#endif
/* USER MCU INCLUDE END */
/* -----------------------------------------------------------------------------------------------------*/

#define HEAP_SIZE 1024
uint64_t tx_deadline_usec = 0;

O1HeapInstance* my_allocator;
CanardInstance canard;
CanardTxQueue 	queue;

TIM_HandleTypeDef *uavcan_htim;

/* -----------------------------------------------------------------------------------------------------*/
/* USER CODE SUBSCRIBTIONS PROTOTYPES */
static CanardRxSubscription heartbeat_subscription;
static CanardRxSubscription rx_message_subscription;
static CanardRxSubscription command_subscription;
static CanardRxSubscription gyro_coeffs_subscription;
/* USER CODE SUBSCRIBTIONS PROTOTYPES END */
/* -----------------------------------------------------------------------------------------------------*/

// buffer for serialization of heartbeat message
size_t hbeat_ser_buf_size = uavcan_node_Heartbeat_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_;
uint8_t hbeat_ser_buf[uavcan_node_Heartbeat_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_] = {0};

uint8_t my_message_transfer_id = 0;

/* -----------------------------------------------------------------------------------------------------*/
// ----------------- Static functions definition -----------------------------
static void processRxBuffer(void);
static void canFdConfigFilter(void);
static void uavCanRxSubscribe(void);
static void processCanardTxQueue(void);
// -------------------- TX messages functions ------------------------------
static void rxMessageCallback(CanardRxTransfer *transfer);
static void executeCommandCallback(CanardRxTransfer *transfer);
static void rxGyroCoefsCallback(CanardRxTransfer *transfer);
// ----------------- Service functions definition -----------------------------
static inline uint8_t allocateHeapMemory(void);
static inline void*   memAllocate(CanardInstance* const canard, const size_t amount);
static inline void    memFree(CanardInstance* const canard, void* const pointer);
static uint8_t  	  LengthDecoder(uint32_t length);
static uint32_t 	  LengthCoder(uint8_t length);
static void 		  startMicrosecondsTimer(void);
static uint32_t 	  getCurrentMicroseconds(void);
static void 		  processReceivedTransfer(uint8_t redundant_interface_index, CanardRxTransfer* transfer);
/* -----------------------------------------------------------------------------------------------------*/

/* -----------------------------------------------------------------------------------------------------*/
/* USER CODE BUISNESS LOGIC */
uint8_t MCU_restart = 0;
/* USER CODE BUISNESS LOGIC END */
/* -----------------------------------------------------------------------------------------------------*/

/* -----------------------------------------------------------------------------------------------------*/
/* --------------------------------- Global functions --------------------------------------------------*/

/**
 * @brief Initializes the UAVCAN application.
 *
 * This function initializes the UAVCAN application by setting up the CANard instance,
 * configuring the CAN filters, and starting the CAN interface.
 *
 * @return uint8_t Returns 1 if initialization is successful, 0 otherwise.
 */
uint8_t uavcanAppInit(TIM_HandleTypeDef *htim){
	uavcan_htim = htim;
	startMicrosecondsTimer(); // start counting timer

	canard = canardInit(&memAllocate, &memFree);
	canard.node_id = CURRENT_PORT_ID;
	queue = canardTxInit(100, CANARD_MTU_CAN_FD);

	uavCanRxSubscribe();

	if (!allocateHeapMemory())
		return 0;

	HAL_FDCAN_Start(&hfdcan1);
	if (HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK) {
		Error_Handler();
	}
	canFdConfigFilter();

	return 1; // program normal
}

/**
 * @brief Processes the UAVCAN application.
 *
 * This function processes the UAVCAN application by handling received messages
 * and sending heartbeat messages.
 */
void uavcanAppProcess(void){
	// Handle received messages
	processRxBuffer();

	sendHeartbeatMessage();
	/* -----------------------------------------------------------------------------------------------------*/
	/* USER CODE MESSAGES CALL */


	/* USER CODE MESSAGES CALL END */
	/* -----------------------------------------------------------------------------------------------------*/

    uint32_t timestamp = HAL_GetTick();
    while(HAL_GetTick() < timestamp + 1000u)
    	processCanardTxQueue();
}

/**
 * @brief Callback function for FDCAN Rx FIFO 0.
 *
 * This function is called when a new message is received in the FDCAN Rx FIFO 0.
 * It processes the received message and adds it to the Rx buffer.
 *
 * @param hfdcan Pointer to the FDCAN handle.
 * @param RxFifo0ITs Interrupt flags for Rx FIFO 0.
 */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs) {
	if((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != RESET) {
		FDCAN_RxHeaderTypeDef RxHeader = {0};
		uint8_t RxData[64] = {0};

		if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &RxHeader, RxData) != HAL_OK)
		  Error_Handler();

        if (!addToRxBuffer(&RxHeader, RxData)) {
            Error_Handler(); // Обработка ошибки: буфер переполнен
        }
	}
}

/**
 * @brief Processes the Rx buffer.
 *
 * This function processes the Rx buffer by extracting messages and handling them
 * using the Canard library.
 */
static void processRxBuffer(void){
    FDCAN_RxHeaderTypeDef header;
    uint8_t data[64];

    while (getFromRxBuffer(&header, data)) {
        CanardFrame rxf;
        rxf.extended_can_id = (uint32_t) header.Identifier;
        rxf.payload_size = (size_t) LengthDecoder(header.DataLength);
        rxf.payload = (void*) data;

        CanardRxTransfer transfer;
        uint64_t rx_timestamp_usec = getCurrentMicroseconds();
        int8_t result = canardRxAccept(&canard, rx_timestamp_usec, &rxf, 0, &transfer, NULL);
        if (result < 0) {
            // Error
        } else if (result == 1) {
            processReceivedTransfer(0, &transfer);              // A transfer has been received, process it.
            canard.memory_free(&canard, transfer.payload);      // Deallocate the dynamic memory afterwards.
        }
    }
}

// ------------------- Static functions ----------------------------------

/**
 * @brief Processes the Rx buffer.
 *
 * This function processes the Rx buffer by extracting messages and handling them
 * using the Canard library.
 */
static void canFdConfigFilter(void){
	FDCAN_FilterTypeDef sFilterConfig;
	sFilterConfig.IdType = FDCAN_STANDARD_ID;
	sFilterConfig.FilterIndex = 0;
	sFilterConfig.FilterType = FDCAN_FILTER_MASK;
	sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
	sFilterConfig.FilterID1 = 0x11;  // USER FILTER ID
	sFilterConfig.FilterID2 = 0x11;  // USER FILTER ID
	//sFilterConfig.RxBufferIndex = 0;
	if (HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig) != HAL_OK) {
		/* Filter configuration Error */
		Error_Handler();
	}
}

/**
 * @brief Subscribes to UAVCAN Rx messages.
 *
 * This function subscribes to various UAVCAN messages and commands.
 */
static void uavCanRxSubscribe(void){
/* -----------------------------------------------------------------------------------------------------*/
/* USER CODE INCLUDES */
	if( canardRxSubscribe(      &canard,
								CanardTransferKindMessage,
								4,	// USER DEFINED PORT for this message type
								uavcan_primitive_array_Real32_1_0_EXTENT_BYTES_,
								CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
								&rx_message_subscription) != 1 ){ Error_Handler(); }

	if( canardRxSubscribe(        &canard,
								CanardTransferKindRequest,
								uavcan_node_ExecuteCommand_1_1_FIXED_PORT_ID_,
								uavcan_node_ExecuteCommand_Request_1_1_EXTENT_BYTES_,
								CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
								&command_subscription) != 1 ){ Error_Handler(); }

	if( canardRxSubscribe(      &canard,
								CanardTransferKindMessage,
								58, // USER DEFINED PORT for this message type
								uavcan_GyroCoefs_1_0_EXTENT_BYTES_,
								CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
								&gyro_coeffs_subscription) != 1 ){ Error_Handler(); }
/* USER CODE END INCLUDES */
/* -----------------------------------------------------------------------------------------------------*/
}

/**
 * @brief Processes the Canard Tx queue.
 *
 * This function processes the Canard Tx queue by sending messages over the CAN interface.
 */
static void processCanardTxQueue(void){
	for (const CanardTxQueueItem* ti = NULL; (ti = canardTxPeek(&queue)) != NULL;){  // Peek at the top of the queue.
		if ((0U == ti->tx_deadline_usec) || (ti->tx_deadline_usec > getCurrentMicroseconds())){  // Check the deadline.
			FDCAN_TxHeaderTypeDef fdcan1TxHeader;

		    fdcan1TxHeader.Identifier = ti->frame.extended_can_id;
		    fdcan1TxHeader.IdType = (ti->frame.extended_can_id & 0x1FFFFFFF) > 0x7FF ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
		    fdcan1TxHeader.TxFrameType = FDCAN_DATA_FRAME;
		    fdcan1TxHeader.DataLength = LengthCoder(ti->frame.payload_size);
		    fdcan1TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
		    fdcan1TxHeader.BitRateSwitch = FDCAN_BRS_ON;
		    fdcan1TxHeader.FDFormat = FDCAN_FD_CAN;
		    fdcan1TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
		    fdcan1TxHeader.MessageMarker = 0;

		    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &fdcan1TxHeader, (uint8_t *)ti->frame.payload) != HAL_OK)
		    {
		        Error_Handler();
		    }
	    }
	    // After the frame is transmitted or if it has timed out while waiting, pop it from the queue and deallocate:
	    canard.memory_free(&canard, canardTxPop(&queue, ti));
	}
}

// ------------------- Service functions ------------------------------------

/**
 * @brief Allocates heap memory for the UAVCAN application.
 *
 * This function allocates heap memory for the UAVCAN application using the O1Heap allocator.
 *
 * @return uint8_t Returns 1 if memory allocation is successful, 0 otherwise.
 */
static inline uint8_t allocateHeapMemory(void){
	void* heap_memory = malloc(HEAP_SIZE);
	if (heap_memory == NULL)
		return 0;

	uintptr_t aligned_address = (uintptr_t)heap_memory;
	if (aligned_address % O1HEAP_ALIGNMENT != 0)
	    aligned_address += O1HEAP_ALIGNMENT - (aligned_address % O1HEAP_ALIGNMENT);

	heap_memory = (void*)aligned_address;

	my_allocator = o1heapInit(heap_memory, HEAP_SIZE);
	return 1;
}

/**
 * @brief Allocates memory using the O1Heap allocator.
 *
 * This function allocates memory using the O1Heap allocator for the Canard instance.
 *
 * @param canard Pointer to the Canard instance.
 * @param amount Amount of memory to allocate.
 * @return void* Pointer to the allocated memory.
 */
static inline void* memAllocate(CanardInstance* const canard, const size_t amount){
    (void) canard;
    return o1heapAllocate(my_allocator, amount);
}

/**
 * @brief Frees memory using the O1Heap allocator.
 *
 * This function frees memory using the O1Heap allocator for the Canard instance.
 *
 * @param canard Pointer to the Canard instance.
 * @param pointer Pointer to the memory to be freed.
 */
static inline void memFree(CanardInstance* const canard, void* const pointer){
    (void) canard;
    o1heapFree(my_allocator, pointer);
}

/*
  * @brief Decodes FDCAN_data_length_code into the decimal length of FDCAN message
  * @param[in]          length           FDCAN_data_length_code
  * @retval             uint8_t         Decimal message length (bytes)
*/
static uint8_t LengthDecoder( uint32_t length )
{
  switch( length )
  {
    case FDCAN_DLC_BYTES_0:     return 0;
    case FDCAN_DLC_BYTES_1:     return 1;
    case FDCAN_DLC_BYTES_2:     return 2;
    case FDCAN_DLC_BYTES_3:     return 3;
    case FDCAN_DLC_BYTES_4:     return 4;
    case FDCAN_DLC_BYTES_5:     return 5;
    case FDCAN_DLC_BYTES_6:     return 6;
    case FDCAN_DLC_BYTES_7:     return 7;
    case FDCAN_DLC_BYTES_8:     return 8;
    case FDCAN_DLC_BYTES_12:    return 12;
    case FDCAN_DLC_BYTES_16:    return 16;
    case FDCAN_DLC_BYTES_20:    return 20;
    case FDCAN_DLC_BYTES_24:    return 24;
    case FDCAN_DLC_BYTES_32:    return 32;
    case FDCAN_DLC_BYTES_48:    return 48;
    case FDCAN_DLC_BYTES_64:    return 64;

    default:
      while(1); //error
  }
}

/*
  * @brief Codes decimal length of FDCAN message into the FDCAN_data_length_code
  * @param[in]          length              Decimal message length (bytes)
  * @retval             FDCAN_data_length_code        Code of required message length
*/
static uint32_t LengthCoder( uint8_t length )
{
  switch( length )
  {
    case 0:     return FDCAN_DLC_BYTES_0;
    case 1:     return FDCAN_DLC_BYTES_1;
    case 2:     return FDCAN_DLC_BYTES_2;
    case 3:     return FDCAN_DLC_BYTES_3;
    case 4:     return FDCAN_DLC_BYTES_4;
    case 5:     return FDCAN_DLC_BYTES_5;
    case 6:     return FDCAN_DLC_BYTES_6;
    case 7:     return FDCAN_DLC_BYTES_7;
    case 8:     return FDCAN_DLC_BYTES_8;
    case 12:    return FDCAN_DLC_BYTES_12;
    case 16:    return FDCAN_DLC_BYTES_16;
    case 20:    return FDCAN_DLC_BYTES_20;
    case 24:    return FDCAN_DLC_BYTES_24;
    case 32:    return FDCAN_DLC_BYTES_32;
    case 48:    return FDCAN_DLC_BYTES_48;
    case 64:    return FDCAN_DLC_BYTES_64;

    default:
      while(1); //error
  }
}

static void startMicrosecondsTimer(void){
	HAL_TIM_Base_Start(uavcan_htim);
}

static uint32_t getCurrentMicroseconds(void){
	return __HAL_TIM_GET_COUNTER(uavcan_htim);
}

/**
 * @brief Processes received transfers based on their port ID.
 *
 * This function processes incoming transfers by checking their port ID. If the port ID matches
 * a specific value, it calls the corresponding callback function to handle the transfer.
 *
 * @param[in] redundant_interface_index The index of the redundant interface from which the transfer was received.
 * @param[in] transfer Pointer to the received transfer structure.
 */
static void processReceivedTransfer(uint8_t redundant_interface_index, CanardRxTransfer* transfer){
    if (transfer->metadata.port_id == 4){
    	rxMessageCallback(transfer);
    }
    else if (transfer->metadata.port_id == uavcan_node_ExecuteCommand_1_1_FIXED_PORT_ID_){
    	executeCommandCallback(transfer);
    }
    else if (transfer->metadata.port_id == 58){
    	rxGyroCoefsCallback(transfer);
    }
}

// Functions for use printf()
int __io_putchar(uint8_t ch){
  HAL_UART_Transmit(&hlpuart1, &ch, 1, HAL_MAX_DELAY);
  return ch;
}

int _write(int file, char *ptr, int len){
  int DataIdx;
  for (DataIdx = 0; DataIdx < len; DataIdx++)
  {
    __io_putchar(*ptr++);
  }
  return len;
}

/* -----------------------------------------------------------------------------------------------------*/
/* -------------------- TX messages functions and Recv callbacks ---------------------------------------*/
/* -----------------------------------------------------------------------------------------------------*/
/* USER CODE MESSAGES TRANSMIT */

void sendHeartbeatMessage(void){
	static uint8_t heartbeat_transfer_id = 0;
    uavcan_node_Heartbeat_1_0 test_heartbeat = {.uptime = getCurrentMicroseconds()/1000000u,
                                                .health = {uavcan_node_Health_1_0_NOMINAL},
                                                .mode = {uavcan_node_Mode_1_0_OPERATIONAL}};

    // Serialize the heartbeat message
    if (uavcan_node_Heartbeat_1_0_serialize_(&test_heartbeat, hbeat_ser_buf, &hbeat_ser_buf_size) < 0){
    	Error_Handler();
    }

    // Create a transfer for the heartbeat message
    const CanardTransferMetadata transfer_metadata = {.priority = CanardPriorityNominal,
                                                      .transfer_kind = CanardTransferKindMessage,
                                                      .port_id = uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_,
                                                      .remote_node_id = CANARD_NODE_ID_UNSET,
                                                      .transfer_id = my_message_transfer_id,};

    __disable_irq();
    if(canardTxPush(&queue, &canard, 0, &transfer_metadata, hbeat_ser_buf_size, hbeat_ser_buf) < 0)
		Error_Handler();
	__enable_irq();
	heartbeat_transfer_id++;
}

void sendReal32ArrayMessage(void) {
	static uint8_t real32_transfer_id = 0;
    uavcan_primitive_array_Real32_1_0 message = {
        .value.elements = {1.0f, 2.0f, 3.0f, 4.0f},
		.value.count = 4
    };

    size_t message_ser_buf_size = uavcan_primitive_array_Real32_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_;
    uint8_t message_ser_buf[uavcan_primitive_array_Real32_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];

    if (uavcan_primitive_array_Real32_1_0_serialize_(&message, message_ser_buf, &message_ser_buf_size) < 0) {
        Error_Handler();
    }

    const CanardTransferMetadata transfer_metadata = {
        .priority = CanardPriorityNominal,
        .transfer_kind = CanardTransferKindMessage,
        .port_id = CURRENT_PORT_ID,  // Укажите свой ID порта
        .remote_node_id = CANARD_NODE_ID_UNSET,
        .transfer_id = real32_transfer_id,
    };

    if (canardTxPush(&queue, &canard, 0, &transfer_metadata, message_ser_buf_size, message_ser_buf) < 0) {
        Error_Handler();
    }

    real32_transfer_id++;
}

void sendExecuteCommandRequest(void){
	static uint8_t execute_command_transfer_id = 0;
	uavcan_node_ExecuteCommand_Request_1_1 request = {
        .command = uavcan_node_ExecuteCommand_Request_1_1_COMMAND_RESTART,
        .parameter = {0}
    };

    size_t request_ser_buf_size = uavcan_node_ExecuteCommand_Request_1_1_SERIALIZATION_BUFFER_SIZE_BYTES_;
    uint8_t request_ser_buf[uavcan_node_ExecuteCommand_Request_1_1_SERIALIZATION_BUFFER_SIZE_BYTES_] = {0};

    if (uavcan_node_ExecuteCommand_Request_1_1_serialize_(&request, request_ser_buf, &request_ser_buf_size) < 0) {
        Error_Handler();
    }

    const CanardTransferMetadata transfer_metadata = {
        .priority = CanardPriorityNominal,
        .transfer_kind = CanardTransferKindRequest,
        .port_id = uavcan_node_ExecuteCommand_1_1_FIXED_PORT_ID_,
        .remote_node_id = DESTANATION_NODE_ID,  // ID узла, которому отправляется запрос
        .transfer_id = execute_command_transfer_id,
    };

    if (canardTxPush(&queue, &canard, 0, &transfer_metadata, request_ser_buf_size, request_ser_buf) < 0) {
        Error_Handler();
    }
}

void sendGyroCoefsMessage(void) {
	static uint8_t gyro_transfer_id = 0;
	uavcan_GyroCoefs_1_0 message = {
        .koeff_0_x = 1.1,
		.koeff_0_y = 2.1,
		.koeff_0_z = 3.1,
		.koeff_1_x = 4.2,
		.koeff_1_y = 5.3,
		.koeff_1_z = 6.4,
		.koeff_2_x = 7.5,
		.koeff_2_y = 8.5,
		.koeff_2_z = 9.2,
    };

    size_t message_ser_buf_size = uavcan_GyroCoefs_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_;
    uint8_t message_ser_buf[uavcan_GyroCoefs_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_] = {0};

    if (uavcan_GyroCoefs_1_0_serialize_(&message, message_ser_buf, &message_ser_buf_size) < 0) {
        Error_Handler();
    }

    const CanardTransferMetadata transfer_metadata = {
        .priority = CanardPriorityNominal,
        .transfer_kind = CanardTransferKindMessage,
        .port_id = 58,  // Укажите свой ID порта
        .remote_node_id = CANARD_NODE_ID_UNSET,
        .transfer_id = gyro_transfer_id,
    };

    if (canardTxPush(&queue, &canard, 0, &transfer_metadata, message_ser_buf_size, message_ser_buf) < 0) {
        Error_Handler();
    }

    gyro_transfer_id++;
}

/* USER CODE MESSAGES TRANSMIT END */
/* -----------------------------------------------------------------------------------------------------*/
/* USER CODE MESSAGES RECEIVE CALLBACK */
static void rxMessageCallback(CanardRxTransfer *transfer){
	  uavcan_primitive_array_Real32_1_0 array = {0};
	  size_t array_ser_buf_size = uavcan_primitive_array_Real32_1_0_EXTENT_BYTES_;

	  if ( uavcan_primitive_array_Real32_1_0_deserialize_( &array, transfer->payload, &array_ser_buf_size) < 0 )
	  {
	    Error_Handler();
	  }
}

static void executeCommandCallback(CanardRxTransfer *transfer)
{
	uavcan_node_ExecuteCommand_Request_1_1 request;
	uavcan_node_ExecuteCommand_Response_1_1 response = {0};

	size_t request_ser_buf_size = uavcan_node_ExecuteCommand_Request_1_1_EXTENT_BYTES_;

	if( uavcan_node_ExecuteCommand_Request_1_1_deserialize_(&request, transfer->payload, &request_ser_buf_size ) < 0){
		Error_Handler();
	}

	if( request.command == uavcan_node_ExecuteCommand_Request_1_1_COMMAND_RESTART ){
		MCU_restart = 1;
		response.status = uavcan_node_ExecuteCommand_Response_1_1_STATUS_SUCCESS;
	}
	else{
		response.status = uavcan_node_ExecuteCommand_Response_1_1_STATUS_BAD_COMMAND;
	}

	uint8_t c_serialized[uavcan_node_ExecuteCommand_Response_1_1_SERIALIZATION_BUFFER_SIZE_BYTES_] = {0};
	size_t c_serialized_size = sizeof(c_serialized);

	if ( uavcan_node_ExecuteCommand_Response_1_1_serialize_(&response, &c_serialized[0], &c_serialized_size) < 0){
		Error_Handler();
	}

	const CanardTransferMetadata transfer_metadata = {    .priority       = CanardPriorityNominal,
														.transfer_kind  = CanardTransferKindResponse,
														.port_id        = transfer->metadata.port_id,
														.remote_node_id = transfer->metadata.remote_node_id,
														.transfer_id    = transfer->metadata.transfer_id };

	(void) canardTxPush(        &queue,               	// Call this once per redundant CAN interface (queue)
										&canard,
										0,     					// Zero if transmission deadline is not limited.
										&transfer_metadata,
										c_serialized_size,		// Size of the message payload (see Nunavut transpiler)
										c_serialized);
}

static void rxGyroCoefsCallback(CanardRxTransfer *transfer){
	uavcan_GyroCoefs_1_0 gyro_ = {0};
	  size_t gyro_size = uavcan_GyroCoefs_1_0_EXTENT_BYTES_;

	  if ( uavcan_GyroCoefs_1_0_deserialize_( &gyro_, transfer->payload, &gyro_size) < 0 )
	  {
	    Error_Handler();
	  }
}
/* USER CODE MESSAGES RECEIVE CALLBACK END */
/* -----------------------------------------------------------------------------------------------------*/

