/**
 *  @file shci.c
 *
 *	@brief Drinkworks Serial Host Command Interface.
 *
 *	This file defines and implements the serial interface between the ESP module and the Host system.
 *	Command protocol is modeled on the the protocol used by the Microchip BM71 BLE module.
 *
 *  Created on: Sep 3, 2019
 *  @author		ian.whitehead
 *  @copyright	Drinkworks LLC.  All rights reserved.
 *  @date		2019
 */


#include <stdio.h>
#include <esp_wifi.h>
#include "shci.h"
#include "shci_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "shci_logging.h"

#include "message_buffer.h"

#include "espFunction.h"
#include "bleFunction.h"
#include "wifiFunction.h"

/************************************************************/

/**
 * @brief SHCI Command List - array of table pointers
 */
static _shciCommandTable_t ShciCommandTableList[MAX_SHCI_COMMAND_LIST_TABLES];

static int _shciUartNum;									/**< UART port number used for Host communication */
static _shciRxStateType_t _rxState = eSHCIRxSync;
static _shciCommand_t IncommingCommand;

static MessageBufferHandle_t   _shciMessageBuffer;

//static QueueHandle_t _shciResponseQueueHandle;

static TaskHandle_t _shciTaskHandle;
static QueueHandle_t hci_handle;
static uint8_t TxEventBuf[180];

static uint8_t commandCompleteResponse[] = { 0x00, 0x00, 0x00 };


// _shciResponse_t deviceInfoResponse = 
// {
	// .buffer = { 0x80, 0x01, 0x00, 0x25, 0xAE, 0x86, 0x9B, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x01 },
	// .numBytes = 14
// };

static	_shciResponse_t _testResponse;

/************************************************************/

/* Static Function Prototypes */

/**
 * @brief Serial Host Command Interface (shci) Task.  Executes on separate thread, reads input from
 * specified UART and processes commands as received.
 * 
 * Transmit Response Queue allows client modules/tasks to add Response messages that
 * will be transmitted to Host in the order they are queued.
 *
 * @param[in] arg pointer to uart number
 * 
 */
static void _shciTask(void *arg);

/**
 * @brief Dispatch SHCI commands via registered callback function
 *
 * @param[in] opCode  SHCI command OpCode
 * @param[in] pData   Pointer to command parameter buffer
 * @param[in] nBytes  Number of bytes in parameter buffer
 * @return true if no matching registered command found, false if registered command callback called
 */
static bool _shciDispatchCommand( uint8_t opCode, uint8_t *pdata, uint16_t nBytes );

/**
 * @brief Process serial data received from Host
 * 
 * A state machine reads data from the UART, parsing it into the Command packet
 * Receive state is static, since a single incoming packet may require several calls
 * to this function to fully receive and process.
 * 
 * @param[in,out] ic	pointer to Incoming Command packet
 * @return Current Receive Status.  Incoming Command packet contains a complete command when status returned is RX_VALID
 */
static _shciRxStateType_t _shciProcessInput( _shciCommand_t *ic );

/**
 * @brief Send Response Packet to Host
 *
 * @param[in] pResponse  Pointer to Response Data Structure
 */
static void _shciSendResponse( _shciResponse_t * pResponse );

/************************************************************/


/**
 * @brief Serial Host Command Interface (shci) Initialization.  Creates a new thread to run the shci task
 * 
 * @param[in] uart_num	UART port number to use for Host communication
 * @return ESP_FAIL if Task or Queue cannot be created
 *         ESP_OK if Task and Queue were successfully created
 */
int shci_init(int uart_num)
{
	int i;
	_shciUartNum = uart_num;    // Save UART number in static variable
//    uint8_t unknownCommandResponse[3] = { 0x80, 0x00, 0x01 };
	
	/* Intialize SHCI Command List */
	for( i = 0; i < MAX_SHCI_COMMAND_LIST_TABLES; ++i )
	{
		ShciCommandTableList[i].pTable = NULL;
		ShciCommandTableList[i].numEntries = 0;
	}

    xTaskCreate(_shciTask, "shci_task", SHCI_STACK_SIZE, (void *) _shciUartNum, SHCI_TASK_PRIORITY, &_shciTaskHandle);		// Create Task on new thread, Increase stack size
	
    if( _shciTaskHandle == NULL )
	{
        return -1;
    }

	// _shciMessageBuffer = xMessageBufferCreate( SHCI_MESSAGE_BUFFER_SIZE );				/* Create a message buffer */
    // if( _shciMessageBuffer == NULL )
	// {
        // return -1;
    // }
    // ESP_LOGI( TAG, "Message Buffer Created" );

/*
	shci_PostResponse( ( const uint8_t * ) &unknownCommandResponse, sizeof( unknownCommandResponse ) );

    ESP_LOGI( TAG, "Send Message to Message Buffer" );

    // Test - try to read from Message Buffer
	_testResponse.numBytes = xMessageBufferReceive( _shciMessageBuffer, &_testResponse.buffer, MAX_TX_RESPONSE, ( TickType_t ) 0);

	ESP_LOGI( TAG, "Read %d bytes from Message Buffer",  _testResponse.numBytes);
*/
	// _shciResponseQueueHandle = xQueueCreate( RESPONSE_QUEUE_SIZE, sizeof( _shciResponse_t * ) );            // Create Response Queue
    // if( _shciResponseQueueHandle == NULL )
	// {
        // return -1;
    // }

    hci_handle = xQueueCreate( 1, sizeof(int) );											// Create a queue ... used for passkey acceptance
    if( hci_handle == NULL ) 
	{
        return -1;
    }

	espFunction_init();					/* Initialize DW ESP Function module */
	bleFunction_init();					/* Initialize DW BLE Function module */
	wifiFunction_init();					/* Initialize DW WiFi Function module */
	
    return 0;

}


/**
 * @brief Serial Host Command Interface (shci) Task.  Executes on separate thread, reads input from
 * specified UART and processes commands as received.
 * 
 * Client tasks can register callbacks for a list of commands
 * Allowing different commands to be directed to specific modules/tasks
 *
 * Expect at least 3 clients (General, BLE, WiFi), how to register commands?
 *   1. Create a linked list with all commands
 *   2. Create a dynamic list of command tables (i.e. one entry per client)
 *	 3. Create a list of command tables, size defined at initialization
 *   4. Create a list of command tables, size fixed at compile time
 * With the exception of option #3, implementation can be confined to this component and transparent to
 * all other components.
 *
 * Transmit Response Queue allows client modules/tasks to add Response messages that
 * will be transmitted to Host in the order they are queued.
 *
 * @param[in] arg pointer to uart number
 * 
 */
static void _shciTask(void *arg)
{
    int uart_num = (int) arg;
    uint8_t unknownCommandResponse[3] = { 0x80, 0x00, 0x01 };

	
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config( uart_num, &uart_config );
    uart_set_pin( uart_num, SHCI_TXD, SHCI_RXD, SHCI_RTS, SHCI_CTS );							// Set GPIO pins for UART usage
    uart_driver_install( uart_num, BUF_SIZE * 2, 0, 0, NULL, 0 );								// Install UART driver

    // Configure a temporary buffer for the incoming data
//    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);


    /* Message buffer needs to be created within the task, if it is created in the init call we get exceptions ! */

	_shciMessageBuffer = xMessageBufferCreate( SHCI_MESSAGE_BUFFER_SIZE );				/* Create a message buffer */
    if( _shciMessageBuffer == NULL ) 
	{
		IotLogError( "Error creating Message Buffer" );
    }

    while( 1 ) 
	{
		
    	_shciProcessInput( &IncommingCommand );
    	if( _rxState == eSHCIRxValid ) 
		{
			if( _shciDispatchCommand( IncommingCommand.opCode, IncommingCommand.data, IncommingCommand.pbCount ) )
			{
				unknownCommandResponse[1] = IncommingCommand.opCode;     /* overwrite the opcode */
				shci_PostResponse( ( const uint8_t * ) &unknownCommandResponse, sizeof( unknownCommandResponse ) );
			}
    		_rxState = eSHCIRxSync;
    	}
    	else if( _rxState == eSHCIRxError )
		{
    		IotLogError( "Invalid command" );
    		_rxState = eSHCIRxSync;
    	}
		
		if( _shciMessageBuffer != NULL )
		{
			_testResponse.numBytes = xMessageBufferReceive( _shciMessageBuffer, &_testResponse.buffer, MAX_TX_RESPONSE, ( TickType_t ) 0 );
			
			if ( _testResponse.numBytes != 0 )
			{
				
//	            IotLogInfo( "Read %d bytes from Message Buffer",  _testResponse.numBytes );
			
				_shciSendResponse( & _testResponse );
			}
			// Test with hardwired response to Device Information command times-out with vTaskDelay(50);
		}
        vTaskDelay( 1 );
    }
}

/**
 * @brief Register a list of commands with associated callback functions
 *
 * @param[in] pCommandTable Pointer to Command Table
 * @param[in] numEntries Number of entries in Command Table
 * @return true if error registering table, false if Command Table was successfully registered
 */
bool shci_RegisterCommandList( ShciCommandTableElement_t *pCommandTable, uint8_t numEntries )
{
    int i;
	bool error = true;
	
	for( i = 0; i < MAX_SHCI_COMMAND_LIST_TABLES; ++i )
	{
		if( ShciCommandTableList[i].pTable == NULL )
		{
			ShciCommandTableList[i].pTable = pCommandTable;			/* Add Command Table to List */
			ShciCommandTableList[i].numEntries = numEntries;
			error = false;
			break;
		}
	}
	return error;
}

/**
 * @brief Dispatch SHCI commands via registered callback function
 *
 * @param[in] opCode  SHCI command OpCode
 * @param[in] pData   Pointer to command parameter buffer
 * @param[in] nBytes  Number of bytes in parameter buffer
 * @return true if no matching registered command found, false if registered command callback called
 */
static bool _shciDispatchCommand( uint8_t opCode, uint8_t *pdata, uint16_t nBytes )
{
	int i;
	bool error = true;
	_shciCommandTable_t *pCommandTable;
	ShciCommandTableElement_t	*pElement;
	
	for( pCommandTable = ShciCommandTableList; pCommandTable->pTable != NULL; ++pCommandTable )
	{
		pElement = pCommandTable->pTable;
		for( i = 0; i < pCommandTable->numEntries; ++i, pElement++ )
		{
			if( pElement->command == opCode )
			{
				pElement->callback(pdata, nBytes);    /* dispatch command callback */
				error = false;
				return error;
			}
		}
	}
	IotLogInfo( "No callback found for command: %02X", opCode );
	return error;
}

/**
 * @brief Post a Command Complete Event message to the Message Buffer
 */
void shci_postCommandComplete( _CommandOpcode_t opCode, _errorCodeType_t error)
{
	commandCompleteResponse[0] = eCommandComplete;
	commandCompleteResponse[1] = opCode;
	commandCompleteResponse[2] = error;
	shci_PostResponse(  commandCompleteResponse, sizeof( commandCompleteResponse ) );
}

/**
 * @brief Post a response to be transmitted to Host using the SHCI Message Buffer
 *
 * @param[in] pData  Pointer to Response Data
 * @param[in] numBytes  Number of bytes in Response Data
 * @return true if Reponse was successfully send to Message Buffer, false if error when Sending Response to Message Buffer
 *
 * Note that Response Data is copied into message Buffer, so caller does not need to 
 * maintain buffer integrity.
 */
bool shci_PostResponse( const uint8_t *pData, size_t numBytes )
{
	bool error = true;
	
	if ( _shciMessageBuffer != NULL )
	{
		if( xMessageBufferSend( _shciMessageBuffer, ( const void * ) pData, numBytes, ( TickType_t ) 5 ) == numBytes )
		{
//			IotLogInfo( "shci_PostResponse %d bytes", numBytes);
			return false;		/* Succeeded to post the message, within 5 ticks */
		}
		IotLogError( "shci_PostResponse failed");
	}
	
	return error;
}

/**
 * @brief Process serial data received from Host
 * 
 * A state machine reads data from the UART, parsing it into the Command packet
 * Receive state is static, since a single incoming packet may require several calls
 * to this function to fully receive and process.
 * 
 * @param[in,out] ic	pointer to Incoming Command packet
 * @return Current Receive Status.  Incoming Command packet contains a complete command when status returned is RX_VALID
 */
static _shciRxStateType_t _shciProcessInput( _shciCommand_t *ic )
{
	int len;
	uint8_t	head[2];

	switch( _rxState )
	{

		case eSHCIRxSync:
	        len = uart_read_bytes( _shciUartNum, &ic->sync, 1, ( 10 / portTICK_RATE_MS ) );	// read one byte
			if( ( len == 1 ) && ( ic->sync == SYNC_CHAR ) )
			{
				_rxState = eSHCIRxHead;
				ic->pbCount = 0;
				ic->checksum = 0;
			}
			break;

		case eSHCIRxHead:
	        len = uart_read_bytes( _shciUartNum, head, 2, ( 10 / portTICK_RATE_MS ) );		// read 2 bytes
	        if( len == 2 )
			{
				ic->length = ( head[0] << 8 ) + head[1];
				ic->checksum += head[0] + head[1];
				_rxState = eSHCIRxData;
	        }
			break;

		case eSHCIRxData:																	// includes the OpCode, any parameters and checksum
	        len = uart_read_bytes( _shciUartNum, ic->data, (ic->length + 1), 0 );			// read (length+1) bytes
	        if ( len == ( ic->length + 1 ) ) 
			{
	        	for ( int i = 0; i <= ic->length; ++i ) 									// sum all data bytes, including checksum
				{
	        		ic->checksum += ic->data[i];
	        	}
	        	if ( ic->checksum == 0 )													// if checksum is zero 
				{
	        		ic->opCode = ic->data[0];												// copy first byte to OpCode
					ic->pbCount = ( ic->length - 1 );
	        		_rxState = eSHCIRxValid;
	        	}
	        	else 
				{
	        		IotLogError( "ProcessInput, non-zero checksum:" );
//	        		esp_log_buffer_hex( TAG, (void *)ic->data, ic->length );
	        		_rxState = eSHCIRxError;
	        	}
	        }
			break;

		case eSHCIRxValid:
		case eSHCIRxError:
			break;

		default:
			_rxState = eSHCIRxSync;
			break;
	}
	return _rxState;
}


/**
 * @brief Send Response Packet to Host
 *
 * @param[in] pResponse  Pointer to Response Data Structure
 */
static void _shciSendResponse( _shciResponse_t * pResponse )
{
	uint16_t i = 0;
	uint8_t chksum = 0;

	TxEventBuf[ i++ ] = SYNC_CHAR;
	chksum += TxEventBuf[ i++ ] = ( uint8_t )( pResponse->numBytes >> 8 );        /* MSB of length field */
	chksum += TxEventBuf[ i++ ] = ( uint8_t )( pResponse->numBytes & 0xFF );      /* LSB of length field */

	for( uint16_t j = 0; j < pResponse->numBytes; ++j )                           /* Copy Response data to TxEventBuf */
	{
		chksum += TxEventBuf[ i++ ] = pResponse->buffer[ j ];
	}

	TxEventBuf[ i++ ] = ( uint8_t )( 0 - chksum );                                /* Add checksum */

	uart_write_bytes( _shciUartNum, (const char *) TxEventBuf, i );               /* transmit Response packet */
}



