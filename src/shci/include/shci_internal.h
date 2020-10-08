/**
 *  @file DwShci_internal.h
 *
 *  @brief Internal definition for Drinkworks Serial Host Command Interface
 *
 *  Created on: Sep 3, 2019
 *  @author ian.whitehead
 *  @copyright	Drinkworks LLC.  All rights reserved.
 *  @date		2019
*/

#ifndef _DWSHCI_INTERNAL_H_
#define _DWSHCI_INTERNAL_H_

/**
 * @brief UART configuration definitions
 */
#define SHCI_TXD  (GPIO_NUM_4)
#define SHCI_RXD  (GPIO_NUM_5)
#define SHCI_RTS  (UART_PIN_NO_CHANGE)
#define SHCI_CTS  (UART_PIN_NO_CHANGE)
#define BLE_RX_TIMEOUT (30000 / portTICK_PERIOD_MS)


#define BUF_SIZE (1024)

#define	SHCI_STACK_SIZE    ( 4096 )

#define	SHCI_TASK_PRIORITY	( 17 )

#define MAX_READ_CHAR_BUFF_SIZE 	40					///< Maximum data size for Read Local Characteristic command

/**
 * @brief Number of SHCI Command Tables that can be registered
 */
#define	MAX_SHCI_COMMAND_LIST_TABLES	4

/**
 * @brief Enumerated States if SHCI Recieve State Machine
 */
typedef enum 
{
	eSHCIRxSync,												/**< RX_SYNC */
	eSHCIRxHead, 												/**< RX_HEAD */
	eSHCIRxData,												/**< RX_DATA */
	eSHCIRxValid,												/**< RX_VALID */
	eSHCIRxError												/**< RX_ERROR */
} _shciRxStateType_t;

#define MAX_PARAM_LEN	256
#define	SYNC_A_CHAR		0xAA									/**< Sync byte for SHCI protocol with 8-bit Checksum */
#define	SYNC_B_CHAR		0xCC									/**< Sync byte for SHCI protocol with 16-bit CRC */

//#define	MAX_TX_RESPONSE	( 5 * 1024 )                           
#define	MAX_TX_RESPONSE	( 512 )                           /**< Maximum Data for SHCI Response */

#define RESPONSE_QUEUE_SIZE  ( 4 )                             /**< Number of items in the SHCI Response Queue */

//#define	SHCI_MESSAGE_BUFFER_SIZE	( (const size_t) ( 10000 ) )
#define	SHCI_MESSAGE_BUFFER_SIZE	( (const size_t) ( 1024 ) )

/**
 * @brief SHCI Command format
 *
 * Within packet payload:
 * 		length value includes opCode and data, it does not include the length bytes, or the checksum/crc byte(s)
 * 		Checksum/CRC includes the two length bytes, but not the sync byte
 * 		Checksum/CRC is calculated on rawData[ 1 .. (length + 2) ]
 */
typedef struct _shciCommand
{
	union
	{
		uint8_t	rawData[ MAX_PARAM_LEN + 6 ];
		struct
		{
			struct
			{
				uint8_t	sync;						/**< Sync byte */
				uint8_t length[ 2 ];				/**< head length, includes opcode and data */
			} head;
			uint8_t	opCode;							/**< command/event opcode */
			uint8_t data[ MAX_PARAM_LEN ];			/**< parameter data */
			uint8_t checkbytes[ 2];					/**< allocate space for 8-bit checksum or 16-bit CRC */
		};
	};
	uint16_t length;
	uint8_t checksum;
	uint16_t pbCount;						// Parameter byte count
	bool	useCRC;
	uint16_t	crc;								/**< CRC16_CCITT value */
} _shciCommand_t;


typedef struct _shciResponse
{
	uint16_t   numBytes;
	uint8_t    buffer[ MAX_TX_RESPONSE ];
	bool		useCRC;
} _shciResponse_t;

#endif /* _DWSHCI_INTERNAL_H_ */
