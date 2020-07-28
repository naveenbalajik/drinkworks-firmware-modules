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
#include <stdint.h>
#include <stdbool.h>
#include <esp_wifi.h>
#include "shci.h"
#include "shci_internal.h"
#include "crc16_ccitt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "shci_logging.h"

#include "message_buffer.h"

#include "espFunction.h"
#include "bleFunction.h"
#include "wifiFunction.h"

#include "esp_log.h"

/************************************************************/
#define TEST_POINT_1    15			// Camera LED, IO15
#define	TEST_POINT_2	33			// Camera Reset, IO33
#define GPIO_OUTPUT_PIN_SEL  ( ( 1ULL << TEST_POINT_1 ) | ( 1ULL << TEST_POINT_2 ) )

#ifdef	DEBUG_USING_GPIO

#define	DEBUG_GPIO_INIT()				debug_gpio_init()
#define	DEBUG_GPIO_PIN_SET( pin )	    gpio_set_level( pin, 1)
#define	DEBUG_GPIO_PIN_CLR( pin )	    gpio_set_level( pin, 0)
#define DEBUG_GPIO_PIN_TOGGLE( pin )	gpio_set_level( pin, (gpio_set_level( pin ) ^ 0x01) )

#else

#define	DEBUG_GPIO_INIT()
#define	DEBUG_GPIO_PIN_SET( pin )
#define	DEBUG_GPIO_PIN_CLR( pin )
#define DEBUG_GPIO_PIN_TOGGLE( pin )

#endif

/**
 * @brief SHCI Command List - array of table pointers
 */
static _shciCommandTable_t ShciCommandTableList[MAX_SHCI_COMMAND_LIST_TABLES];

static int _shciUartNum;									/**< UART port number used for Host communication */
static _shciRxStateType_t _rxState = eSHCIRxSync;
static _shciCommand_t IncommingCommand;

static MessageBufferHandle_t   _shciMessageBuffer;
static TaskHandle_t _shciTaskHandle;
static QueueHandle_t hci_handle;
static uint8_t TxEventBuf[180];
static bool _shciUseCRC;
static portMUX_TYPE shci_spinlock = portMUX_INITIALIZER_UNLOCKED;


static	_shciResponse_t _txPacket;

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
 * @brief	Initialize selected GPIO pins as outputs, for debug and timing analysis
 *
 * This function is only implemented is DEBUG_USING_GPIO is defined
 */
#ifdef	DEBUG_USING_GPIO
void debug_gpio_init( void )
{
    gpio_config_t io_conf;

    io_conf.intr_type = GPIO_INTR_DISABLE;    		// disable interrupt
    io_conf.mode = GPIO_MODE_OUTPUT;   				// set as output mode
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;   	// bit mask of the pins that you want to set, e.g.GPIO15/33
    io_conf.pull_down_en = 0;   					// disable pull-down mode
    io_conf.pull_up_en = 0;    						// disable pull-up mode
    gpio_config(&io_conf);    						// configure GPIO with the given settings

    DEBUG_GPIO_PIN_CLR( TEST_POINT_1 );
    DEBUG_GPIO_PIN_CLR( TEST_POINT_2 );
}
#endif

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
	
	_shciUseCRC = false;

	/* Intialize SHCI Command List */
	for( i = 0; i < MAX_SHCI_COMMAND_LIST_TABLES; ++i )
	{
		ShciCommandTableList[i].pTable = NULL;
		ShciCommandTableList[i].numEntries = 0;
	}

    xTaskCreate( _shciTask, "shci_task", SHCI_STACK_SIZE, (void *) _shciUartNum, SHCI_TASK_PRIORITY, &_shciTaskHandle );		// Create Task on new thread, Increase stack size
    if( _shciTaskHandle == NULL )
	{
        return -1;
    }

    hci_handle = xQueueCreate( 1, sizeof(int) );											// Create a queue ... used for passkey acceptance
    if( hci_handle == NULL ) 
	{
        return -1;
    }

	espFunction_init();					/* Initialize DW ESP Function module */
	bleFunction_init();					/* Initialize DW BLE Function module */
	wifiFunction_init();				/* Initialize DW WiFi Function module */
	
	DEBUG_GPIO_INIT();					/* Initialize GPIO for debugging and timing analysis */

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

    /* Message buffer needs to be created within the task, if it is created in the init call we get exceptions ! */

	_shciMessageBuffer = xMessageBufferCreate( SHCI_MESSAGE_BUFFER_SIZE );				/* Create a message buffer */
    if( _shciMessageBuffer == NULL ) 
	{
		IotLogError( "Error creating Message Buffer" );
    }

    while( 1 ) 
	{
		
    	DEBUG_GPIO_PIN_TOGGLE( TEST_POINT_2 );								// Debug - Toggle TestPoint2

    	/* Process incoming packets */
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
		
    	/* Process any outgoing messages in the message buffer */
		if( _shciMessageBuffer != NULL )
		{
			_txPacket.numBytes = xMessageBufferReceive( _shciMessageBuffer, &_txPacket.buffer, MAX_TX_RESPONSE, ( TickType_t ) 0 );
			
			if ( _txPacket.numBytes != 0 )
			{
				
//			    DEBUG_GPIO_PIN_CLR( TEST_POINT_2 );
	            IotLogInfo( "Read %d bytes from Message Buffer",  _txPacket.numBytes );
			
				_shciSendResponse( & _txPacket );
			}
			// Test with hardwired response to Device Information command times-out with vTaskDelay(50);
		}
		vTaskDelay( 10 );
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
 *
 * @param[in]	opCode	Op-code of the completing command
 * @param[in]	error	Error code for the completing command
 */
void shci_postCommandComplete( _CommandOpcode_t opCode, _errorCodeType_t error)
{
	uint8_t commandCompleteResponse[ 3 ];

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
 * @return true if Response was successfully send to Message Buffer, false if error when Sending Response to Message Buffer
 *
 * Note that Response Data is copied into message Buffer, so caller does not need to 
 * maintain buffer integrity.
 */
bool shci_PostResponse( const uint8_t *pData, size_t numBytes )
{
	bool error = true;
	size_t n;
	
	if ( _shciMessageBuffer != NULL )
	{
		/*  Since this function can be called from different tasks, must follow:
		 *
		 * "If there are to be multiple different writers then the application writer must
		 *  place each call to a writing API function (such as xMessageBufferSend()) inside
		 *  a critical section and use a send block time of 0"
		 */
	    portENTER_CRITICAL(&shci_spinlock);
		n = xMessageBufferSend( _shciMessageBuffer, ( const void * ) pData, numBytes, 0 );
	    portEXIT_CRITICAL(&shci_spinlock);

		if( n == numBytes )
		{
//		    DEBUG_GPIO_PIN_SET( TEST_POINT_2 );
//			 vTaskDelay( 10 );
			IotLogInfo( "shci_PostResponse %d bytes", numBytes);
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

	_shciRxStateType_t nextState = _rxState;	// default to maintaining current state
	do
	{
		_rxState = nextState;

		/* get number of bytes in UART RX FIFO */
		uart_get_buffered_data_len( _shciUartNum, ( size_t * )&len );

		switch( _rxState )
		{

			case eSHCIRxSync:
				if( len >= 1 )
				{
					len = uart_read_bytes( _shciUartNum, &ic->head.sync, 1, 0 );			// read one byte
					if( ( len == 1 ) && ( ( SYNC_A_CHAR == ic->head.sync ) || (SYNC_B_CHAR == ic->head.sync ) ) )
					{
						DEBUG_GPIO_PIN_SET( TEST_POINT_1 );
						nextState = eSHCIRxHead;
						ic->useCRC = ( SYNC_B_CHAR == ic->head.sync ) ? true : false;
						ic->pbCount = 0;
					}
				}
				break;

			case eSHCIRxHead:
				if( len >= 2 )
				{
	//	            DEBUG_GPIO_PIN_CLR( TEST_POINT_1 );
					len = uart_read_bytes( _shciUartNum, ic->head.length, 2, 0 );					// read 2 bytes
					ic->length = ( ic->head.length[0] << 8 ) + ic->head.length[1];					// reassemble length
					nextState = eSHCIRxData;
				}
				break;

			case eSHCIRxData:																		// includes the OpCode, any parameters and checksum/crc
				if( ic->useCRC )
				{
					if( len >= ( ic->length + 2 ) )
					{
	//				    DEBUG_GPIO_PIN_SET( TEST_POINT_1, 1);
						len = uart_read_bytes( _shciUartNum, &ic->opCode, (ic->length + 2), 0 );	// read (length+2) bytes
						/* compute CRC and validate that it is zero */
						ic->crc = crc16_ccitt_compute( &ic->rawData[ 1 ], ( ic->length + 4 ) );		// include length and crc bytes
					}
					DEBUG_GPIO_PIN_CLR( TEST_POINT_1 );
					if ( ic->crc == 0 )																// if crc is zero
					{
						ic->pbCount = ( ic->length - 1 );
						_shciUseCRC = true;															// if a CRC packet is successfully received, enable CRC's when transmitting
						nextState = eSHCIRxValid;
					}
					else
					{
						IotLogError( "ProcessInput, non-zero CRC" );
						nextState = eSHCIRxError;
					}
				}
				else
				{
					if ( len >= ( ic->length + 1 ) )
					{
						len = uart_read_bytes( _shciUartNum, &ic->opCode, (ic->length + 1), 0 );	// read (length+1) bytes
						ic->checksum = 0;
						for ( int i = 1; i <= ( ic->length + 3 ); ++i ) 							// sum all data bytes, including length and checksum
						{
							ic->checksum += ic->rawData[ i ];
						}
						if ( ic->checksum == 0 )													// if checksum is zero
						{
							ic->pbCount = ( ic->length - 1 );
							DEBUG_GPIO_PIN_CLR( TEST_POINT_1 );
							nextState = eSHCIRxValid;
						}
						else
						{
							IotLogError( "ProcessInput, non-zero checksum" );
		//	        		esp_log_buffer_hex( TAG, (void *)ic->data, ic->length );
							nextState = eSHCIRxError;
						}
					}
				}
				break;

			case eSHCIRxValid:
			case eSHCIRxError:
				break;

			default:
				nextState = eSHCIRxSync;
				break;
		}
	} while( nextState != _rxState ); 		// keep looping if state changes

	_rxState = nextState;

	return _rxState;
}


/**
 * @brief Send Response Packet to Host
 *
 * @param[in] pResponse  Pointer to Response Data Structure
 */
static void _shciSendResponse( _shciResponse_t * pResponse )
{
	uint16_t j;
	uint16_t i = 0;
	uint8_t chksum = 0;

	if( _shciUseCRC )																/* SHCI Protocol with CRC16 */
	{
		uint16_t crc;

		TxEventBuf[ i++ ] = SYNC_B_CHAR;
		TxEventBuf[ i++ ] = ( uint8_t )( pResponse->numBytes >> 8 );					/* MSB of length field */
		TxEventBuf[ i++ ] = ( uint8_t )( pResponse->numBytes & 0xFF );					/* LSB of length field */

		for( j = 0; j < pResponse->numBytes; ++j )										/* Copy Response data to TxEventBuf */
		{
			TxEventBuf[ i++ ] = pResponse->buffer[ j ];
		}

		crc = crc16_ccitt_compute( &TxEventBuf[ 1 ], ( pResponse->numBytes + 2 ) );

		TxEventBuf[ i++ ] = ( uint8_t )( crc >> 8 );									/* Add CRC msb */
		TxEventBuf[ i++ ] = ( uint8_t )( crc & 0xff );									/* Add CRC lsb */
//		iot_log_info( "_shciSendResponse: ");
//		esp_log_buffer_hex( "_shciSendResponse: ", (void *)TxEventBuf, i );
	}
	else																				/* SHCI Protocol with 8-bit checksum */
	{
		TxEventBuf[ i++ ] = SYNC_A_CHAR;
		chksum += TxEventBuf[ i++ ] = ( uint8_t )( pResponse->numBytes >> 8 );			/* MSB of length field */
		chksum += TxEventBuf[ i++ ] = ( uint8_t )( pResponse->numBytes & 0xFF );		/* LSB of length field */

		for( j = 0; j < pResponse->numBytes; ++j )										/* Copy Response data to TxEventBuf */
		{
			chksum += TxEventBuf[ i++ ] = pResponse->buffer[ j ];
		}

		TxEventBuf[ i++ ] = ( uint8_t )( 0 - chksum );									/* Add checksum */
	}

	uart_write_bytes( _shciUartNum, (const char *) TxEventBuf, i );						/* transmit Response packet */
}



