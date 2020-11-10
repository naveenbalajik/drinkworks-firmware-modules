/**
 * @file bleConnect.c
 *
 * This file contains functions to handle the SHCI interface commands to implement the BLE Connect/Disconnect functionality.
 */
 
#include <stdint.h>
#include <string.h>

#include "ble_logging.h"
#include "shci_internal.h"
#include "shci.h"
#include "iot_ble.h"

#include "bleStatus.h"
#include "bleConnect.h"

#include "esp_gap_ble_api.h"

typedef struct
{
    uint8_t sPassKey[7];
    BTBdaddr_t xAddress;
} BLEPassKeyConfirm_t;

static BLEPassKeyConfirm_t xPassKeyConfirm;


#define	MAX_CONNECTION_NUMBER 	5
#define	CONNECTION_MAP_OFFSET	0x30					/**< Offset to convert table index into handle value, eliminates zero as a handle value */
static _connectionMap_t connectionMap[ MAX_CONNECTION_NUMBER ];



static uint8_t passKeyConfirmRequest[] = { ePasskeyConfirmRequest, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static uint8_t pairCompleteEvent[] = { ePairingComplete, 0x00, 0x00 };
static uint8_t leConnectCompleteEvent[] = { eLeConnectionComplete, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static uint8_t disconnectCompleteEvent[] = { eDisconnectionComplete, 0x00, 0x00 };


static _bleStatus_t  _bleStatus = eUnknownStatus;



/**
 * @brief	Store Connection information in map table
 *
 *	Connection ID and Remote BT Address information, associated with a connection, is
 *	stored in a limited capacity Map table.  Entries can be retrieved, using a handle value,
 *	using retrieveConnection().
 *
 *	A non-zero handle value is returned. If the connection information could not be stored
 *	a value of 0xFF is returned.  Otherwise, the index of the table entry plus CONNECTION_MAP_OFFSET
 *	is returned as the handle value.
 *
 * @param[in] connectionID	16-bit connection ID assigned by network manager
 * @param[in]	pRemoteAddress	Pointer to BT Address of remote device
 *
 * @return		Handle value, table index after CONNECTION_MAP_OFFSET has been applied
 * 				0xFF if connection information could not be stored
 */
static uint8_t storeConnection( uint16_t connectionID, BTBdaddr_t *pRemoteAddress )
{
	uint8_t index;
	uint8_t handle = 0xff;

	/* First, try to find matching connection ID, update remote Address if found */
	for( index = 0; index < MAX_CONNECTION_NUMBER; ++index )
	{
		if( connectionID == connectionMap[ index ].connectionID )
		{
			memcpy( connectionMap[ index ].btAddress, (uint8_t * ) pRemoteAddress, sizeof( BTBdaddr_t ) );			// Copy BT Address
			connectionMap[ index ].inUse = true;
			handle = index + CONNECTION_MAP_OFFSET;																	// apply offset to index, creating handle
			IotLogInfo( "storeConnection: %04X, %02X:%02X:%02X:%02X:%02X:%02X -> handle: %02X",
					connectionMap[ index ].connectionID,
					connectionMap[ index ].btAddress[ 0 ],
					connectionMap[ index ].btAddress[ 1 ],
					connectionMap[ index ].btAddress[ 2 ],
					connectionMap[ index ].btAddress[ 3 ],
					connectionMap[ index ].btAddress[ 4 ],
					connectionMap[ index ].btAddress[ 5 ],
					handle
				);
			break;
		}
	}

	/* If no match, look for first unused table entry */
	if( 0xff == handle )
	{
		for( index = 0; index < MAX_CONNECTION_NUMBER; ++index )
		{
			if( false == connectionMap[ index ].inUse )
			{
				connectionMap[ index ].connectionID = connectionID;													// Save Connection ID
				memcpy( connectionMap[ index ].btAddress, (uint8_t * ) pRemoteAddress, sizeof( BTBdaddr_t ) );		// Copy BT Address
				connectionMap[ index ].inUse = true;
				handle = index + CONNECTION_MAP_OFFSET;																// apply offset to index, creating handle
				IotLogInfo( "storeConnection: %04X, %02X:%02X:%02X:%02X:%02X:%02X -> handle: %02X",
						connectionMap[ index ].connectionID,
						connectionMap[ index ].btAddress[ 0 ],
						connectionMap[ index ].btAddress[ 1 ],
						connectionMap[ index ].btAddress[ 2 ],
						connectionMap[ index ].btAddress[ 3 ],
						connectionMap[ index ].btAddress[ 4 ],
						connectionMap[ index ].btAddress[ 5 ],
						handle
					);
				break;
			}
		}
	}
	return handle;
}

/**
 * @brief	Retrieve Connection information using handle value
 *
 * @param[in]	8-bit handle value, adjust by CONNECTION_MAP_OFFSET to give map table index
 *
 * @return		pointer to map table index, NULL, if handle is not valid
 */
_connectionMap_t *retrieveConnection( uint8_t handle )
{
	if( 0xff != handle )
	{
		handle -= CONNECTION_MAP_OFFSET;								// reverse offset
		if( MAX_CONNECTION_NUMBER > handle )							// validate table index
		{
			if( true == connectionMap[ handle ].inUse )					// If in-use
				return( &connectionMap[ handle ] );
		}
	}
	return NULL;
}



/**
 * @brief User Confirm Response Command handler
 *
 * Parameter Data:
 * 		pData[0] = Connection Handle
 * 		pData[1] = User Response
 *
 * @param[in] pData  Pointer to parameter data
 * @param[in] size	 Number of bytes in parameter data
 */
static void vUserConfirmResponse( const uint8_t *pData, const uint16_t size )
{
	IotLogInfo( "UserConfirmResponse, size = %d", size );
	
	// TODO: Do we need to process the Connection handle at Offset 0?
	if( size == 2)
	{
		if( pData[ 1 ] == 0 )
		{
			IotLogInfo( "UserConfirmResponse: Key Accepted" );
			IotBle_ConfirmNumericComparisonKeys( &xPassKeyConfirm.xAddress, true );
		}
		else
		{
			IotLogInfo( "UserConfirmResponse: Key Rejected" );
			IotBle_ConfirmNumericComparisonKeys( &xPassKeyConfirm.xAddress, false );
		}
	}
    else
    {
		shci_postCommandComplete( eUserConfirmResponse, eInvalidCommandParameters );
    }	
}

/* ************************************************************************* */
/* ************************************************************************* */
/* **********        I N T E R F A C E   F U N C T I O N S        ********** */
/* ************************************************************************* */
/* ************************************************************************* */




/**
 * @brief Update BLE Status, post <i>Status Report Event</i> to message buffer when a peer BLE device is connected
 *
 * This function should be called by aws_iot_network_manager whenever there is a change in the BLE status.
 * The posted messages will drive UI messaging on the host, if enabled (i.e. Bluetooth Connected/Disconnected)
 *
 * FIXME - Not the most elegant way of connecting the state change
 *
 * @param[in] connectionID  	Connection identifier (unused)
 * @param[in] pRemoteAddress	BLE Address of connected device
 */
void bleConnect_BleConnected(  uint16_t connectionID, BTBdaddr_t * pRemoteAddress )
{
	uint8_t	handle;
	IotLogInfo( "bleFunction -> Connected" );
	_bleStatus = eBleConnectedMode;

	handle = storeConnection( connectionID, pRemoteAddress );						// Store connection ID and remote address -> connection handle

	leConnectCompleteEvent[ 1 ] = eCommandSucceeded;								//  Status
	leConnectCompleteEvent[ 2 ] = handle;											// Connection Handle
	leConnectCompleteEvent[ 3 ] = 0;												// 0 = Master, 1 = slave
	leConnectCompleteEvent[ 4 ] = 1;												// 0 = public address, 1 = random address, 2 = previously bonded

	memcpy( &leConnectCompleteEvent[5], ( uint8_t * ) pRemoteAddress, sizeof( BTBdaddr_t ) );
	
	/* Connection parameters are hard-coded - ESP IDF v3.3 does not support getConnectionParams() */
	leConnectCompleteEvent[ 11 ] = 0x01;											// Connection Interval MSB
	leConnectCompleteEvent[ 12 ] = 0x80;											// Connection Interval LSB
	
	leConnectCompleteEvent[ 13 ] = 0x00;											// Slave Latency MSB
	leConnectCompleteEvent[ 14 ] = 0x80;											// Slave Latency LSB
	
	leConnectCompleteEvent[ 15 ] = 0x02;											// Supervision Timeout MSB
	leConnectCompleteEvent[ 16 ] = 0x40;											// Supervision Timeout LSB

	shci_PostResponse(  leConnectCompleteEvent, sizeof( leConnectCompleteEvent ) );
	
}

/**
 * @brief	Post <i>Disconnect Complete Event</i> to message buffer
 */
void bleConnect_BleDisconnected( void )
{
	IotLogInfo( "bleFunction -> Disconnected" );
	_bleStatus = eStandbyMode;

	disconnectCompleteEvent[ 1 ] = 0x80;			// Connection Handle
	disconnectCompleteEvent[ 2 ] = eConnectionTerminatedByLocalHost;		// Disconnect reason
	shci_PostResponse(  disconnectCompleteEvent, sizeof( disconnectCompleteEvent ) );
	
}

/**
 * @brief BLE Pairing State Changed Callback
 *
 * This callback is registered by AwsIotNetworkManager and is called when the pairing state changes
 *   eBTbondStateNone,
 *   eBTbondStateBonding,
 *   eBTbondStateBonded,
 *
 * @param[in] xStatus				Bonding Status
 * @param[in] pxRemoteBdAddress		Remote Device Bluetooth Address pointer
 * @param[in] bondState				Bonding State
 * @param[in] xSecurityLevel		Security Level
 * @param[in] xReason  				Reason why authentication failed
 */
void bleConnect_PairingStateChangedCb( BTStatus_t xStatus,
                                  BTBdaddr_t * pxRemoteBdAddr,
                                  BTBondState_t bondState,
                                  BTSecurityLevel_t xSecurityLevel,
                                  BTAuthFailureReason_t xReason )
{
	switch (bondState)
	{
		case eBTbondStateNone:
			pairCompleteEvent[ 1 ] = 0x80;			// FIXME hardwired connection handle
			pairCompleteEvent[ 2 ] = 0x01;			// Passkey failed, rejected by host or timeout (xReason = 0, in both cases)
			shci_PostResponse(  pairCompleteEvent, sizeof( pairCompleteEvent ) );
			IotLogInfo( "Pairing State -> None, reason = %d", xReason );
			break;
			
		case eBTbondStateBonding:
			IotLogInfo( "Pairing State -> Bonding" );
			break;
			
		case eBTbondStateBonded:
			pairCompleteEvent[ 1 ] = 0x80;			// FIXME hardwired connection handle
			pairCompleteEvent[ 2 ] = 0x00;			// Passkey completed
			shci_PostResponse(  pairCompleteEvent, sizeof( pairCompleteEvent ) );
			IotLogInfo( "Pairing State -> Bonded" );
			break;
	}
}

/**
 * @brief Numeric Comparison Callback
 *
 * This callback is registered by AwsIotNetworkManager and is called when a Pass Code needs to be verified
 *
 * @param[in] pxRemoteBdAddress		Pointer to Remote Device's Bluetooth Address
 * @param[in] ulPassKey				Pass Key to be verified.  32-bit value that can be represented as 6-digit ASCII string
 */
 
void bleConnect_NumericComparisonCb( BTBdaddr_t * pxRemoteBdAddr,
                             uint32_t ulPassKey )
{

    if( ( pxRemoteBdAddr != NULL ) && ( ulPassKey <= 999999 ) )
    {
        memcpy( &xPassKeyConfirm.xAddress, pxRemoteBdAddr, sizeof( BTBdaddr_t ) );

		passKeyConfirmRequest[1] = 0x80;		// FIXME Hardwire the Connection handle value
		/* convert 32-bit PassKey value into a 6-digit ASCII representation */
		sprintf( ( char * ) xPassKeyConfirm.sPassKey, "%d", ulPassKey );
		memcpy( &passKeyConfirmRequest[2], xPassKeyConfirm.sPassKey, 6 );
		
		/* Save requester's address, needed for response */
        memcpy( &xPassKeyConfirm.xAddress, pxRemoteBdAddr, sizeof( BTBdaddr_t ) );

		IotLogInfo( "PassKey confirm response: %s", xPassKeyConfirm.sPassKey );
		
		shci_PostResponse( passKeyConfirmRequest, sizeof( passKeyConfirmRequest ) );
    }
}

/**
 * @brief Initialize BLE - register BLE commands
 */
void bleConnect_init( void )
{
	/* Register BLE commands */
	shci_RegisterCommand( eUserConfirmResponse, &vUserConfirmResponse );

}

/**
 * @brief	BLE Status getter
 *
 * @return	BLE Status
 */
_bleStatus_t bleConnect_getStatus( void )
{
	return _bleStatus;
}

/**
 * @brief	BLE Status setter
 *
 * @param[in]	status	BLE Status
 */
void bleConnect_setStatus( _bleStatus_t status )
{
	_bleStatus = status;
}
