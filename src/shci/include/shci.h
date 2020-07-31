/**
 *  @file shci.h
 *
 *  @brief External interface to Drinkworks Serial Host Command Interface
 *
 *  Created on: Sep 3, 2019
 *  @author ian.whitehead
 *  @copyright	Drinkworks LLC.  All rights reserved.
 *  @date		2019
*/

#ifndef _SHCI_H_
#define _SHCI_H_

#define MAX_READ_CHAR_BUFF_SIZE 	40					///< Maximum data size for Read Local Characteristic command

/**
 * @brief Enumerated SHCI Command Opcodes, common to ESP, BLE and WiFi modules
 */
enum CommandOpcode 
{
	eReadLocalInformation =					0x01,
	eReadStatus =							0x03,
	eReadDeviceName =						0x07,
	eWriteDeviceName =						0x08,
	eReadAllPairedDeviceInformation =		0x0C,
	eWriteScanResponseData =				0x12,
	eConnectionParameterUpdateRequest =		0x19,
	eSetAdvertisingEnable =					0x1C,
	eSendCharacteristicValue =				0x38,
	eUpdateCharacteristicValue =			0x39,
	eReadLocalCharacteristicValue =			0x3A,
	eUserConfirmResponse =					0x41,
	
	eWiFiResetProvisioning = 				0xA0,
	eWiFiReadStatus =						0xA1,
	
	eWifiTestParameter =					0xB2,
	eWifiTestStart,
	eWifiTestStop,

	eCaptureArm =							0xB6,
	eCaptureRead,

	eWifiConnectAP = 						0xBA,

	eEspSetSerialNumber = 					0xC0

};
typedef uint8_t _CommandOpcode_t;

/**
 * @brief Enumerated ESP Module Error Codes, common to ESP, BLE and WIFI modules
 */
enum
{
	eCommandSucceeded,												// 0x00
	eUnknownCommand,												// 0x01
	eUnknownConnectionIdentifier,									// 0x02
	eHardwareFailure,												// 0x03
	eAuthenticationFailure = 0x05,									// 0x04
	ePinOrKeyMissing,												// 0x05
	eMemoryCapacityExceeded,										// 0x06
	eConnectionTimeout,												// 0x07
	eConnectionLimitExceeded,										// 0x08
	eACLConnectionAlreadyExists = 0x0b,								// 0x0B
	eCommandDisallowed,												// 0x0C
	eConnectionRejectedDueToLimitedResources,						// 0x0D
	eConnectionRejectedDueToSecurityReasons,						// 0x0E
	eConnectionRejectedDueToUnacceptableBD_ADDR,					// 0x0F
	eConnectionAcceptTimeoutExceeded,								// 0x10
	eUnsupportedFeatureOrParameterValue,							// 0x11
	eInvalidCommandParameters,										// 0x12
	eRemoteUserTerminatedConnection,								// 0x13
	eRemoteDeviceTerminatedConnectionDueToLowResources,				// 0x14
	eRemoteDeviceTerminatedConnectionDueToPowerOff,					// 0x15
	eConnectionTerminatedByLocalHost,								// 0x16
	ePairingNotAllowed = 0x18,										// 0x18
	eUnspecifiedError = 0x1f,										// 0x1F
	eInstantPassed = 0x28,											// 0x28
	ePairingWithUnitKeyNotSupported,								// 0x29
	eInsufficientSecurity = 0x2f,									// 0x2F
	eConnectionRejectedDueToNoSuitableChannelFound = 0x39,			// 0x39
	eControllerBusy,												// 0x3A
	eUnacceptableConnectionInterval,								// 0x3B
	eDirectedAdvertisingTimeout,									// 0x3C
	eConnectionTerminatedDueToMICFailure,							// 0x3D
	eConnectionFailedToBeEstablished,								// 0x3E
	eInvalidHandle = 0x81,											// 0x81
	eReadNotPermitted,												// 0x82
	eWriteNotPermitted,												// 0x83
	eInvalidPDU,													// 0x84
	eInsufficientAuthentication,									// 0x85
	eRequestNotSupported,											// 0x86
	eInvalidOffset,													// 0x87 (Docs say 0x77, but assume typo)
	eInsufficientAuthorization,										// 0x88
	ePrepareQueueFull,												// 0x89
	eAttributeNotFound,												// 0x8A
	eAttributeNotLong,												// 0x8B
	eInsufficientEncryptionKeySize,									// 0x8C
	eInvalidAttributeValueLength,									// 0x8D
	eUnlikelyError,													// 0x8E
	eInsufficientEncryption,										// 0x8F
	eUnsupportedGroupType,											// 0x90
	eInsufficientResources,											// 0x91
	eApplicationDefinedError = 0xf0,								// 0xF0
	eUARTCheckSumError = 0xff										// 0xFF
};
typedef uint8_t _errorCodeType_t;

/**
 * @brief Enumerated SHCI Event OpCodes, common to ESP, BLE and WiFi modules
 */
enum EventOpcode 
{
	NoEventOpcode,													// 0x00
	ePasskeyEntryRequest = 0x60,									// 0x60
	ePairingComplete,												// 0x61
	ePasskeyConfirmRequest,											// 0x62
	AdvertisingReport = 0x70,										// 0x70
	eLeConnectionComplete,											// 0x71
	eDisconnectionComplete,											// 0x72
	ConnectionParameterUpdateNotify,								// 0x73
	eCommandComplete = 0x80,										// 0x80
	eStatusReport,													// 0x81
	ConfigureModeStatus = 0x8f,										// 0x8F
	DiscoverAllPrimaryServiceResponse,								// 0x90
	DiscoverSpecificPrimaryServiceCharacteristicResponse,			// 0x91
	DiscoverAllCharacteristicDescriptorResponse,					// 0x92
//????	CharacteristicValueReceived,								// 0x93
	eClientWriteCharacteristicValue = 0x98,							// 0x98
	ReceivedTransparentData = 0x9a,									// 0x9A
	// All commands below are extensions to those supported by the BM7x Module, and are targeted for the ESP32 module
	eWiFiStatus = 0xB0,
	eNetworkInitializedEvent = 0xB1,
	eWifiTestStatus = 0xB5,
	eCaptureComplete = 0xB8,
	eDispenseComplete = 0xB9
};
typedef uint8_t _eventOpcode_t;


/**
 * @brief Callback called when an SHCI command is received
 */
typedef void (* DwShciCommandCallback_t)( uint8_t *pData, uint16_t size );
 
/**
 * @brief SHCI Command List Element, links an SHCI command with a callback function.
 * List -> Table -> Element
 */
typedef struct ShciCommandTableElement
{
    uint8_t	command;
    DwShciCommandCallback_t callback;
} ShciCommandTableElement_t;

typedef struct _shciCommandTable
{
	ShciCommandTableElement_t * pTable;
	uint8_t numEntries;
} _shciCommandTable_t;


/************************************************************/

/**
 * @brief Serial Host Command Interface (shci) Initialization.  Creates a new thread to run the shci task
 * 
 * @param[in] uart_num	UART port number to use for Host communication
 * @return ESP_FAIL if Task or Queue cannot be created
 *         ESP_OK if Task and Queue were successfully created
 */
int shci_init(int uart_num);

/**
 * @brief	Deinitialize SHCI - Delete the SHCI Task
 *
 * The SHCI task must be terminated if communications with host processor are to be aborted.
 */
void shci_deinit( void);

/**
 * @brief Register a list of commands with associated callback functions
 *
 * @param[in] pCommandTable Pointer to Command Table
 * @param[in] numEntries Number of entries in Command Table
 * @return true if Command Table was successfully registered, false if error registering table
 */
bool shci_RegisterCommandList( ShciCommandTableElement_t *pCommandTable, uint8_t numEntries );

/**
 * @brief Post a Command Complete Event message to the Message Buffer
 *
 * @param[in]	opCode	Op-code of the completing command
 * @param[in]	error	Error code for the completing command
 */
void shci_postCommandComplete( _CommandOpcode_t opCode, _errorCodeType_t error);

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
bool shci_PostResponse( const uint8_t *pData, size_t numBytes );

#endif /* _SHCI_H_ */
