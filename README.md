

# UavCan Library Usage Guide
* This guide will help you integrate the UavCan library into your STM32-based project. We will use STM32CubeMX for code generation and CMake for project building. *

This project utilizes the libcanard library for implementing the UavCan protocol. We acknowledge and appreciate the contributions of the OpenCyphal community and the authors of libcanard.

## Acknowledgments
- libcanard: [GitHub Repository](https://github.com/OpenCyphal/libcanard) 
- OpenCyphal: [Official Website](https://opencyphal.org/)
Thank you to the OpenCyphal community for providing this valuable library.

## Step 1: Project Preparation
### 1. Create a Project in STM32CubeMX:
- Open STM32CubeMX and create a new project for your microcontroller (e.g., STM32G474xx).
- Configure the necessary peripherals (FDCAN, UART, TIM, GPIO).
- Generate the project code.
### 2. Clone the UavCan Library (git submodule):

## Step 2: Integrate the UavCan Library
### 1.  Add the UavCan Library to Your Project:
- Add the UavCan submodule to your project and add the path in Properties -> C/C++ General -> Paths and Symbols or add the library paths in your build system.
### 2. Include the Header File in Your Business Logic File:
```с
/* USER CODE BEGIN Includes */
#include "UavCan.h"
/* USER CODE END Includes */
```
### 3. Configure the Main Function main:
- Insert the following code in the USER CODE BEGIN 2 section or the initialization section:
```с
/* USER CODE BEGIN 2 */
uavcanAppInit(&htim16);
/* USER CODE END 2 */
```
- Insert the following code in the USER CODE BEGIN WHILE section:
```с
while (1)
{
  uavcanAppProcess();
}
```
### 4. To Call a Message:
- Messages are defined in the UavCan.h file. These functions are called in your business logic.
```с
/* -----------------------------------------------------------------------------------------------------*/
/* USER CODE MESSAGES TRANSMIT */

void sendHeartbeatMessage(void);

void sendReal32ArrayMessage(void);
/* USER CODE MESSAGES TRANSMIT END */
/* -----------------------------------------------------------------------------------------------------*/
```

## Step 3: Configure the UavCan Library
### 1. Include Header Files:
- To work with specific message and service types, you need to include the corresponding header files. This is done in the USER CODE INCLUDES section.
```с
#include "uavcan/node/Heartbeat_1_0.h"
#include "uavcan/node/ExecuteCommand_1_1.h"
#include "uavcan/primitive/array/Real32_1_0.h"
#include "uavcan/node/GyroCoefs_1_0.h"
```
- These types are located in the uavcan/uavcan/ folder. You can also create your own data types using the DSDL language and the Nunavut tool.
### 2. Subscribe to Messages:
- To receive messages from other nodes, you need to subscribe to these messages. This is done in the uavCanRxSubscribe function.
```с
/* USER CODE SUBSCRIBTIONS */
if( canardRxSubscribe(      &canard,
                            CanardTransferKindMessage,
                            4,  // USER DEFINED PORT for this message type
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
/* USER CODE END SUBSCRIBTIONS */
```
- Add subscriptions to the messages and services you need. Ensure you use the correct values for port_id and extent_bytes.
- Request/Response port_id is always standard.
### 3. Send Messages:
- To send messages to other nodes, you need to implement the send functions. This is done in the USER CODE MESSAGES TRANSMIT section.
```с
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
/* USER CODE MESSAGES TRANSMIT END */
```
- Implement the functions to send the messages you need. Ensure you correctly serialize the messages and add them to the queue for transmission.
### 4. Process Received Messages:
- To process received messages, you need to implement the corresponding callback functions. This is done in the USER CODE MESSAGES RECEIVE CALLBACK section.
```с
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

    (void) canardTxPush(        &queue,                // Call this once per redundant CAN interface (queue)
                                        &canard,
                                        0,      // Zero if transmission deadline is not limited.
                                        &transfer_metadata,
                                        c_serialized_size,    // Size of the message payload (see Nunavut transpiler)
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
```
- Implement the callback functions to process received messages. Ensure you correctly deserialize the messages and handle them. Next, add the calls to these functions in the processReceivedTransfer function.
```с
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
```

# How to Create New Data Types:
https://github.com/OpenCyphal/nunavut

## 1. Install Nunavut:
- Nunavut is a tool for interpreting .dsdl files into .c code. Nunavut depends on PyDSDL.
- Install Nunavut using PIP:

```
pip install -U nunavut
```
## 2. Create the Required Folder with the Desired Data Type:
- Generate C headers using the command-line tool. This example assumes that the reg and uavcan directories are located under public_regulated_data_types/. Nunavut is invoked to generate code for the former.
```
nnvg --target-language c --enable-serialization-asserts public_regulated_data_types/reg --lookup-dir public_regulated_data_types/uavcan
```
## 3. Example Service in .dsdl:
- Example service described in .dsdl format:
```
@union
float32 koeff_0_x
float32 koeff_1_x
float32 koeff_2_x
float32 koeff_0_y
float32 koeff_1_y
float32 koeff_2_y
float32 koeff_0_z
float32 koeff_1_z
float32 koeff_2_z

@extent 72 * 8

---

@struct
float32 result_x
float32 result_y
float32 result_z

@extent 96 * 8
```
## 4. Example Message in .dsdl:
- Example message described in .dsdl format:
```
@struct
@fixed_port_id 42

float32 koeff_0_x
float32 koeff_1_x
float32 koeff_2_x
float32 koeff_0_y
float32 koeff_1_y
float32 koeff_2_y
float32 koeff_0_z
float32 koeff_1_z
float32 koeff_2_z

@extent 72 * 8
```

