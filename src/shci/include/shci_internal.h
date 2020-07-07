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

#define	SHCI_STACK_SIZE    ( 2048 )

#define	SHCI_TASK_PRIORITY	( 10 )

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
#define	SYNC_CHAR	0xAA

//#define	MAX_TX_RESPONSE	( 5 * 1024 )                           
#define	MAX_TX_RESPONSE	( 512 )                           /**< Maximum Data for SHCI Response */

#define RESPONSE_QUEUE_SIZE  ( 4 )                             /**< Number of items in the SHCI Response Queue */

//#define	SHCI_MESSAGE_BUFFER_SIZE	( (const size_t) ( 10000 ) )
#define	SHCI_MESSAGE_BUFFER_SIZE	( (const size_t) ( 1024 ) )

/**
 * @brief SHCI Command format
 */
typedef struct _shciCommand
{
	uint8_t	sync;
	uint16_t length;
	uint8_t	opCode;
	uint8_t data[ MAX_PARAM_LEN ];
	uint8_t checksum;
	uint16_t pbCount;						// Parameter byte count
} _shciCommand_t;


typedef struct _shciResponse
{
	uint16_t   numBytes;
	uint8_t    buffer[ MAX_TX_RESPONSE ];
} _shciResponse_t;

#endif /* _DWSHCI_INTERNAL_H_ */
