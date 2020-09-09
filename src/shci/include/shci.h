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
 * @brief Enumerated SHCI Opcodes, consolidates Command and Event opcodes into one enumerated data type
 *
 * SHCI Commands are send by the Host processor and received by the ESP32.
 * SHCI Events are send by the ESP32 and received by the Host processor.
 * SHCI Commands and SHCI Event code should not overlap
 */
enum ShciOpcode
{
	eNoEventOpcode =										0x00,
	eReadLocalInformation =									0x01,		/**< Command: */
	eReadStatus =											0x03,		/**< Command: */
	eReadDeviceName =										0x07,		/**< Command: */
	eWriteDeviceName =										0x08,		/**< Command: */
	eReadAllPairedDeviceInformation =						0x0C,		/**< Command: */

	eWriteScanResponseData =								0x12,		/**< Command: */
	eConnectionParameterUpdateRequest =						0x19,		/**< Command: */
	eSetAdvertisingEnable =									0x1C,		/**< Command: */

	eSendCharacteristicValue =								0x38,		/**< Command: */
	eUpdateCharacteristicValue =							0x39,		/**< Command: */
	eReadLocalCharacteristicValue =							0x3A,		/**< Command: */

	eUserConfirmResponse =									0x41,		/**< Command: */

	eCommunicationsInitialized =							0x50,		/**< Event: Communications channel has been initialized */

	ePasskeyEntryRequest = 									0x60,		/**< Event: */
	ePairingComplete = 										0x61,		/**< Event: */
	ePasskeyConfirmRequest = 								0x62,		/**< Event: */

	eAdvertisingReport = 									0x70,		/**< Event: */
	eLeConnectionComplete = 								0x71,		/**< Event: */
	eDisconnectionComplete = 								0x72,		/**< Event: */
	eConnectionParameterUpdateNotify = 						0x73,		/**< Event: */

	eCommandComplete =										0x80,		/**< Event: */
	eStatusReport =											0x81,		/**< Event: */
	eConfigureModeStatus =									0x8f,		/**< Event: */

	eDiscoverAllPrimaryServiceResponse =					0x90,		/**< Event: */
	eDiscoverSpecificPrimaryServiceCharacteristicResponse =	0x91,		/**< Event: */
	eDiscoverAllCharacteristicDescriptorResponse =			0x92,		/**< Event: */
	eClientWriteCharacteristicValue = 						0x98,		/**< Event: */
	eReceivedTransparentData = 								0x9a,		/**< Event: */

	eWiFiResetProvisioning = 								0xA0,		/**< Command: */
	eWiFiReadStatus =										0xA1,		/**< Command: */

	eWiFiStatus = 											0xB0,		/**< Event: */
	eNetworkInitializedEvent = 								0xB1,		/**< Event: */
	eWifiTestParameter =									0xB2,		/**< Command: */
	eWifiTestStart = 										0xB3,		/**< Command: */
	eWifiTestStop = 										0xB4,		/**< Command: */
	eWifiTestStatus = 										0xB5,		/**< Event: */
	eCaptureArm =											0xB6,		/**< Command: */
	eCaptureRead =											0xB7,		/**< Command: */
	eCaptureComplete = 										0xB8,		/**< Event: */
	eDispenseComplete = 									0xB9,		/**< Event: */
	eWifiConnectAP = 										0xBA,		/**< Command: */
	eEventRecordWriteIndex = 								0xBB,		/**< Event: Update Event Record index (Model-A) */
	eEventRecordData =										0xBC,		/**< Command: Send Event Record data (Model-A) */

	eEspSetSerialNumber = 									0xC0		/**< Command: */

};
typedef uint8_t _shciOpcode_t;

#ifdef	DEPRECIATED
/**
 * @brief Enumerated SHCI Command Opcodes
 *
 * SHCI Commands are send by the Host processor and received by the ESP32.
 * SHCI Commands and SHCI Event code should not overlap
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
 * @brief Enumerated SHCI Event OpCodes
 *
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
#endif		/* DEPRECIATED */

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
 * @brief Callback called when an SHCI command is received
 */
typedef void (* _shciCommandCallback_t)( uint8_t *pData, uint16_t size );
 

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
 * @brief	SHCI Communications Initialized
 *
 * This function should be called after shci_init(), and after all shci command handlers have been registered,
 * to post eCommunicationsInitialized event to queue.
 *
 * Host can use this this event to detect that the ESP has reset, with minimal delay.
 *
 * No additional data is sent with this event.  Ideally the boot partition could be identified and that
 * information relayed to the host, but no method of determining the boot partition (without modifying the
 * bootloader) has been identified.
 */
void shci_communicationsInitialized( void );

/**
 * @brief	Deinitialize SHCI - Delete the SHCI Task
 *
 * The SHCI task must be terminated if communications with host processor are to be aborted.
 */
void shci_deinit( void);

/**
 * @brief	Register an SHCI command
 *
 * Callback function will be called when the command is received
 *
 * @param[in] command  	SHCI command
 * @param[in] handler	Callback function
 */
void shci_RegisterCommand( uint8_t command, _shciCommandCallback_t handler );

/**
 * @brief	Unregister an SHCI command
 *
 * Set Callback function to NULL, default action will be taken when the command is received
 *
 * @param[in] command  	SHCI command
 */
void shci_UnregisterCommand( uint8_t command );

/**
 * @brief Post a Command Complete Event message to the Message Buffer
 *
 * @param[in]	opCode	Op-code of the completing command
 * @param[in]	error	Error code for the completing command
 */
void shci_postCommandComplete( _shciOpcode_t opCode, _errorCodeType_t error);

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
