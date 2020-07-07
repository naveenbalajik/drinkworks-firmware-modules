/**
 * @file dw_test_shci.c
 */
 
#include <stdint.h>
#include <string.h>

//#include "iot_ble.h"
#include "iot_app_logging.h"
#include "../src/DwShci_internal.h"
#include "../src/DwShci.h"
#include "../src/DwBleInterface_internal.h"
#include "../src/DwBleInterface.h"

#include "dw_test_shci.h"

typedef enum _compareType
{
	eCompareEquals = 0,
	eCompareGreaterThan = 1,
	eCompareLessThan = 2,
	eCompareGreaterOrEqual = 3,
	eCompareLessOrEqual = 4
} _compareType_t;

typedef struct
{
    uint8_t sPassKey[7];
    BTBdaddr_t xAddress;
} BLEPassKeyConfirm_t;

static BLEPassKeyConfirm_t xPassKeyConfirm;



static void vReadDeviceName( uint8_t *pData, uint16_t size );
static void vWriteDeviceName( uint8_t *pData, uint16_t size );
static void vReadLocalInformation( uint8_t *pData, uint16_t size );
static void vReadStatus( uint8_t *pData, uint16_t size );
static void vReadAllPairedDeviceInformation( uint8_t *pData, uint16_t size );
static void vWriteScanResponseData( uint8_t *pData, uint16_t size );
static void vConnectionParameterUpdateRequest( uint8_t *pData, uint16_t size );
static void vSetAdvertisingEnable( uint8_t *pData, uint16_t size );
static void vSendCharacteristicValue( uint8_t *pData, uint16_t size );
static void vUpdateCharacteristicValue( uint8_t *pData, uint16_t size );
static void vReadLocalCharacteristicValue( uint8_t *pData, uint16_t size );
static void vUserConfirmResponse( uint8_t *pData, uint16_t size );
static void postCommandComplete( uint8_t opCode, _errorCodeType_t error);
//static bool _checkCommandLength( uint16_t expected, _compareType_t compareOp, uint16_t actual );

static ShciCommandTableElement_t bleCommandEntries[] =
{
	{ eReadDeviceName, &vReadDeviceName },
	{ eWriteDeviceName, &vWriteDeviceName },
	{ eReadLocalInformation, &vReadLocalInformation },
	{ eReadStatus, &vReadStatus },
	{ eReadAllPairedDeviceInformation, &vReadAllPairedDeviceInformation },
	{ eWriteScanResponseData, &vWriteScanResponseData },
	{ eConnectionParameterUpdateRequest, &vConnectionParameterUpdateRequest },
	{ eSetAdvertisingEnable, &vSetAdvertisingEnable },
	{ eSendCharacteristicValue, &vSendCharacteristicValue },
	{ eUpdateCharacteristicValue, &vUpdateCharacteristicValue },
	{ eReadLocalCharacteristicValue, &vReadLocalCharacteristicValue },
	{ eUserConfirmResponse, &vUserConfirmResponse }
};

//const uint8_t deviceInfoResponse[] =  { 0x80, 0x01, 0x00, 0x25, 0xAE, 0x86, 0x9B, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x01 };	// ESP32
static const uint8_t deviceInfoResponse[] =  { 0x80, 0x01, 0x00, 0x01, 0x11, 0x01, 0x01, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x01 };	// BM71
static const uint8_t deviceName[] =  { 0x80, 0x07, 0x00, 'E', 'S', 'P', '3', '2', '-', 'W', 'r', 'o', 'v', 'e', 'r' };
static uint8_t commandCompleteResponse[] = { 0x80, 0x00, 0x00 };
static uint8_t bondTableResponse[] = { 0x80, 0x0C, 0x00, 0x00 };
static uint8_t statusResponse[] = { 0x81, eStandbyMode };
static uint8_t passKeyConfirmRequest[] = { ePasskeyConfirmRequest, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static uint8_t pairCompleteEvent[] = { ePairingComplete, 0x00, 0x00 };
static uint8_t leConnectCompleteEvent[] = { eLeConnectionComplete, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static uint8_t disconnectCompleteEvent[] = { eDisconnectionComplete, 0x00, 0x00 };

static uint8_t responseBuffer[ 256 ];
static uint8_t printBuffer[ 256 ];

static _bleStatus_t  _bleStatus = eUnknownStatus;


/**
 * @brief Initialize SHCI Test - register BLE commands
 */
void DwTestShci_init( void )
{
	const uint8_t numEntries = ( sizeof( bleCommandEntries) / sizeof(_shciCommandTable_t) );
	
	if( shci_RegisterCommandList( bleCommandEntries, numEntries ) )
	{
		IotLogError( "Error Registering BLE Command Table" );
	}
	else
	{
		IotLogInfo( "Success Registering BLE Command Table" );
		
	}

}


/**
 * @brief Read Device Name Command handler
 */
static void vReadDeviceName( uint8_t *pData, uint16_t size )
{
	IotLogInfo( "ReadDeviceName size = %d", size );
//    if (_checkCommandLength( (uint16_t) 0, eCompareEquals, size ) == true )
    if ( size == 0 )
    {
	    shci_PostResponse(  deviceName, sizeof( deviceName ) );
	}
	else
	{
		postCommandComplete( ( uint8_t ) eReadDeviceName, eInvalidCommandParameters );
	}
}


/**
 * @brief Write Device Name Command handler
 */
static void vWriteDeviceName( uint8_t *pData, uint16_t size )
{
	IotLogInfo( "WriteDeviceName size = %d", size );
//    if (_checkCommandLength( (uint16_t) 2, eCompareGreaterOrEqual, ( size - 1 ) ) == true )
    if ( size >= 2 )
    {
		postCommandComplete( ( uint8_t ) eWriteDeviceName, eCommandSucceeded );
	}
	else
	{
		postCommandComplete( ( uint8_t ) eWriteDeviceName, eInvalidCommandParameters );
	}
}


/**
 * @brief Read Local Information Command handler
 */
static void vReadLocalInformation( uint8_t *pData, uint16_t size )
{
	IotLogInfo( "ReadLocalInformation, post %d bytes to Message Buffer", sizeof( deviceInfoResponse ) );
//    if (_checkCommandLength( (uint16_t) 0, eCompareEquals, size ) == true )
    if ( size == 0 )
    {
		shci_PostResponse(  deviceInfoResponse, sizeof( deviceInfoResponse ) );
	}
	else
	{
		postCommandComplete( ( uint8_t ) eReadLocalInformation, eInvalidCommandParameters );
	}
}


/**
 * @brief Read Status Command handler
 */
static void vReadStatus( uint8_t *pData, uint16_t size )
{
	IotLogInfo( "ReadStatus" );

    if ( size == 0 )
    {
		statusResponse[ 1 ] = _bleStatus;
		shci_PostResponse(  statusResponse, sizeof( statusResponse ) );
	}
	else
	{
		postCommandComplete( ( uint8_t ) eReadStatus, eInvalidCommandParameters );
	}
}


/**
 * @brief Read All Paired Device Information Command handler (i.e. Bond Table)
 */
static void vReadAllPairedDeviceInformation( uint8_t *pData, uint16_t size )
{
	IotLogInfo( "ReadAllPairedDeviceInformation" );

    if ( size == 0 )
    {
		shci_PostResponse(  bondTableResponse, sizeof( bondTableResponse ) );
	}
	else
	{
		postCommandComplete( ( uint8_t ) eReadAllPairedDeviceInformation, eInvalidCommandParameters );
	}

}


/**
 * @brief Write Scan Response Data Command handler
 */
static void vWriteScanResponseData( uint8_t *pData, uint16_t size )
{
	IotLogInfo( "WriteScanResponseData" );
	postCommandComplete( ( uint8_t ) eWriteScanResponseData, eCommandSucceeded );
}


/**
 * @brief Connection Parameter Update Command handler
 */
static void vConnectionParameterUpdateRequest( uint8_t *pData, uint16_t size )
{
	IotLogInfo( "ConnectionParameterUpdateRequest" );
	if( size != 8 )
	{	
		postCommandComplete( ( uint8_t ) eConnectionParameterUpdateRequest, eInvalidCommandParameters );
	}
	else
	{
		postCommandComplete( ( uint8_t ) eConnectionParameterUpdateRequest, eCommandSucceeded );
	}
}


/**
 * @brief Set Advertising Enable Command handler
 */
static void vSetAdvertisingEnable( uint8_t *pData, uint16_t size )
{
	IotLogInfo( "SetAdvertisingEnable" );
	if( size == 1)
	{
		postCommandComplete( ( uint8_t ) eSetAdvertisingEnable, eCommandSucceeded );
	}
	else
	{
		postCommandComplete( ( uint8_t ) eSetAdvertisingEnable, eInvalidCommandParameters );
	}
	
	shci_PostResponse(  statusResponse, sizeof( statusResponse ) );
}


/**
 * @brief Send Characteristic Value Command handler
 */
static void vSendCharacteristicValue( uint8_t *pData, uint16_t size )
{
	IotLogInfo( "SendCharacteristicValue" );
	if( size >= 4 )
	{
        postCommandComplete( ( uint8_t ) eSendCharacteristicValue, eCommandSucceeded );
	}
	else
	{
        postCommandComplete( ( uint8_t ) eSendCharacteristicValue, eInvalidCommandParameters );
	}
}


/**
 * @brief Update Characteristic Value Command handler
 */
static void vUpdateCharacteristicValue( uint8_t *pData, uint16_t size )
{
	uint16_t characteristicHandle;
//	int i;
	_errorCodeType_t error;
	
	characteristicHandle = ( pData[ 1 ] << 8 ) + pData[ 2 ];
//	IotLogInfo( "UpdateCharacteristicValue %04X", characteristicHandle );
	if( size >= 3 )
	{
		error = bleInterface_SetCharacteristicValueRemote( characteristicHandle, &pData[3], (uint16_t) ( size - 2 ) );
		postCommandComplete( ( uint8_t ) eUpdateCharacteristicValue, ( uint8_t ) error );
	}
	else
	{
		postCommandComplete( ( uint8_t ) eUpdateCharacteristicValue, eInvalidCommandParameters );
	}
	
/*
	if( characteristicHandle == 0x8004 )
	{
		IotLogInfo( "  size = %d", size );
		if( size > 3 )
		{
			for( i = 0; i < ( size - 3 ); ++i )
			{
				printBuffer[ i ] = pData[ i + 3 ];
			}
			printBuffer[ i ] = '\0';
			IotLogInfo( "  CharacteristicValue = %s", printBuffer );
		}
	}
*/	
	/* pData[0] = OpCode, pData[1..2] = handle */
//	retVal = DwBleInterface_SetCharacteristicValueRemote( characteristicHandle, &pData[3], (uint16_t) ( size - 2 ) );
//	IotLogInfo( "UpdateCharacteristicValue %d:%d", retVal, ( size - 2 ) );
	
}


/**
 * @brief Read Local Characteristic Value Command handler
 */
static void vReadLocalCharacteristicValue( uint8_t *pData, uint16_t size )
{
	uint16_t characteristicHandle;
	uint16_t numBytes;
	int i;
	
	if( size == 2 )
	{
		characteristicHandle = ( pData[ 1 ] << 8 ) + pData[ 2 ];
		IotLogInfo( "ReadLocalCharacteristicValue %04X", characteristicHandle );
		
		numBytes = bleInterface_GetCharacteristicValueRemote( characteristicHandle, &responseBuffer[ 3 ], sizeof( responseBuffer ) );
		
		responseBuffer[0] = ( uint8_t ) CommandComplete;
		responseBuffer[1] = ( uint8_t ) eReadLocalCharacteristicValue;
		responseBuffer[2] = ( uint8_t ) eCommandSucceeded;
		
		if( numBytes > 0 )
		{
			for( i = 0; i < numBytes; ++i )
			{
				printBuffer[ i ] = responseBuffer[ i + 3 ];
			}
			printBuffer[ i ] = '\0';
			IotLogInfo( "  CharacteristicValue = %s", printBuffer );
			
			shci_PostResponse(  responseBuffer, ( numBytes + 3 ) );
		}
		else
		{
			postCommandComplete( ( uint8_t ) eReadLocalCharacteristicValue, eInvalidHandle );
		}
	}
	else
	{
		postCommandComplete( ( uint8_t ) eReadLocalCharacteristicValue, eInvalidCommandParameters );
	}
}


/**
 * @brief User Confirm Response Command handler
 */
static void vUserConfirmResponse( uint8_t *pData, uint16_t size )
{
	IotLogInfo( "UserConfirmResponse" );
	
	// FIXME size should be 3, offset 1 is the Connection handle
	if( pData[ 2 ] == 0 )
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


/**
 * @brief Post a Command Complete Event message to the Message Buffer
 */
static void postCommandComplete( uint8_t opCode, _errorCodeType_t error)
{
	commandCompleteResponse[1] = ( uint8_t ) opCode;
	commandCompleteResponse[2] = ( uint8_t ) error;
	shci_PostResponse(  commandCompleteResponse, sizeof( commandCompleteResponse ) );
	
}

/**
 * @brief Update BLE Status, post Status Report Event to message buffer
 *
 * This function should be called by aws_iot_network_manager whenever there is a change in the BLE status.
 * The posted messages will drive UI messaging on the host, if enabled (i.e. Blutooth Connected/Disconnected)
 *
 * FIXME - Not the most elegent way of connecting the state change
 */
void bleFunction_BleConnected(  uint16_t connectionID, BTBdaddr_t * pRemoteAddress )
{
	IotLogInfo( "BLE -> Connected" );
	_bleStatus = eBleConnectedMode;
//	statusResponse[ 1 ] = _bleStatus;
//	DwShci_PostResponse(  statusResponse, sizeof( statusResponse ) );

	leConnectCompleteEvent[ 1 ] = eCommandSucceeded;			//  Status
	leConnectCompleteEvent[ 2 ] = 0x80;		// Connection Handle
	leConnectCompleteEvent[ 3 ] = 0;		// 0 = Master, 1 = slave
	leConnectCompleteEvent[ 4 ] = 1;		// 0 = public address, 1 = random address, 2 = previously bonded

	memcpy( &leConnectCompleteEvent[5], ( uint8_t * ) pRemoteAddress, sizeof( BTBdaddr_t ) );
	
	leConnectCompleteEvent[ 11 ] = 0x01;	// Connection Interval MSB
	leConnectCompleteEvent[ 12 ] = 0x80;	// Connection Interval LSB
	
	leConnectCompleteEvent[ 13 ] = 0x00;	// Slave Latency MSB
	leConnectCompleteEvent[ 14 ] = 0x80;	// Slave Latency LSB
	
	leConnectCompleteEvent[ 15 ] = 0x02;	// Supervision Timeout MSB
	leConnectCompleteEvent[ 16 ] = 0x40;	// Supervision Timeout LSB

	shci_PostResponse(  leConnectCompleteEvent, sizeof( leConnectCompleteEvent ) );
	
}
void bleFunction_BleDisconnected( void )
{
	IotLogInfo( "BLE -> Disconnected" );
	_bleStatus = eStandbyMode;
//	statusResponse[ 1 ] = _bleStatus;
//	DwShci_PostResponse(  statusResponse, sizeof( statusResponse ) );
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
 */
 
void bleFunction_PairingStateChangedCb( BTStatus_t xStatus,
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
 */
 
void bleFunction_NumericComparisonCb( BTBdaddr_t * pxRemoteBdAddr,
                             uint32_t ulPassKey )
{

	IotLogInfo( "BLENumericComparisonCb" );
    if( ( pxRemoteBdAddr != NULL ) && ( ulPassKey <= 999999 ) )
    {
        memcpy( &xPassKeyConfirm.xAddress, pxRemoteBdAddr, sizeof( BTBdaddr_t ) );

//        xQueueSend( xNumericComparisonQueue, ( void * ) &xPassKeyConfirm, ( portTickType ) portMAX_DELAY );
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
 * @brief Check that the command length is valid
 */
 /*
static bool _checkCommandLength( uint16_t expected, _compareType_t compareOp, uint16_t actual )
{
	bool lengthOK = false;
    switch( compareOp )
    {
        case eCompareEquals:
			if ( actual == expected )
			{
				lengthOK = true;
			}
			break;
			
        case eCompareGreaterThan:
			if ( actual > expected )
			{
				lengthOK = true;
			}
			break;
			
        case eCompareLessThan:
			if ( actual < expected )
			{
				lengthOK = true;
			}
			break;
			
        case eCompareGreaterOrEqual:
			if ( actual >= expected )
			{
				lengthOK = true;
			}
			break;
			
        case eCompareLessOrEqual:
			if ( actual <= expected )
			{
				lengthOK = true;
			}
			break;
	}
	
	return ( lengthOK );
}
*/
