/**
 * @file	host_ota.c
 *
 *	Over-The-Air (OTA) update of the Host processor firmware.
 *
 *	For Model-A the Host processor is a PIC32MZ1024EFK144
 *	For Model-B the Host processor is a PIC18F57Q43
 *
 *	In all cases the updated firmware will be downloaded to a dedicated <i>pic_fw</i> flash partition
 *	of the ESP32 module.  The firmware image will contain JSON metadata prepended to the image that
 *	contains sufficient information to allow this module to complete the validation and transfer to
 *	the host processor.  Different processor will have widely differing capabilities and thus
 *	require different processes for the validation and transfer.
 *
 * Created on: September 17, 2020
 * Author: Ian Whitehead
 */
#include	<stdlib.h>
#include	<stdio.h>
#include	<stdint.h>
#include	<stdbool.h>
#include	<string.h>
#include	"shci.h"
#include	"mjson.h"
#include	"bleInterface.h"
#include	"bleInterface_internal.h"
#include	"freertos/task.h"
#include	"event_records.h"
#include	"bleGap.h"
#include	"mqtt.h"
#include	"TimeSync.h"
#include	"nvs_utility.h"
#include	"shadow.h"
#include	"esp_partition.h"
#include	"esp_err.h"
#include	"event_notification.h"

/* Platform includes. */
#include	"platform/iot_clock.h"
#include	"platform/iot_threads.h"
#include	"../../../../freertos/vendors/espressif/esp-idf/components/bootloader_support/include_bootloader/bootloader_flash.h"
#include	"../../../../freertos/libraries/3rdparty/mbedtls/include/mbedtls/sha256.h"
#include	"crc16_ccitt.h"

#include	"host_ota.h"
#include	"shadow_updates.h"
#include	"host_ota_pal.h"

#include "aws_iot_ota_agent.h"

/* Debug Logging */
#include "host_ota_logging.h"

#define SwapTwoBytes(data) \
( (((data) >> 8) & 0x00FF) | (((data) << 8) & 0xFF00) )

#define SwapFourBytes(data)   \
( (((data) >> 24) & 0x000000FF) | (((data) >>  8) & 0x0000FF00) | \
  (((data) <<  8) & 0x00FF0000) | (((data) << 24) & 0xFF000000) )

#define	HOST_OTA_STACK_SIZE    ( 3072 )

#define	HOST_OTA_TASK_PRIORITY	( 4 )

#define	FIXED_CONNECTION_HANDLE		(37)
#define	OTA_COMMAND_HANDLE					0x8062					// Dummy

/**
 * @brief	Timeout, in milliseconds, Pend for Update
 *
 * Upon initialization the HostOta task transitions to PendUpdate state.  This allows the OtaUpdate
 * task to complete any update jobs (i.e. return success status to AWS).  This timeout value allows
 * the HostOta task to check for a valid Host processor update image, without having to wait for
 * an image to be down-loaded (i.e. if an image was previously down-loaded).
 */
#define	HOSTOTA_PEND_TIMEOUT_MS	( 5 * 60 * 1000 )

/**
 * @brief	Abstract SHCI Event OpCode to easily swap between legacy BLE characteristics and direct SHCI opcodes
 */
#ifdef	LEGACY_BLE_INTERFACE
#define	OTA_XMIT_OPCODE	eClientWriteCharacteristicValue
#else
#define	OTA_XMIT_OPCODE	eHostUpdateCommand
#endif

/**
 * @brief	Wait for MQTT retry count
 *
 * _hostOtaTask has a 10ms delay, so a value or 500 will give a maximum wait time of 5 seconds.
 * If the counter expires and the MQTT connection has not been established, state-machine will
 * transition to pending on a firmware download.  A couple of status notifications will be lost.
 */
#define	MQTT_WAIT_RETRY_COUNT	500

/**
 * @brief	Host OTA State Machine States
 */
typedef enum
{
	eHostOtaInit,
	eHostOtaIdle,
	eHostOtaParseJSON,
	eHostOtaVerifyImage,
	eHostOtaVersionCheck,
	eHostOtaWaitMQTT,
	eHostOtaPendUpdate,
	eHostOtaUpdateAvailable,
	eHostOtaWaitBootme,
	eHostOtaTransfer,
	eHostOtaActivate,
	eHostOtaWaitReset,
	eHostOtaError
} _hostOtaStates_t;

/**
 * @brief	Update Notification Event Name
 */
//static const char hostOta_notificationEventName[] = "MZ_Update";

/**
 * @brief	Update Available SHCI Event packet
 */
static const uint8_t updateAvailableEvent[] = { eHostUpdateAvailable };

/**
 * @brief	Update Notification Event statuses
 */
typedef	enum
{
	eNotifyWaitForImage,
	eNotifyDownload,
	eNotifyImageVerification,
	eNotifyFlashErase,
	eNotifyFlashProgram,
	eNotifyUpdateValidation,
	eNotifyHostReset,
	eNotifyUpdateSuccess,
	eNotifyUpdateFailed
} hostOta_notification_t;

static const char *NotificationMessage[] =
{
	[ eNotifyWaitForImage ]			= "Waiting for update",
	[ eNotifyDownload ]				= "Downloading image",
	[ eNotifyImageVerification ]	= "Verifying image",
	[ eNotifyFlashErase ]			= "Erasing Flash",
	[ eNotifyFlashProgram ]			= "Programming Flash",
	[ eNotifyUpdateValidation ]		= "Validating Update",
	[ eNotifyHostReset ]			= "Resetting Host Processor",
	[ eNotifyUpdateSuccess ]		= "Update complete",
	[ eNotifyUpdateFailed ]			= "Update failed"
};


/**
 * @brief	SHA256 Hash Value type definition (includes null-terminator)
 */
typedef	struct
{
	uint8_t	x[32];
	uint8_t	terminator;
}	sha256_t;

/**
 * @brief	PIC32MZ Transfer State-machine states
 */
typedef enum
{
	mzXfer_Idle,
	mzXfer_Init,
	mzXfer_Init_Ack,
	mzXfer_Erase,
	mzXfer_Erase_Ack,
	mzXfer_Data,
	mzXfer_Data_Ack,
	mzXfer_Verify,
	mzXfer_Verify_Ack,
	mzXfer_Reset,
	mzXfer_Complete,
	mzXfer_Error
} _mzXfer_state_t;

/**
 * @brief	Command/Status definitions for Host transfer
 */
enum _otaOpcode
{
	eOTA_INIT_CMD =			0x01,
	eOTA_ERASE_CMD =		0x02,
	eOTA_DATA_CMD =			0x03,
	eOTA_VERIFY_CMD =		0x04,
	eOTA_RESET_CMD =		0x05,
	eBL_VERSION_CMD =		0x06,
//	eBL_FLASH_READ_CMD =	0x07,
	eBL_FLASH_WRITE_CMD =	0x08,
	eBL_FLASH_ERASE_CMD =	0x09,
//	eBL_EEPROM_READ_CMD =	0x0A,
//	eBL_EEPROM_WRITE_CMD =	0x0B,
//	eBL_CONFIG_READ_CMD =	0x0C,
	eBL_CONFIG_WRITE_CMD =	0x0D,
	eBL_CALC_CRC_CMD =		0x0E,

	eOTA_INIT_ACK =			0x41,
	eOTA_ERASE_ACK =		0x42,
	eOTA_DATA_ACK =			0x43,
	eOTA_VERIFY_ACK =		0x44,
	eBL_RESET_ACK =			0x45,
	eBL_VERSION_ACK =		0x46,
//	eBL_FLASH_READ_ACK =	0x47,
	eBL_FLASH_WRITE_ACK =	0x48,
	eBL_FLASH_ERASE_ACK =	0x49,
//	eBL_EEPROM_READ_ACK =	0x4A,
//	eBL_EEPROM_WRITE_ACK =	0x4B,
//	eBL_CONFIG_READ_ACK =	0x4C,
	eBL_CONFIG_WRITE_ACK =	0x4D,
	eBL_CALC_CRC_ACK =		0x4E,

	eBL_BOOTME =			0x62,

	eOTA_INIT_NACK =		0x81,
	eOTA_ERASE_NACK =		0x82,
	eOTA_DATA_NACK =		0x83,
	eOTA_VERIFY_NACK =		0x84,
	eOTA_RESET_NACK =		0x85,
	eBL_VERSION_NACK =		0x86,
//	eBL_FLASH_READ_NACK =	0x87,
	eBL_FLASH_WRITE_NACK =	0x88,
	eBL_FLASH_ERASE_NACK =	0x89,
//	eBL_EEPROM_READ_NACK =	0x8A,
//	eBL_EEPROM_WRITE_NACK =	0x8B,
//	eBL_CONFIG_READ_NACK =	0x8C,
	eBL_CONFIG_WRITE_NACK =	0x8D,
	eOTA_CRC_NACK =			0x8E,
	eOTA_UNKNOWN_NACK =		0x8F
};
typedef	uint8_t	_otaOpcode_t;

/**
 * @brief OTA Response Error Codes for PIC32MZ transfer
 */
enum _otaStatus
{
	eOTA_NO_ERR =				0x00,
	eOTA_CRYPTO_ERR =			0x01,				/**< Crypto Engine engine not present */
	eOTA_NO_KEYS_ERR =			0x02,				/**< Crypto Keys missing */
	eOTA_CMD_SEQ_ERR =			0x03,				/**< Init command not received (i.e. command out of sequence) */
	eOTA_ERASE_TO_ERR =			0x04,				/**< Erase Time-out */
	eOTA_ADDRESS_ERR =			0x05,				/**< Data Address field out of range */
	eOTA_DECRYPT_ERR =			0x06,				/**< Decryption time-out error */
	eOTA_WRITE_TO_ERR =			0x07,				/**< Write Time-out */
	eOTA_WRITE_COMP_ERR =		0x08,				/**< Write Compare Error */
	eOTA_IMAGE_START_ERR =		0x09,				/**< Verify Image Start Address Error */
	eOTA_IMAGE_LEN_ERR =		0x0A,				/**< Verify Image Length Error */
	eOTA_HASH_TO_ERR =			0x0B,				/**< Compute Hash Time-out Error */
	eOTA_HASH_ERR =				0x0C,				/**< Hash Values don't match */
	eOTA_CMD_LEN_ERR =			0x0D,				/**< Unexpected Command Length */
	eOTA_UNKN_CMD_ERR =			0x0E,				/**< Unknown Command */
	eOTA_CRC_ERR =				0x0F				/**< CRC Error */
};
typedef uint8_t	_otaStatus_t;


/**
 * @brief Revision ID & Device ID
 *
 * For PIC18F17Q43: Read-only data located @ 3FFFFC - 3FFFFF
 */
typedef union
{
	struct
	{
		uint16_t		revision;
		uint16_t		id;
	};
	uint8_t			buff[ 4 ];
} device_t;

/**
 * @brief Device Configuration Information
 *
 * For PIC18F17Q43: Read-only data located @ 3C0000 - 3C0009
 */
typedef union
{
	struct
	{
		uint16_t		erasePageSize;
		uint16_t		writeLatches;
		uint16_t		erasablePages;
		uint16_t		eepromSize;
		uint16_t		pinCount;
	};
	uint8_t			buff[ 10 ];
} dci_t;

#define	NUM_DEVID_BYTES	( sizeof( device_t ) )
#define	NUM_DCI_BYTES		( sizeof( dci_t ) )
#define	NUM_CONFIG_BYTES	10
#define	NUM_MUI_BYTES		18

/**
 *	@brief	Flash Address data type
 */
typedef union
{
	struct
	{
		uint8_t			address_L;
		uint8_t			address_H;
		uint8_t			address_U;
		uint8_t			address_E;
	};
	uint32_t			address32;
	struct
	{
		uint8_t			addr_L;
		uint16_t		address_Page;
	};
} flash_address_t;

/**
 * @brief	OTA Transfer Packet Length
 *
 * The MZ decryption engine operates on block that are multiples of 64 bytes in length.
 * Legacy protocol limited transfer length to 128 bytes, due to BLE limitations.
 * In Command structure, length is type uint8_t so limits total packet length to 255 bytes.
 * Increasing maximum packet data length to 192 reduced the number of packets and thus
 * speeds up transfer time.
 *
 * The PIC18F57Q43 has 256 byte flash pages, so transfers of multiples of 256 bytes simplifies
 * processing on the host.  To support transfers larger than 192 bytes, the protocol was expanded
 * to allow for an option 2nd byte of the length field.  MZ transfers could utilize the updated
 * protocol, to reduce the programming time.
 */
#define	OTA_PKT_DLEN_64		64						/**< Bootloader Transfer Packet Data Length */
#define	OTA_PKT_DLEN_128	( OTA_PKT_DLEN_64 * 2 )	/**< Bootloader Transfer Packet Data Length */
#define	OTA_PKT_DLEN_192	( OTA_PKT_DLEN_64 * 3 )	/**< Bootloader Transfer Packet Data Length */
#define	OTA_PKT_DLEN_256	( OTA_PKT_DLEN_64 * 4 )	/**< Bootloader Transfer Packet Data Length */

#define	MAX_OTA_PKT_DLEN	( OTA_PKT_DLEN_192 )	/**< Maximum data length to be used */

/**
 * @brief	OTA Command packet definition for Host transfer
 */
typedef struct {
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	command;						/**< Command OpCode */
	uint16_t		uid;							/**< 16-bit unique ID */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaInit_t;				/**< Init Command */

typedef struct {
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	command;						/**< Command OpCode */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaErase_t;			/**< Erase Command */

typedef struct {
	uint16_t		length;							/**< Number of bytes in transfer packet (msb-first), including these bytes */
	_otaOpcode_t	command;						/**< Command OpCode */
	uint32_t		address;						/**< Address */
	uint8_t			buffer[ OTA_PKT_DLEN_256 ];		/**< Data Buffer */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaPageData_t;			/**< Data Command */

typedef struct {
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	command;						/**< Command OpCode */
	uint32_t		address;						/**< Address */
	uint8_t			buffer[ OTA_PKT_DLEN_192 ];		/**< Data Buffer */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaLongData_t;			/**< Data Command */

typedef struct {
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	command;						/**< Command OpCode */
	uint32_t		address;						/**< Address */
	uint8_t			buffer[ OTA_PKT_DLEN_128 ];		/**< Data Buffer */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaData_t;				/**< Data Command */

typedef struct {
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	command;						/**< Command OpCode */
	uint32_t		address;						/**< Address */
	uint8_t			buffer[ OTA_PKT_DLEN_64 ];		/**< Data Buffer */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaShortData_t;		/**< Data Command */

typedef struct {
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	command;						/**< Command OpCode */
	uint32_t		startAddr;						/**< Start Address of image */
	uint32_t		imgLength;						/**< Image Length */
	uint8_t			sha256hash[ 32 ];				/**< 256-bit SHA256 Hash Code */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaVerify_t;			/**< Verify Command */

typedef struct {
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	command;						/**< Command OpCode */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaReset_t;			/**< Reset Command */


typedef struct {
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	command;						/**< Command OpCode */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _blOpcode_t;			/**< OpCode only Command */

typedef struct {
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	command;						/**< Command OpCode */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _blVersion_t;			/**< Version Command */

typedef struct {
	uint16_t		length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	command;						/**< Command OpCode */
	uint8_t			EE_key_1;						/**< unlock Key, 1st byte */
	uint8_t			EE_key_2;						/**< unlock Key, 2nd byte */
	flash_address_t	addr;							/**< 32-bit address */
	uint8_t			buffer[ OTA_PKT_DLEN_256 ];		/**< data buffer to be written */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _blFlashWrite_t;		/**< Flash Write Command */

typedef struct {
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	command;						/**< Command OpCode */
	uint8_t			EE_key_1;						/**< unlock Key, 1st byte */
	uint8_t			EE_key_2;						/**< unlock Key, 2nd byte */
	flash_address_t	addr;							/**< 32-bit address */
	uint16_t		page_count;						/**< number of pages to erase */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _blFlashErase_t;		/**< Flash Erase Command */

typedef struct {
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	command;						/**< Command OpCode */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _blConfigWrite_t;		/**< Config Write Command */

typedef struct {
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	command;						/**< Command OpCode */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _blCalcCrc_t;			/**< Calculate CRC Command */


typedef struct
{
	_shciOpcode_t		opCode;						/**< SHCI OpCode */
	uint8_t				connHandle;					/**< Connection Handle */
	uint16_t			charHandle;					/**< BLE Characteristic Handle */
	union
	{
		_otaInit_t		Init;						/**< Init Command */
		_otaErase_t		Erase;						/**< Erase Command */
		_otaPageData_t	PageData;					/**< Data Command, 256 bytes */
		_otaLongData_t	LongData;					/**< Data Command, 192 bytes */
		_otaData_t		Data;						/**< Data Command, 128 bytes */
		_otaShortData_t	ShortData;					/**< Data Command, 64 bytes */
		_otaVerify_t	Verify;						/**< Verify Command */
		_blVersion_t	Version;					/**< Version Command */
		_blFlashWrite_t	FlashWrite;					/**< Flash Write Command */
		_blFlashErase_t	FlashErase;					/**< Flash Erase Command */
		_blConfigWrite_t ConfigWrite;				/**< Config Write Command */
		_blCalcCrc_t	CalcCrc;					/**< Calculate CRC Command */
		_otaReset_t		Reset;						/**< Reset Command */
	};
}  __attribute__ ((packed)) _otaCommand_t;

typedef struct
{
	_shciOpcode_t		opCode;						/**< SHCI OpCode */
	union
	{
		_blOpcode_t		OpCodeOnly;					/**< OpCode only command (Version, Calculate CRC ... ) */
		_blVersion_t	Version;					/**< Version Command */
		_blFlashWrite_t	FlashWrite;					/**< Flash Write Command */
		_blFlashErase_t	FlashErase;					/**< Flash Erase Command */
		_blConfigWrite_t ConfigWrite;				/**< Config Write Command */
		_blCalcCrc_t	CalcCrc;					/**< Calculate CRC Command */
	};
}  __attribute__ ((packed)) _blCommand_t;

/**
 * @brief	Host OTA Status protocol
 */
typedef struct
{
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	status;							/**< Status OpCode */
	uint8_t			data[ 9 ];						/**< Data */
}  __attribute__ ((packed)) _otaGenericStatus_t;	/**< Generic Status */

typedef struct
{
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	status;							/**< Status OpCode */
	uint8_t			error;							/**< Error Code */
	uint16_t		uid;							/**< Unique ID */
	uint32_t		nextAddr;						/**< Next Write Address */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaInitStatus_t;		/**< Init Status */

typedef struct
{
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte (51 bytes) */
	_otaOpcode_t	status;							/**< Status OpCode */
	uint8_t			error;							/**< Error Code */
	uint8_t			versionMinor;					/**< Firmware version, minor */
	uint8_t			versionMajor;					/**< Firmware version, major */
	uint16_t		maxPacketSize;					/**< Maximum packet size */
	device_t		device;							/**< Device ID and Revision */
	dci_t			dci;							/**< Device Configuration Information */
	uint8_t			config[ NUM_CONFIG_BYTES ];		/**< Configuration Bytes */
	uint8_t			mui[ NUM_MUI_BYTES ];			/**< Microchip Unique ID */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _blVersionStatus_t;		/**< Version Status */

typedef struct
{
	uint8_t			length;								/**< Command length: 9 bytes */
	_otaOpcode_t	command;							/**< OpCode */
	_otaStatus_t	error;								/**< Error */
	uint32_t		address;							/**< Start Flash Address */
	uint16_t		crc;								/**< command CRC */
}  __attribute__ ((packed)) _blFlashWriteStatus_t;		/**< Flash Write Status */

typedef struct
{
	uint8_t			length;							/**< Command length: 9 bytes */
	_otaOpcode_t	command;						/**< OpCode */
	_otaStatus_t	error;							/**< Error */
	uint16_t		value;							/**< Calculated CRC16 Value */
	uint16_t		crc;							/**< command CRC */
}  __attribute__ ((packed)) _blCalcCrcStatus_t;		/**< Calculate CRC Status */

typedef struct
{
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	status;							/**< Status OpCode */
	uint8_t			error;							/**< Error Code */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaAckStatus_t;		/**< Generic (5-bytes) ACK/NAK Status */

typedef struct
{
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	status;							/**< Status OpCode */
	uint8_t			error;							/**< Error Code */
	uint32_t		Addr;							/**< Current Write Address */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaDataStatus_t;		/**< Data Status */

typedef struct
{
	uint8_t			length;							/**< Number of bytes in transfer packet, including this byte */
	_otaOpcode_t	status;							/**< Status OpCode */
	uint16_t		crc;							/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaNakStatus_t;		/**< Simple (4-byte) NAK Status */

typedef union
{
	_otaGenericStatus_t	Generic;					/**< Generic Status */
	_otaInitStatus_t	Init;						/**< Init Status */
	_otaAckStatus_t		Ack;						/**< Generic (5-bytes) ACK/NAK Status */
	_otaDataStatus_t	Data;						/**< Data Status */
	_otaNakStatus_t		Nak;						/**< Simple (4-byte) NAK Status */
} _otaStatusPkt_t;

typedef struct
{
	uint8_t				length;						/**< Command length: 5 bytes */
	_otaOpcode_t		command;					/**< OpCode: "b" */
	uint8_t				data[ 5 ];					/**< Data: "ootme" */
	uint16_t			crc;						/**< command CRC */
}  __attribute__ ((packed)) _bootme_t;

typedef	enum
{
	eStep_Initialize,
	eStep_Version,
	eStep_Erase,
	eStep_Write,
	eStep_Verify,
	eStep_Reset,
	eStep_Complete
} _eSteps_t;

/**
 * @brief Step-wise Image Transfer states
 */
typedef enum
{
	eXfer_command,
	eXfer_response,
	eXfer_continue,
	eXfer_complete,
	eXfer_error
} _xferState_t;

/**
 * @brief Step-wise Image Transfer status
 */
typedef enum
{
	eXferStat_NoError,
	eXferStat_CrcError,
	eferStat_Timeout
} _xferStatus_t;

/**
 * @brief	Opaque Image Transfer Step structure
 */
typedef	struct _blStep_t	_blStep_t;

/**
 * @brief	Host OTA control structure
 */
typedef struct
{
	TaskHandle_t		taskHandle;												/**< handle for Event Record Task */
	_hostOtaStates_t	state;
    esp_partition_t 	*partition;
	uint32_t			PaddingBoundary;
	uint32_t			LoadAddress;
	uint32_t			ImageSize;
	uint32_t			Offset;
	double				Version_MZ;
	double				Version_PIC;
	sha256_t			sha256Plain;
	sha256_t			sha256Encrypted;
	uint16_t			crc16_ccitt;											/**< Metadata Image CRC value */
	double				currentVersion_MZ;										/**< Currently installed/running MZ firmware version */
	double				currentVersion_PIC;										/**< Currently installed/running PIC firmware version */
	_mzXfer_state_t		mzXfer_state;
	void		*pXferBuf;
	_otaOpcode_t		expectedStatus;
	bool				ackReceived;
	uint32_t			targetAddress;
	uint32_t			startAddress;
	uint16_t			uid;
	bool				bStartTransfer;
	IotSemaphore_t		iotUpdateSemaphore;
	OTA_PAL_ImageState_t	imageState;
	double				percentComplete;
	int					lastPercentComplete;
	_hostOtaNotifyCallback_t	notifyHandler;
	int					waitMQTTretry;
	size_t				imageAddress;				/**< Current offset, from start of Flash Partition, used to read image block */
	size_t				bytesRemaining;				/**< Number of bytes remaining to be transfered */
	size_t				transferSize;				/**< Size, in bytes, of the current transfer block */
	_blStep_t			*pStep;						/**< Pointer to current ImageTransfer step */
	bool				bBootme;					/**< true if BootMe message received */
	uint16_t			calc_crc;					/**< CRC value calculated by host */
	_xferStatus_t		xferStatus;
} host_ota_t;

typedef size_t ( * _function_t )( host_ota_t *pHost, _otaOpcode_t opcode );
typedef bool ( * _ackFunction_t )( host_ota_t *pHost );
typedef esp_err_t ( * _statusFunction_t )( const void * pData );

struct _blStep_t
{
	_function_t			command;
	_otaOpcode_t		opCode;
	_otaOpcode_t		expectedStatus;
	_ackFunction_t		onAcknowledge;
	_xferState_t		stateAfterCommand;
	_eSteps_t			nextStep;
};

typedef struct
{
	uint8_t				opcode;
	uint8_t				length;
	_statusFunction_t	onStatus;						/**< function to be called upon receipt of an OTA Status packet, optional */
} _otaStatusEntry_t;


static host_ota_t _hostota =
{
	.state = eHostOtaInit,													/**< Start in Init state */
	.bBootme = false,
};

/**
 * @brief	OTA PAL functions
 *
 * This table contains the abstraction layer functions specific to the HostOta module.
 * A pointer to this table is passed to the OtaUpdate module, so it can call these
 * functions when the Update Job ServerFileId is set for a Host Update.
 */
static AltProcessor_Functions_t hostOtaFunctions =
{
    .xAbort                    = hostOta_Abort,
    .xActivateNewImage         = hostOta_ActivateNewImage,
    .xCloseFile                = hostOta_CloseFile,
    .xCreateFileForRx          = hostOta_CreateFileForRx,
    .xGetImageState            = hostOta_getImageState,
    .xResetDevice              = hostOta_ResetDevice,
    .xSetImageState            = hostOta_setImageState,
    .xWriteBlock               = hostOta_WriteBlock,
};


/**
 * @brief	Post notification of Host OTA Update Status
 *
 * This function could post the notification to the device shadow, or to the MQTT Event topic.
 * The basic notification message would be the same in either case, the function to perform the
 * MQTT publish is different.
 *
 * Notification messages include:
 *  - Waiting for Image
 * 	- Downloading
 * 	- Image Verification
 * 	- Flash Erase
 * 	- Flash Program x%
 * 	- Update Validation
 * 	- Update Complete vN.NN
 */
static void hostOtaNotificationUpdate( hostOta_notification_t notify, double param )
{
	char * jsonBuffer = NULL;
	int	n = 0;
	int percent;

	/* Format Notification update */
	switch( notify )
	{
		case eNotifyWaitForImage:
		case eNotifyDownload:
		case eNotifyImageVerification:
		case eNotifyFlashErase:
		case eNotifyUpdateValidation:
		case eNotifyHostReset:
		case eNotifyUpdateFailed:
			n = mjson_printf( &mjson_print_dynamic_buf, &jsonBuffer, "{%Q:{%Q:%Q}}",
					eventNotification_getSubject( eEventSubject_PicUpdate ), "State", NotificationMessage[ notify ] );
			break;

		case eNotifyFlashProgram:
			percent = ( int ) param;
			if( percent != _hostota.lastPercentComplete )
			{
				_hostota.lastPercentComplete = percent;
				n = mjson_printf( &mjson_print_dynamic_buf, &jsonBuffer, "{%Q:{%Q:%Q, %Q:%d}}",
						eventNotification_getSubject( eEventSubject_PicUpdate ), "State", NotificationMessage[ notify ], "percent", percent );
			}
			break;

		case eNotifyUpdateSuccess:
			n = mjson_printf( &mjson_print_dynamic_buf, &jsonBuffer, "{%Q:{%Q:%Q, %Q:%f}}",
					eventNotification_getSubject( eEventSubject_PicUpdate ), "State", NotificationMessage[ notify ], "version", param  );
			break;

		default:
			break;
	}

	if( n )
	{
		IotLogDebug( "hostOta notify: %s", jsonBuffer );

		/* Call notification handler, if one has been registered */
		if( NULL != _hostota.notifyHandler )
		{
			_hostota.notifyHandler( jsonBuffer );
		}

		/* Free buffer */
		vPortFree( jsonBuffer );
	}
}

/**
 * @brief	Process	OTA Status: INIT_ACK
 */
static esp_err_t onStatus_InitAck( const void * pData )
{
	esp_err_t	err = ESP_OK;
	const _otaInitStatus_t * pInit = pData;
	uint32_t	address;

	if( pInit->uid == _hostota.uid )
	{
		address = SwapFourBytes( pInit->nextAddr );

		/* If next address in INIT_ACK is in valid range (for image), adjust to give offset from start of image */
		if( ( address >= _hostota.LoadAddress ) && ( address < ( _hostota.LoadAddress + _hostota.ImageSize ) ) )
		{
			_hostota.startAddress = address - _hostota.LoadAddress;
		}
		else
		{
			_hostota.startAddress = 0;
		}
		IotLogDebug( "OTA_INIT_ACK: UID matches, address = %08X, startAddress = %08X", address, _hostota.startAddress );
	}
	else
	{
		IotLogError( "Error: UID mis-match" );
		err = ESP_FAIL;
	}
	return( err );
}


/**
 * @brief	Process	OTA Status: DATA_ACK
 */
static esp_err_t onStatus_DataAck( const void * pData )
{
	esp_err_t	err = ESP_OK;
	const _otaDataStatus_t * pDat = pData;

	if( SwapFourBytes( pDat->Addr ) == _hostota.targetAddress )
	{
		IotLogDebug( "Target Address matches" );
	}
	else
	{
		IotLogError( "Error: Target Address mis-match" );
		err = ESP_FAIL;
	}
	return( err );
}

/**
 * @brief	Process	OTA Status: VERSION_ACK
 */
static esp_err_t onStatus_VersionAck( const void * pData )
{
	esp_err_t	err = ESP_OK;
	const _blVersionStatus_t * pVersion = pData;

	IotLogInfo( "Firmware version = %d.%02d", pVersion->versionMajor, pVersion->versionMinor );
	IotLogInfo( "Device ID/Rev = %04X/%04X", pVersion->device.id, pVersion->device.revision );
	IotLogInfo( "Max packet size = %d", pVersion->maxPacketSize );

	return( err );
}

/**
 * @brief	Process	OTA Status: FLASH_WRITE_ACK
 *
 * Flash Write Ack returns target address in little endian format (same as it was packed)
 */
static esp_err_t onStatus_FlashWriteAck( const void * pData )
{
	esp_err_t	err = ESP_OK;
	const _blFlashWriteStatus_t * pWrite = pData;

	if( pWrite->address == _hostota.targetAddress )
	{
		IotLogDebug( "Target Address matches" );
	}
	else
	{
		IotLogError( "Error: Target Address mis-match: %08X vs. %08X", pWrite->address, _hostota.targetAddress );
		err = ESP_FAIL;
	}
	return( err );
}

/**
 * @brief	Process	OTA Status: BOOTME
 */
static esp_err_t onStatus_Bootme( const void * pData )
{
	esp_err_t	err = ESP_OK;
	IotLogInfo( "Bootme" );

	_hostota.bBootme = true;

	return( err );
}

/**
 * @brief	Calculate CRC Status
 */
static esp_err_t onStatus_CalcCrcAck( const void * pData )
{
	esp_err_t	err = ESP_OK;
	const _blCalcCrcStatus_t *pCrc = pData;

	IotLogInfo( "CalcCRC Ack: %04X vs. %04X", pCrc->value, _hostota.crc16_ccitt );

	/* Save calculated CRC */
	_hostota.calc_crc = pCrc->value;

	return( err );
}

/**
 * @brief	OTA Status Table
 *
 * This table lists the OpCode and payload size for all valid status packets
 */
const static _otaStatusEntry_t otaStatusTable[] =
{
	{ .opcode = eOTA_INIT_ACK,			.length = ( uint8_t ) sizeof( _otaInitStatus_t ),		.onStatus = &onStatus_InitAck },
	{ .opcode = eOTA_ERASE_ACK,			.length = ( uint8_t ) sizeof( _otaAckStatus_t ),		.onStatus = NULL },
	{ .opcode = eOTA_DATA_ACK,			.length = ( uint8_t ) sizeof( _otaDataStatus_t ),		.onStatus = &onStatus_DataAck },
	{ .opcode = eOTA_VERIFY_ACK,		.length = ( uint8_t ) sizeof( _otaAckStatus_t ),		.onStatus = NULL },
	{ .opcode = eBL_RESET_ACK,			.length = ( uint8_t ) sizeof( _otaAckStatus_t ),		.onStatus = NULL },
	{ .opcode = eBL_VERSION_ACK,		.length = ( uint8_t ) sizeof( _blVersionStatus_t ),		.onStatus = &onStatus_VersionAck },
//	{ .opcode = eBL_FLASH_READ_ACK,		.length = ( uint8_t ) sizeof( _otaAckStatus_t ),		.onStatus = NULL },
	{ .opcode = eBL_FLASH_WRITE_ACK,	.length = ( uint8_t ) sizeof( _blFlashWriteStatus_t ),	.onStatus = &onStatus_FlashWriteAck },
	{ .opcode = eBL_FLASH_ERASE_ACK,	.length = ( uint8_t ) sizeof( _otaAckStatus_t ),		.onStatus = NULL },
//	{ .opcode = eBL_EEPROM_READ_ACK,	.length = ( uint8_t ) sizeof( _otaAckStatus_t ),		.onStatus = NULL },
//	{ .opcode = eBL_EEPROM_WRITE_ACK,	.length = ( uint8_t ) sizeof( _otaAckStatus_t ),		.onStatus = NULL },
//	{ .opcode = eBL_CONFIG_READ_ACK,	.length = ( uint8_t ) sizeof( _otaAckStatus_t ),		.onStatus = NULL },
	{ .opcode = eBL_CONFIG_WRITE_ACK,	.length = ( uint8_t ) sizeof( _otaAckStatus_t ),		.onStatus = NULL },
	{ .opcode = eBL_CALC_CRC_ACK,		.length = ( uint8_t ) sizeof( _blCalcCrcStatus_t ),		.onStatus = &onStatus_CalcCrcAck },

	{ .opcode = eBL_BOOTME,				.length = ( uint8_t ) sizeof( _bootme_t ),				.onStatus = &onStatus_Bootme },

	{ .opcode = eOTA_INIT_NACK,			.length = ( uint8_t ) sizeof( _otaNakStatus_t ),		.onStatus = NULL },
	{ .opcode = eOTA_ERASE_NACK,		.length = ( uint8_t ) sizeof( _otaNakStatus_t ),		.onStatus = NULL },
	{ .opcode = eOTA_DATA_NACK,			.length = ( uint8_t ) sizeof( _otaNakStatus_t ),		.onStatus = NULL },
	{ .opcode = eOTA_VERIFY_NACK,		.length = ( uint8_t ) sizeof( _otaNakStatus_t ),		.onStatus = NULL },
	{ .opcode = eOTA_RESET_NACK,		.length = ( uint8_t ) sizeof( _otaNakStatus_t ),		.onStatus = NULL },
	{ .opcode = eBL_VERSION_NACK,		.length = ( uint8_t ) sizeof( _otaNakStatus_t ),		.onStatus = NULL },
//	{ .opcode = eBL_FLASH_READ_NACK,	.length = ( uint8_t ) sizeof( _otaNakStatus_t ),		.onStatus = NULL },
	{ .opcode = eBL_FLASH_WRITE_NACK,	.length = ( uint8_t ) sizeof( _otaNakStatus_t ),		.onStatus = NULL },
	{ .opcode = eBL_FLASH_ERASE_NACK,	.length = ( uint8_t ) sizeof( _otaNakStatus_t ),		.onStatus = NULL },
//	{ .opcode = eBL_EEPROM_READ_NACK,	.length = ( uint8_t ) sizeof( _otaNakStatus_t ),		.onStatus = NULL },
//	{ .opcode = eBL_EEPROM_WRITE_NACK,	.length = ( uint8_t ) sizeof( _otaNakStatus_t ),		.onStatus = NULL },
//	{ .opcode = eBL_CONFIG_READ_NACK,	.length = ( uint8_t ) sizeof( _otaNakStatus_t ),		.onStatus = NULL },
	{ .opcode = eBL_CONFIG_WRITE_NACK,	.length = ( uint8_t ) sizeof( _otaNakStatus_t ),		.onStatus = NULL },
	{ .opcode = eOTA_CRC_NACK,			.length = ( uint8_t ) sizeof( _otaAckStatus_t ),		.onStatus = NULL },
	{ .opcode = eOTA_UNKNOWN_NACK,		.length = ( uint8_t ) sizeof( _otaAckStatus_t ),		.onStatus = NULL }
};

#define	NUM_STATUS_ENTRIES	( sizeof( otaStatusTable ) / sizeof( _otaStatusEntry_t ) )

/**
 * @brief	OTA Status update handler
 *
 * This handler will be called whenever:
 *	+ The OTA Status BLE characteristic value is updated [Legacy BLE Characteristic configuration]
 *	+ The SHCI HostUpdateResponse command is received
 *
 * @param[in]	pData	Pointer to characteristic value
 * @param[in]	size	Size, in bytes, of characteristic value
 */
static void vOtaStatusUpdate( const uint8_t *pData, const uint16_t size )
{
	esp_err_t	err = ESP_OK;
	int i;
	uint16_t	crc;
	const _otaStatusPkt_t	*pStat = ( const _otaStatusPkt_t * )pData;
	_otaStatusEntry_t * pStatusEntry;

	// Validate the Status packet
	for( i = 0, pStatusEntry = &otaStatusTable[ 0 ]; i < NUM_STATUS_ENTRIES; ++i, ++pStatusEntry )
	{
		if( ( pStat->Generic.status == pStatusEntry->opcode ) && ( pStat->Generic.length == pStatusEntry->length ) )
		{
			IotLogDebug( "Found Status Entry, length matches" );
			/* Calculate CRC on received packet */
			crc = crc16_ccitt_compute( ( uint8_t * )pStat, ( pStat->Generic.length ) );
			if( crc )
			{
				IotLogError( "Error: Non-zero CRC" );
				err = ESP_FAIL;
			}
			else
			{
				if( pStat->Generic.status == _hostota.expectedStatus )
				{
					IotLogDebug( "Expected Status Received ");

					/* Call onStatus function, if present */
					if( NULL != pStatusEntry->onStatus)
					{
						err = pStatusEntry->onStatus( pStat );
					}

					/* Set Acknowledge received flag */
					_hostota.ackReceived = true;
				}
				else if( pStatusEntry->opcode == eBL_BOOTME )				/* BootMe is asynchronous, not a response */
				{
					/* Call onStatus function, if present */
					if( NULL != pStatusEntry->onStatus)
					{
						err = pStatusEntry->onStatus( pStat );
					}
				}
				else
				{
					IotLogError( "Error: Unexpected Status received %02X",  pStat->Generic.status );
					err = ESP_FAIL;
				}
			}
			/* Break out of loop if opcode and length match */
			break;
		}
	}

	/* If anything goes wrong, or response not found in table, change to error state */
	if( ( ESP_OK != err ) || ( NUM_STATUS_ENTRIES <= i ) )
	{
		_hostota.mzXfer_state = mzXfer_Error;
#ifndef	LEGACY_BLE_INTERFACE
		/* NACK the command */
		shci_postCommandComplete( eHostUpdateResponse, eInvalidCommandParameters  );
	}
	else
	{
		/* ACK the command */
		shci_postCommandComplete( eHostUpdateResponse, eCommandSucceeded );
#endif
	}
}


/**
 * @brief	Print SHA256 hash value as a string with a tag - Debug Only
 *
 * buffer size:
 * 	Const tag length = 6
 * 	If max tag length = 9
 * 	Hash length = 32 * 3 = 96
 * 	terminator = 1
 * 	Total length = 6 + 9 + 96 + 1 = 112
 *
 */
static void printsha256( const char *tag, sha256_t *sha)
{
	int i;
	int n;
	int index = 0;
	char buffer[ 120 ];
	int	remaining = sizeof( buffer );

	n = snprintf( &buffer[ index ], remaining, "SHA256%s: ", tag );
	for (i = 0; i < 32; ++i)
	{
		index += n;
		remaining -= n;
		n = snprintf( &buffer[ index], remaining, "%02x", sha->x[ i ] );
	}

	IotLogDebug( buffer );
}


/**
 * @brief	Append CRC16-CCITT value to data buffer
 *
 * CRC value is saved in big endian format (MSB first)
 */
static void appendCRC( uint8_t *pData, size_t size )
{
	uint16_t		crc;

	/* compute CRC */
	crc = crc16_ccitt_compute( pData, size );

	/* append CRC after data */
	*( uint16_t *)&pData[ size ] = SwapTwoBytes( crc );
}

/**
 * @brief	Pack Init Command
 *
 * @param[out]	pCommand	Pointer to command buffer
 * @param[in]	uid			Psuedo random Unique Identifier
 * @return  SHCI command payload length (command size + 4, for the SHCI header)
 */
static size_t packInitCmnd( _otaCommand_t *pCommand, uint16_t uid )
{

	pCommand->opCode = OTA_XMIT_OPCODE;
	pCommand->connHandle = FIXED_CONNECTION_HANDLE;
	pCommand->charHandle = SwapTwoBytes( OTA_COMMAND_HANDLE );

	pCommand->Init.command = eOTA_INIT_CMD;
	pCommand->Init.length = sizeof( _otaInit_t );
	pCommand->Init.uid = uid;

	appendCRC( ( uint8_t *) &pCommand->Init, ( sizeof( _otaInit_t ) - 2 ) );

	return( sizeof( _otaInit_t ) + 4 );

}


/**
 * @brief	Pack Erase Command
 *
 * @param[out]	pCommand	Pointer to command buffer
 * @return  SHCI command payload length (command size + 4, for the SHCI header)
 */
static size_t packEraseCmnd( _otaCommand_t *pCommand )
{
	pCommand->opCode = OTA_XMIT_OPCODE;
	pCommand->connHandle = FIXED_CONNECTION_HANDLE;
	pCommand->charHandle = SwapTwoBytes( OTA_COMMAND_HANDLE );

	pCommand->Erase.command = eOTA_ERASE_CMD;
	pCommand->Erase.length = sizeof( _otaErase_t );

	appendCRC( ( uint8_t *) &pCommand->Erase, ( sizeof( _otaErase_t ) - 2 ) );

	return( sizeof( _otaErase_t ) + 4 );
}


/**
 * @brief	Pack Data Command
 *
 *	Read data from NVS partition and format as a Data command
 *
 * @param[out]	pCommand	Pointer to command buffer
 * @param[in]	partition		Pointer to partition information structure
 * @param[in]	imageAddress	Offset within partition
 * @param[in]	size			Number of data bytes to be read and packed
 * @param[in]	targetAddress	Host memory address, where data is to be programmed
 * @return  SHCI command payload length (command size + 4, for the SHCI header)
 */
static size_t packDataCmnd( _otaCommand_t * pCommand, esp_partition_t *partition, size_t imageAddress, size_t size, uint32_t targetAddress )
{
	pCommand->opCode = OTA_XMIT_OPCODE;
	pCommand->connHandle = FIXED_CONNECTION_HANDLE;
	pCommand->charHandle = SwapTwoBytes( OTA_COMMAND_HANDLE );
	pCommand->Data.command = eOTA_DATA_CMD;

	/* Read a chunk of Image from Flash - use esp_partition_read() reads flash correctly whether partition is encrypted or not */
	esp_partition_read( partition, imageAddress, pCommand->Data.buffer, size );

	if( OTA_PKT_DLEN_192 == size )				/* 192 byte data command */
	{
		pCommand->LongData.length = sizeof( _otaLongData_t );
		pCommand->LongData.address = SwapFourBytes( targetAddress );

		appendCRC( ( uint8_t *) &pCommand->LongData, ( sizeof( _otaLongData_t ) - 2 ) );

		return( sizeof( _otaLongData_t ) + 4 );
	}
	else if( OTA_PKT_DLEN_128 == size )			/* 128 byte Data command */
	{
		pCommand->Data.length = sizeof( _otaData_t );
		pCommand->Data.address = SwapFourBytes( targetAddress );

		appendCRC( ( uint8_t *) &pCommand->Data, ( sizeof( _otaData_t ) - 2 ) );

		return( sizeof( _otaData_t ) + 4 );
	}
	else if( OTA_PKT_DLEN_64 == size )	/* 64 byte Data command */
	{
		pCommand->ShortData.length = sizeof( _otaShortData_t );
		pCommand->ShortData.address = SwapFourBytes( targetAddress );

		appendCRC( ( uint8_t *) &pCommand->ShortData, ( sizeof( _otaShortData_t ) - 2 ) );

		return( sizeof( _otaShortData_t ) + 4 );
	}
	else
	{
		return( 0 );
	}
}


/**
 * @brief	Pack Verify Command
 *
 * @param[out]	pCommand	Pointer to command buffer
 * @return  SHCI command payload length (command size + 4, for the SHCI header)
 */
static size_t packVerifyCmnd( _otaCommand_t * pCommand )
{
	pCommand->opCode = OTA_XMIT_OPCODE;
	pCommand->connHandle = FIXED_CONNECTION_HANDLE;
	pCommand->charHandle = SwapTwoBytes( OTA_COMMAND_HANDLE );

	pCommand->Verify.command = eOTA_VERIFY_CMD;
	pCommand->Verify.length = sizeof( _otaVerify_t );

	pCommand->Verify.startAddr = SwapFourBytes( _hostota.LoadAddress );
	pCommand->Verify.imgLength = SwapFourBytes( _hostota.ImageSize );
	memcpy( pCommand->Verify.sha256hash, &_hostota.sha256Plain, 32 );

	appendCRC( ( uint8_t *) &pCommand->Verify, ( sizeof( _otaVerify_t ) - 2 ) );

	return( sizeof( _otaVerify_t ) + 4 );
}


/**
 * @brief	Pack Reset Command
 *
 * @param[out]	pCommand	Pointer to command buffer
 * @return  SHCI command payload length (command size + 4, for the SHCI header)
 */
static size_t packResetCmnd( _otaCommand_t * pCommand )
{
	pCommand->opCode = OTA_XMIT_OPCODE;
	pCommand->connHandle = FIXED_CONNECTION_HANDLE;
	pCommand->charHandle = SwapTwoBytes( OTA_COMMAND_HANDLE );

	pCommand->Reset.command = eOTA_RESET_CMD;
	pCommand->Reset.length = sizeof( _otaReset_t );

	appendCRC( ( uint8_t *) &pCommand->Reset, ( sizeof( _otaReset_t ) - 2 ) );

	return( sizeof( _otaReset_t ) + 4 );
}


/**
 * @brief	Pack Bootloader Command that consists only of an OpCode
 *
 *	This handles Version and Calculate CRC commands
 *
 * @param[out]	pCommand	Pointer to command buffer
 * @param[in]	opCode		Bootloader OpCode to be packed
 * @return  SHCI command payload length (command size + 1, for the SHCI header)
 */
static size_t packOpcodeOnlyCmnd( _blCommand_t * pCommand, _otaOpcode_t opCode )
{
	pCommand->opCode = eHostUpdateCommand;

	pCommand->OpCodeOnly.command = opCode;
	pCommand->OpCodeOnly.length = sizeof( _blOpcode_t );

	appendCRC( ( uint8_t *) &pCommand->OpCodeOnly, ( sizeof( _blOpcode_t ) - 2 ) );

	return( sizeof( _blOpcode_t ) + 1 );
}


/**
 * @brief	Pack Flash Erase Command
 *
 * @param[out]	pCommand	Pointer to command buffer
 * @param[in]	targetAddress	Address of first page to be erased
 * @param[in]	page_count		Number of pages to be erased
 * @return  SHCI command payload length (command size + 1, for the SHCI header)
 */
static size_t packFlashEraseCmnd( _blCommand_t * pCommand, uint32_t targetAddress, size_t page_count )
{
	pCommand->opCode = eHostUpdateCommand;

	pCommand->FlashErase.command = eBL_FLASH_ERASE_CMD;
	pCommand->FlashErase.length = sizeof( _blFlashErase_t );
	pCommand->FlashErase.EE_key_1 = 0x55;
	pCommand->FlashErase.EE_key_2 = 0xAA;
	pCommand->FlashErase.addr.address32 = targetAddress;
	pCommand->FlashErase.page_count = page_count;

	appendCRC( ( uint8_t *) &pCommand->FlashErase, ( sizeof( _blFlashErase_t ) - 2 ) );

	return( sizeof( _blFlashErase_t ) + 1 );
}


/**
 * @brief	Check if a buffer contains only FFs
 *
 * @param[in] pbuf	pointer to buffer
 * @param[in] size 	size of buffer to check
 * @return	true if buffer only contains FFs, false if the buffer has any non FF byte
 */
static bool isEmpty( uint8_t *pbuf, size_t size )
{
	size_t i;

	for( i = 0; i < size; ++i )
	{
		if( *pbuf++ != 0xff )					// any byte that is not FF means that the buffer is used
		{
			return false;
		}
	}
	return true;
}


/**
 * @brief	Pack Flash Write Command
 *
 *	Data from NVS partition has already been read into blCommand structure.
 *	Complete packet format for Flash Write command
 *
 * @param[out]	pCommand	Pointer to command buffer
 * @param[in]	targetAddress	Host memory address, where data is to be programmed
 * @param[in]	size			Number of data bytes to be read and packed
 * @return  SHCI command payload length (command size + 1, for the SHCI header)
 */
static size_t packFlashWriteCmnd( _blCommand_t * pCommand, uint32_t targetAddress, size_t size )
{
	pCommand->opCode = eHostUpdateCommand;

	pCommand->FlashWrite.command = eBL_FLASH_WRITE_CMD;
	/* FIXME following length assignment assumes that size is 256 and sizeof (_blFlashWrite_t )  is thus > 255 */
	/* TODO Generalize to handle any size, with limit checking */
	pCommand->FlashWrite.length = SwapTwoBytes( ( uint16_t )sizeof( _blFlashWrite_t ) );
	pCommand->FlashWrite.EE_key_1 = 0x55;
	pCommand->FlashWrite.EE_key_2 = 0xAA;
	pCommand->FlashWrite.addr.address32 = targetAddress;			// little endian

	appendCRC( ( uint8_t *) &pCommand->FlashWrite, ( sizeof( _blFlashWrite_t ) - 2 ) );

	return( sizeof( _blFlashWrite_t ) + 1 );
}


/**
 * @brief	Bootloader Init function
 *
 * For use in step-wise, table-driven, image transfer
 * Use in first table step to allocate transfer buffer
 *
 * @param[in]	pHost	Pointer to Host OTA structure
 * @param[in]	opcode	OTA command opcode (unused)
 * @return		0 - no command to send
 */
static size_t init( host_ota_t *pHost, _otaOpcode_t opcode )
{
	IotLogDebug( "Initialize" );

	/* Allocate a command buffer */
	pHost->pXferBuf = pvPortMalloc( sizeof( _blCommand_t ) );

	return( 0 );
}


/**
 * @brief	Bootloader Version function
 *
 * For use in step-wise, table-driven, image transfer
 *
 * @param[in]	pHost	Pointer to Host OTA structure
 * @param[in]	opcode	OTA command opcode
 * @return		number of bytes in transfer buffer to be sent
 */
static size_t version( host_ota_t *pHost, _otaOpcode_t opcode )
{
	IotLogDebug( "Send Version" );

	/* pack command */
	return( packOpcodeOnlyCmnd( pHost->pXferBuf, opcode ) );

}

/**
 * @brief	Bootloader Version Acknowledge function
 *
 * For use in step-wise, table-driven, image transfer
 *
 * @param[in]	pHost	Pointer to Host OTA structure
 * @return		true if step is complete, false if more processing to be performed in step
 */
static bool onVersionAck( host_ota_t *pHost )
{
	pHost->targetAddress = pHost->LoadAddress + pHost->startAddress;

	return true;		// this step is complete
}


/**
 * @brief	Bootloader Flash Erase function
 *
 * For use in step-wise, table-driven, image transfer
 *
 * @param[in]	pHost	Pointer to Host OTA structure
 * @param[in]	opcode	OTA command opcode
 * @return		number of bytes in transfer buffer to be sent
 */
static size_t flashErase( host_ota_t *pHost, _otaOpcode_t opcode )
{
	size_t			pageCount;
	IotLogDebug( "Send Flash Erase" );

	pageCount = ( pHost->ImageSize - pHost->startAddress) / 256;			// erase page is 128 words, 256 bytes

	/* pack command */
	return( packFlashEraseCmnd( pHost->pXferBuf, pHost->targetAddress, pageCount ) );
}

/**
 * @brief	Bootloader Flash Erase Acknowledge function
 *
 * For use in step-wise, table-driven, image transfer
 *
 * @param[in]	pHost	Pointer to Host OTA structure
 * @return		true if step is complete, false if more processing to be performed in step
 */
static bool OnFlashEraseAck( host_ota_t *pHost )
{
	/* Prepare for initial flash read, startAddress is from InitAck packet */
	pHost->imageAddress = pHost->PaddingBoundary + pHost->startAddress;
	pHost->targetAddress = pHost->LoadAddress + pHost->startAddress;
	pHost->bytesRemaining = pHost->ImageSize - pHost->startAddress;
	pHost->percentComplete = 0;
	pHost->lastPercentComplete = -1;

	return true;		// this step is complete
}


/**
 * @brief	Increment transfer addresses and decrement bytes remaining
 *
 * @param[in]	pHost	Pointer to host OTA structure
 */
static void nextBlock( host_ota_t *pHost )
{
	/* Set variables for next flash read */
	pHost->targetAddress += pHost->transferSize;
	pHost->imageAddress += pHost->transferSize;
	pHost->bytesRemaining -= pHost->transferSize;
}


/**
 * @brief	Bootloader Flash Write function
 *
 * Read a page of transfer image, if not blank back into command.
 * If page is blank, bypass and increment to next page.
 * If last page is blank, return 0 indicating complete image has been written.
 *
 * For use in step-wise, table-driven, image transfer
 *
 * @param[in]	pHost	Pointer to Host OTA structure
 * @param[in]	opcode	OTA command opcode
 * @return		number of bytes in transfer buffer to be sent
 */
static size_t flashWrite( host_ota_t *pHost, _otaOpcode_t opcode )
{
	_blCommand_t * pCommand = pHost->pXferBuf;

	IotLogDebug( "Send Flash Write" );

	/* FIXME notifications bypassed for debug */
//	hostOtaNotificationUpdate( eNotifyFlashProgram, pHost->percentComplete );

	/* loop if page is empty. unless at end of image */
	while(1 )
	{
		/* End of image? Then we're done */
		if( !pHost->bytesRemaining )
		{
			pHost->transferSize = 0;
			pHost->ackReceived = true;
			/* Return zero, this will bypass sending of command */
			return( 0 );
		}

		/* Calculate size of data to send */
		pHost->transferSize = ( OTA_PKT_DLEN_256 < pHost->bytesRemaining ) ? OTA_PKT_DLEN_256 : pHost->bytesRemaining;

		/* Read a chunk of Image from Flash - use esp_partition_read() reads flash correctly whether partition is encrypted or not */
		esp_partition_read( pHost->partition, pHost->imageAddress, pCommand->FlashWrite.buffer, pHost->transferSize );

		/* If page is empty, skip to next page */
		if( !isEmpty( pCommand->FlashWrite.buffer, pHost->transferSize) )
		{
			break;
		}
		IotLogInfo( "Image page @ %08X is blank - skip writing", pHost->imageAddress );

		/* increment addresses, decrement counters */
		nextBlock( pHost );

    	vTaskDelay( 10 / portTICK_PERIOD_MS );
	}

	/* Pack data into command */
	return( packFlashWriteCmnd( pCommand, pHost->targetAddress, pHost->transferSize ) );

}


/**
 * @brief	Bootloader Flash Write Acknowledge function
 *
 * For use in step-wise, table-driven, image transfer
 *
 * @param[in]	pHost	Pointer to Host OTA structure
 * @return		true if step is complete, false if more processing to be performed in step
 */
static bool OnFlashWriteAck( host_ota_t *pHost )
{
	/* Increment addresses, decrement bytes remaining */
	nextBlock( pHost );

	/* compute percent complete */
	pHost->percentComplete = ( ( double )( 100 * ( pHost->ImageSize - pHost->bytesRemaining ) ) ) / pHost->ImageSize;

	IotLogDebug( "FlashWriteACK, targetAddress = %08X, remaining = %08X", pHost->targetAddress, pHost->bytesRemaining );

	/* When remaining bytes is zero, transfer is complete */
	return( ( 0 == pHost->bytesRemaining) ? true : false);
}


/**
 * @brief	Bootloader Calculate CRC function
 *
 * For use in step-wise, table-driven, image transfer
 *
 * @param[in]	pHost	Pointer to Host OTA structure
 * @param[in]	opcode	OTA command opcode
 * @return		number of bytes in transfer buffer to be sent
 */
static size_t calculateCRC( host_ota_t *pHost, _otaOpcode_t opcode )
{
	IotLogDebug( "Send Calculate CRC" );

	/* pack command */
	return( packOpcodeOnlyCmnd( pHost->pXferBuf, opcode) );
}


/**
 * @brief	Bootloader Calculate CRC Acknowledge function
 *
 * For use in step-wise, table-driven, image transfer
 *
 * @param[in]	pHost	Pointer to Host OTA structure
 * @return		true if step is complete, false if more processing to be performed in step
 */
static bool OnCalculateCRCAck( host_ota_t *pHost )
{
	/* Compare calculated CRC with metadata value */
	if( _hostota.calc_crc != _hostota.crc16_ccitt )
	{
		/* Set xferStatus on mis-compare */
		_hostota.xferStatus = eXferStat_CrcError;
	}
	return true;		// this step is complete
}


/**
 * @brief	Bootloader Host Reset function
 *
 * For use in step-wise, table-driven, image transfer
 *
 * @param[in]	pHost	Pointer to Host OTA structure
 * @param[in]	opcode	OTA command opcode
 * @return		number of bytes in transfer buffer to be sent
 */
static size_t hostReset( host_ota_t *pHost, _otaOpcode_t opcode )
{
	IotLogDebug( "Send Host Reset" );

	/* pack command */
	return( packOpcodeOnlyCmnd( pHost->pXferBuf, opcode) );
}


/**
 * @brief	Bootloader Host Reset Acknowledge function
 *
 * For use in step-wise, table-driven, image transfer
 *
 * @param[in]	pHost	Pointer to Host OTA structure
 * @return		true if step is complete, false if more processing to be performed in step
 */
static bool OnHostResetAck( host_ota_t *pHost )
{
	return true;		// this step is complete
}


/**
 * @brief	Bootloader complete function
 *
 * For use in step-wise, table-driven, image transfer
 * Use in last table step to free transfer buffer
 *
 * @param[in]	pHost	Pointer to Host OTA structure
 * @param[in]	opcode	OTA command opcode (unused)
 * @return		0 - no command to send
 */
static size_t complete( host_ota_t *pHost, _otaOpcode_t opcode )
{
	IotLogDebug( "Transfer complete" );

	if( NULL != pHost->pXferBuf )
	{
		/* Release buffer */
		vPortFree( pHost->pXferBuf );
	}

	return( 0 );
}

/**
 * @brief	Bootloader Step Table
 */
static const _blStep_t	blSteps[] =
{
	[ eStep_Initialize ] =
	{
			.command = &init,
			.opCode = 0,
			.expectedStatus = 0,
			.onAcknowledge = NULL,
			.stateAfterCommand = eXfer_continue,
			.nextStep = eStep_Version
	},
	[ eStep_Version ] =
	{
			.command = &version,
			.opCode = eBL_VERSION_CMD,
			.expectedStatus = eBL_VERSION_ACK,
			.onAcknowledge = &onVersionAck,
			.stateAfterCommand = eXfer_response,
			.nextStep = eStep_Erase
	},
	[ eStep_Erase ] =
	{
			.command = &flashErase,
			.opCode = eBL_FLASH_ERASE_CMD,
			.expectedStatus = eBL_FLASH_ERASE_ACK,
			.onAcknowledge = &OnFlashEraseAck,
			.stateAfterCommand = eXfer_response,
			.nextStep = eStep_Write
	},
	[ eStep_Write ] =
	{
			.command = &flashWrite,
			.opCode = eBL_FLASH_WRITE_CMD,
			.expectedStatus = eBL_FLASH_WRITE_ACK,
			.onAcknowledge = &OnFlashWriteAck,
			.stateAfterCommand = eXfer_response,
			.nextStep = eStep_Verify
	},
	[ eStep_Verify ] =
	{
			.command = &calculateCRC,
			.opCode = eBL_CALC_CRC_CMD,
			.expectedStatus = eBL_CALC_CRC_ACK,
			.onAcknowledge = &OnCalculateCRCAck,
			.stateAfterCommand = eXfer_response,
			.nextStep = eStep_Reset
	},
	[ eStep_Reset ] =
	{
			.command = &hostReset,
			.opCode = eOTA_RESET_CMD,
			.expectedStatus = eBL_RESET_ACK,
			.onAcknowledge = &OnHostResetAck,
			.stateAfterCommand = eXfer_response,
			.nextStep = eStep_Complete
	},
	[ eStep_Complete ] =
	{
			.command = &complete,
			.opCode = 0,
			.expectedStatus = 0,
			.onAcknowledge = NULL,
			.stateAfterCommand = eXfer_complete,
			.nextStep = eStep_Complete
	}
};

/**
 * @brief	Step-wise, table driven, Image transfer
 *
 * In general, each step contains a command function that typically causes an SHCI command
 * to be sent to the host.  The Host will then respond, and that response is processed by
 * the onAcknowledge() function. This function will determine if the step is complete; if so
 * control passes to the linked nextStep, if not control is retained by the current step
 * and the command function will continue processing the data for the step.
 *
 * This function make use of the _hostota variable structure, and updates the pStep member
 * when a step has completed. When all steps are completed _hostota.pStep is set to NULL.
 *
 * @param[in]	pStep	Pointer to current Image Transfer Step
 */
static void ImageTransfer( const _blStep_t *pStep )
{
	size_t			packedLength;

	switch( _hostota.mzXfer_state )
	{
		case eXfer_command:
			if( NULL != pStep->command )
			{
				packedLength = pStep->command( &_hostota, pStep->opCode );

				/* If non-zero packedLength */
				if( packedLength )
				{
					/* set expected response */
					_hostota.expectedStatus = pStep->expectedStatus;

					/* clear acknowledge received flag */
					_hostota.ackReceived = false;

					/* Post SHCI Command to message queue */
					shci_PostResponse( _hostota.pXferBuf, packedLength );
				}

				/* switch to linked next state */
				_hostota.mzXfer_state = pStep->stateAfterCommand;
			}
			break;

		case eXfer_response:
			/* If Acknowledgement received */
			if( _hostota.ackReceived )
			{
				/* default to no error */
				_hostota.xferStatus = eXferStat_NoError;

				/* process action */
				if( NULL != pStep->onAcknowledge )
				{
					if( pStep->onAcknowledge( &_hostota ) )
					{
						/* If this step is complete, go to next step, linked */
						_hostota.pStep = &blSteps[ pStep->nextStep ];
					}
				}
				else
				{
					/* No action, go to next step, linked */
					_hostota.pStep = &blSteps[ pStep->nextStep ];
				}

				/* onAcknowledge function can set xferStatus on error condition */
				if( eXferStat_NoError == _hostota.xferStatus)
				{
					/* No Error - return to command state */
					_hostota.mzXfer_state = eXfer_command;
				}
				else
				{
					_hostota.mzXfer_state = eXfer_error;
				}
			}
			/* TODO!! Handle timeouts and other errors */
			break;

		case eXfer_continue:
			/* No action, go to next step, linked */
			_hostota.pStep = &blSteps[ pStep->nextStep ];

			/* return to command state */
			_hostota.mzXfer_state = eXfer_command;
			break;

		case eXfer_complete:
			/* Transfer is complete: set pStep to NULL, this will terminate the calls to ImageTransfer() */
			_hostota.pStep = NULL;
			break;

		case eXfer_error:
			IotLogError( "Transfer Error: %d", _hostota.xferStatus );
			/* terminate transfer */
			_hostota.pStep = NULL;
			break;
		default:
			break;
	}
}


/**
 * @brief	PIC32MZ Image Transfer
 *
 *	Transfer the PIC32MZ Image from the Flash Partition (to which it was down-loaded) to
 *	the PIC32MZ processor.  The protocol is the same as used over Bluetooth, however different
 *	SHCI op-codes are used so that Bluetooth activity will not interfere with this transfer.
 *
 *	A finite state machine performs the transfer with the following basic states:
 *	+ Init  - Initializes the communication link
 *	+ Erase - Erases the target flash bank on the PIC32MZ
 *	+ Data  - Transfers the encrypted image, block-by-block
 *	+ Verify - Verifies the transfered (and decrypted) image using an SHA256 Hash
 *	+ Reset  - Resets the PIC32MZ processor so it can run the new image
 *
 *	Maximum bytes transfered per block is 192.  The host encryption engine requires blocks to
 *	be multiples of 64 bytes.  The transfer protocol has a maximum total command length of 256 bytes.
 *	192 is thus the maximum multiple of 64 that give a total command length less than 256.  The
 *	Bluetooth protocol limited transfers to 128 bytes.  The increased data length reduced the
 *	transfer time by approximately 25% (~120s -> 90s).
 *
 * @param[in]	bStart	Flag to initialize the state machine
 *
 * @return	enumerated state machine state
 */
static _mzXfer_state_t PIC32MZ_ImageTransfer( bool bStart )
{
	static size_t	imageAddress;		/**< Current offset, from start of Flash Partition, used to read image block */
	static size_t	remaining;			/**< Number of bytes remaining to be transfered */
	static size_t	size;				/**< Size, in bytes, of the current transfer block */
	size_t			packedLength;
//	uint16_t		crc;

	/* If start flag is true, set state to Init */
	if( bStart )
	{
		_hostota.mzXfer_state = mzXfer_Init;
	}

	switch( _hostota.mzXfer_state )
	{
		case mzXfer_Idle:
			break;

		case mzXfer_Init:
			IotLogDebug( "mzXfer_Init" );
			/* Allocate a command buffer */
			_hostota.pXferBuf = pvPortMalloc( sizeof( _otaCommand_t ) );

			/* create a pseudo random 16-bit UID */
			_hostota.uid = ( uint16_t ) ( IotClock_GetTimeMs() % 0x10000 );
			/* pack command */
			packedLength = packInitCmnd( _hostota.pXferBuf, _hostota.uid );

			_hostota.expectedStatus = eOTA_INIT_ACK;
			_hostota.ackReceived = false;

			/* Post SHCI Command to message queue */
			shci_PostResponse( ( uint8_t * )_hostota.pXferBuf, packedLength );

			_hostota.mzXfer_state = mzXfer_Init_Ack;
			break;

		case mzXfer_Init_Ack:				/* Wait for Init ACK */
			if( _hostota.ackReceived )
			{
				_hostota.mzXfer_state = mzXfer_Erase;
			}
			break;

		case mzXfer_Erase:
			hostOtaNotificationUpdate( eNotifyFlashErase, 0 );
			IotLogDebug( "mzXfer_Erase" );

			/* pack command */
			packedLength = packEraseCmnd( _hostota.pXferBuf );

			_hostota.expectedStatus = eOTA_ERASE_ACK;
			_hostota.ackReceived = false;

			/* Post SHCI Command to message queue */
			shci_PostResponse( ( uint8_t * )_hostota.pXferBuf, packedLength );

			_hostota.mzXfer_state = mzXfer_Erase_Ack;
			break;

		case mzXfer_Erase_Ack:				/* Wait for Erase ACK */
			if( _hostota.ackReceived )
			{
				/* Prepare for initial flash read, startAddress is from InitAck packet */
				imageAddress = _hostota.PaddingBoundary + _hostota.startAddress;
				_hostota.targetAddress = _hostota.LoadAddress + _hostota.startAddress;
				remaining = _hostota.ImageSize - _hostota.startAddress;
				_hostota.percentComplete = 0;
				_hostota.lastPercentComplete = -1;

				_hostota.mzXfer_state = mzXfer_Data;
			}
			break;

		case mzXfer_Data:
			hostOtaNotificationUpdate( eNotifyFlashProgram, _hostota.percentComplete );
			IotLogDebug( "mzXfer_Data" );

			/* Calculate size of data to send */
			size = ( MAX_OTA_PKT_DLEN < remaining ) ? MAX_OTA_PKT_DLEN : remaining;

			/* Pack data into command */
			packedLength = packDataCmnd( _hostota.pXferBuf, _hostota.partition, imageAddress, size, _hostota.targetAddress );

			if( packedLength )
			{
				_hostota.expectedStatus = eOTA_DATA_ACK;
				_hostota.ackReceived = false;

				/* Post SHCI Command to message queue */
				shci_PostResponse( ( uint8_t * )_hostota.pXferBuf, packedLength );
				_hostota.mzXfer_state = mzXfer_Data_Ack;
			}
			else
			{
				IotLogError( "Error: Image Size is not a multiple of %d", OTA_PKT_DLEN_64 );
				_hostota.mzXfer_state = mzXfer_Error;
			}
			break;

		case mzXfer_Data_Ack:				/* Wait for Data ACK */
			if( _hostota.ackReceived )
			{
				/* Set variables for next flash read */
				_hostota.targetAddress += size;
				imageAddress += size;
				remaining -= size;

				/* compute percent complete */
				_hostota.percentComplete = ( ( double )( 100 * ( _hostota.ImageSize - remaining ) ) ) / _hostota.ImageSize;

				IotLogDebug( "DataACK, targetAddress = %08X, remaining = %08X", _hostota.targetAddress, remaining );
				/* When remaining bytes is zero, transfer is complete */
				_hostota.mzXfer_state = remaining ? mzXfer_Data : mzXfer_Verify;
			}
			break;


		case mzXfer_Verify:
			hostOtaNotificationUpdate( eNotifyUpdateValidation, 0 );
			IotLogDebug( "mzXfer_Verify");

			/* Pack verify into command */
			packedLength = packVerifyCmnd( _hostota.pXferBuf );

			_hostota.expectedStatus = eOTA_DATA_ACK;
			_hostota.ackReceived = false;

			/* Post SHCI Command to message queue */
			shci_PostResponse( _hostota.pXferBuf, packedLength );

			_hostota.mzXfer_state = mzXfer_Verify_Ack;
			break;

		case mzXfer_Verify_Ack:				/* Wait for Verify ACK */
			if( _hostota.ackReceived )
			{
				_hostota.mzXfer_state = mzXfer_Reset;
			}
			break;

		case mzXfer_Reset:
			hostOtaNotificationUpdate( eNotifyHostReset, 0 );

			/* Pack Reset into command */
			packedLength = packResetCmnd( _hostota.pXferBuf );

			/* Post SHCI Command to message queue */
			shci_PostResponse( _hostota.pXferBuf, packedLength );

			_hostota.mzXfer_state = mzXfer_Complete;
			break;

		case mzXfer_Complete:
			if( NULL != _hostota.pXferBuf )
			{
				/* Release buffer */
				vPortFree( _hostota.pXferBuf );
			}
			_hostota.mzXfer_state = mzXfer_Idle;
			break;

		case mzXfer_Error:
			_hostota.mzXfer_state = mzXfer_Complete;
			break;

		default:
			_hostota.mzXfer_state = mzXfer_Idle;
			break;

	}
	return _hostota.mzXfer_state;
}

/**
 * @Brief	Set ImageState and save to NVS
 *
 * @param[in]	eState		Image State to set and save
 */
static void setImageState( OTA_PAL_ImageState_t eState)
{
	_hostota.imageState = eState;
	NVS_Set( NVS_HOSTOTA_STATE, &_hostota.imageState, NULL);
}

/**
 * @brief	Host OTA Task
 *
 * Executes on separate thread:
 *	- Fetches Dispense Records from Host, puts to the Event FIFO
 *	- Gets Event Records from Event FIFO, pushes them to AWS
 *
 *	Last record index fetched from Host is stored in NVS
 *	Last fetched record index is compared with record count to determine
 *	if there are more records to be fetched.
 *	If FIFO is full, no records are fetched.
 *
 *	Putting records to AWS ... TBD
 *
 * @param[in] arg Event FIFO handle
 *
 */
static void _hostOtaTask(void *arg)
{
	char *json;
	int json_length;
	double value;
	uint8_t	*pBuffer;
    size_t size;
    size_t address;
    size_t remaining;
    sha256_t	hash;
	mbedtls_sha256_context ctx;
	int mjson_stat;
	esp_err_t	err = ESP_OK;

	/* Create semaphore to wait for Image Download from AWS IoT */
	if( IotSemaphore_Create( &_hostota.iotUpdateSemaphore, 0, 1 ) == false )
	{
		IotLogError("Failed to create semaphore");
	}
	else
	{
		IotLogInfo( "iotUpdateSempahore = %p", &_hostota.iotUpdateSemaphore );
	}

	/* Attempt to retrieve image state from NVS */
	err = NVS_Get( NVS_HOSTOTA_STATE, &_hostota.imageState, NULL);
	if( ESP_OK == err )
	{
		IotLogInfo(" ImageState = %d", _hostota.imageState );
	}
	else
	{
		setImageState( eOTA_PAL_ImageState_Unknown );
	}

	IotLogInfo( "_hostOtaTask" );

	pBuffer = pvPortMalloc( 512 );

    while( 1 )
	{

    	switch( _hostota.state )
    	{
    		case eHostOtaInit:
    			/* wait for an MQTT connection before starting the host ota update */
    			if( mqtt_IsConnected() )
    			{
    				IotLogInfo( "_hostOtaTask -> PendUpdate" );
    				_hostota.state = eHostOtaPendUpdate;
    			}
    			else
    			{
					vTaskDelay( 1000 / portTICK_PERIOD_MS );
    			}
    			break;

			case eHostOtaIdle:

				/* Get currently running MZ firmware version */
//				_hostota.currentVersion_MZ = shadowUpdate_getFirmwareVersion_MZ();
//				IotLogInfo( "host ota: current MZ version = %5.2f", _hostota.currentVersion_MZ );

				/* Get currently running PIC firmware version */
				_hostota.currentVersion_PIC = shadowUpdate_getFirmwareVersion_PIC();
				IotLogInfo( "host ota: current PIC version = %5.2f", _hostota.currentVersion_PIC );

				/* hardwire partition for initial development */
				_hostota.partition = ( esp_partition_t * )esp_partition_find_first( 0x44, 0x57, "pic_fw" );
				IotLogInfo("host ota: partition address = %08X, length = %08X", _hostota.partition->address, _hostota.partition->size );

				/*
				 *	Read the first 512 bytes from the Flash partition
				 *	Use esp_partition_read() reads flash correctly whether partition is encrypted or not
				 */
				esp_partition_read( _hostota.partition, 0, pBuffer, 512 );

				IotLogInfo( "_hostOtaTask -> ParseJSON" );
				_hostota.state = eHostOtaParseJSON;
				break;

			case eHostOtaParseJSON:

				/* Parse the JSON Header, extracting parameters */
				json = (char *) pBuffer;
				json_length = strlen( json );

				IotLogDebug( "JSON[%d] = %s\n", json_length, json );

				mjson_stat = mjson_get_number( json, json_length, "$.PaddingBoundary", &value );
				if( 0 != mjson_stat )
				{
					_hostota.PaddingBoundary = ( uint32_t ) value;
					IotLogDebug( "  PaddingBoundary = %d\n", _hostota.PaddingBoundary );

					mjson_stat = mjson_get_number( json, json_length, "$.LoadAddress", &value );
				}

				if( 0 != mjson_stat )
				{
					_hostota.LoadAddress = ( uint32_t ) value;
					IotLogDebug( "  LoadAddress = 0x%08X\n", _hostota.LoadAddress );

					mjson_stat = mjson_get_number( json, json_length, "$.ImageSize", &value );
				}

				if( 0 != mjson_stat )
				{
					_hostota.ImageSize = ( uint32_t ) value;
					IotLogDebug( "  ImageSize = 0x%08X\n", _hostota.ImageSize );

					mjson_stat = mjson_get_number( json, json_length, "$.CRC16_CCITT", &value );
				}

				if( 0 != mjson_stat )
				{
					_hostota.crc16_ccitt = ( uint16_t ) value;
					IotLogDebug( "  CRC16_CCITT = 0x%04X\n", _hostota.crc16_ccitt );

#ifdef	MZ_SUPPORT
					mjson_stat = mjson_get_number( json, json_length, "$.Offset", &value );
				}

				if( 0 != mjson_stat )
				{
					_hostota.Offset = ( uint32_t ) value;
					IotLogDebug( "  Offset = 0x%08X\n", _hostota.Offset );

					mjson_stat = mjson_get_number( json, json_length, "$.Version_MZ", &_hostota.Version_MZ );
				}

				if( 0 != mjson_stat )
				{
					IotLogDebug( "  Version_MZ = %f\n", _hostota.Version_MZ );

#endif
					mjson_stat = mjson_get_number( json, json_length, "$.Version_PIC", &_hostota.Version_PIC );
				}

				if( 0 != mjson_stat )
				{
					IotLogDebug( "  Version_PIC = %f\n", _hostota.Version_PIC );

#ifdef	MZ_SUPPORT
					mjson_stat = mjson_get_base64(json, json_length, "$.SHA256Plain", ( char * )&_hostota.sha256Plain, sizeof( sha256_t ) );
#else
					mjson_stat = mjson_get_base64(json, json_length, "$.SHA256", ( char * )&_hostota.sha256Plain, sizeof( sha256_t ) );
#endif
				}

				if( 0 != mjson_stat )
				{
					printsha256( "Plain", &_hostota.sha256Plain );

#ifdef	MZ_SUPPORT
					mjson_stat = mjson_get_base64(json, json_length, "$.SHA256Encrypted", ( char * )&_hostota.sha256Encrypted, sizeof( sha256_t ) );
				}

				if( 0 != mjson_stat )
				{
					printsha256( "Encrypted", &_hostota.sha256Encrypted );
#endif
    				IotLogInfo( "_hostOtaTask -> VerifyImage" );
					_hostota.state = eHostOtaVerifyImage;
				}
				else
				{
					_hostota.waitMQTTretry = MQTT_WAIT_RETRY_COUNT;
    				IotLogInfo( "_hostOtaTask -> WaitMQTT" );
					_hostota.state = eHostOtaWaitMQTT;				/* If error encountered parsing JSON, pend on an update from AWS */
				}
				break;

			case eHostOtaVerifyImage:

				/* Verify the received image using the hash for the encrypted image, from the header */
				mbedtls_sha256_init( &ctx );
				mbedtls_sha256_starts( &ctx, 0 );						/* SHA-256, not 224 */

				address = _hostota.PaddingBoundary;

				/*
				 *	Read image, block-by-block, calculating SHA256 Hash on the fly
				 *	Use esp_partition_read() - reads flash correctly whether partition is encrypted or not
				 */
				for( remaining = _hostota.ImageSize; remaining ; ( remaining -= size ), ( address += size ) )
				{
					/* read a block of image */
					size = ( 512 < remaining ) ? 512 : remaining;
					esp_partition_read( _hostota.partition, address, pBuffer, size );
					/* add to hash */
					mbedtls_sha256_update( &ctx, pBuffer, size );
				}

				/* get result */
				mbedtls_sha256_finish( &ctx, ( uint8_t * ) &hash );

				printsha256( "Hash", &hash);

#ifdef	MZ_SUPPORT
				if( 0 == memcmp( &_hostota.sha256Encrypted, &hash, 32 ) )
				{
					IotLogInfo( "Image SHA256 Hash matches metadata SHA256Encrypted" );
    				IotLogInfo( "_hostOtaTask -> VersionCheck" );
					_hostota.state = eHostOtaVersionCheck;
				}
				else
				{
					IotLogError( "Image SHA256 Hash does not match metadata SHA256Encrypted" );
					_hostota.waitMQTTretry = MQTT_WAIT_RETRY_COUNT;
    				IotLogInfo( "_hostOtaTask -> WaitMQTT" );
					_hostota.state = eHostOtaWaitMQTT;					/* If Image not Valid, pend on an update from AWS */
				}
#else
				if( 0 == memcmp( &_hostota.sha256Plain, &hash, 32 ) )
				{
					IotLogInfo( "Image SHA256 Hash matches metadata SHA256" );
    				IotLogInfo( "_hostOtaTask -> VersionCheck" );
					_hostota.state = eHostOtaVersionCheck;
				}
				else
				{
					IotLogError( "Image SHA256 Hash does not match metadata SHA256" );
					_hostota.waitMQTTretry = MQTT_WAIT_RETRY_COUNT;
    				IotLogInfo( "_hostOtaTask -> WaitMQTT" );
					_hostota.state = eHostOtaWaitMQTT;					/* If Image not Valid, pend on an update from AWS */
				}
#endif
				break;

			case eHostOtaVersionCheck:

				/* If Bootload is already active, it will be sending BootMe messages */
				if( _hostota.bBootme )
				{
					IotLogInfo( "_hostOtaTask -> WaitBootme" );
					_hostota.state = eHostOtaWaitBootme;
				}
				else if( 0 > _hostota.currentVersion_PIC )
				{
					vTaskDelay( 1000 / portTICK_PERIOD_MS );
					_hostota.currentVersion_PIC = shadowUpdate_getFirmwareVersion_PIC();
				}
				else if( _hostota.Version_PIC > _hostota.currentVersion_PIC )
				{
					IotLogInfo( "Update from %5.2f to %5.2f", _hostota.currentVersion_PIC, _hostota.Version_PIC);
    				IotLogInfo( "_hostOtaTask -> UpdateAvailable" );
 					_hostota.state = eHostOtaUpdateAvailable;
				}
				else
				{
					IotLogInfo( "Current Version: %5.2f, Downloaded Version: %5.2f", _hostota.currentVersion_PIC, _hostota.Version_PIC);
					_hostota.waitMQTTretry = MQTT_WAIT_RETRY_COUNT;
    				IotLogInfo( "_hostOtaTask -> WaitMQTT" );
					_hostota.state = eHostOtaWaitMQTT;				/* If downloaded Image version is not greater than current version, pend on an update from AWS */
				}
#ifdef	MZ_SUPPORT
				if( 0 > _hostota.currentVersion_MZ )
				{
					vTaskDelay( 1000 / portTICK_PERIOD_MS );
					_hostota.currentVersion_MZ = shadowUpdate_getFirmwareVersion_MZ();
				}
				else if( _hostota.Version_MZ > _hostota.currentVersion_MZ )
				{
					IotLogInfo( "Update from %5.2f to %5.2f", _hostota.currentVersion_MZ, _hostota.Version_MZ);
    				IotLogInfo( "_hostOtaTask -> Transfer" );
    				_hostota.pStep = &blSteps[ 0 ];						/* start image transfer with first step */
					_hostota.state = eHostOtaTransfer;
					_hostota.bStartTransfer = true;
				}
				else
				{
					IotLogInfo( "Current Version: %5.2f, Downloaded Version: %5.2f", _hostota.currentVersion_MZ, _hostota.Version_MZ);
					_hostota.waitMQTTretry = MQTT_WAIT_RETRY_COUNT;
    				IotLogInfo( "_hostOtaTask -> WaitMQTT" );
					_hostota.state = eHostOtaWaitMQTT;				/* If downloaded Image version is not greater than current version, pend on an update from AWS */
				}
#endif
				break;

			case eHostOtaWaitMQTT:
				/* If MQTT connection is active, or retry count expires */
				if( (mqtt_IsConnected() == true ) || ( _hostota.waitMQTTretry-- == 0 ) )
				{
    				IotLogInfo( "_hostOtaTask -> PendUpdate" );
					_hostota.state = eHostOtaPendUpdate;				/* Pend on an update from AWS */
				}
				break;

			case eHostOtaPendUpdate:
				if( eOTA_PAL_ImageState_PendingCommit == _hostota.imageState )
				{
					/* If image state is pending commit */
					if( _hostota.Version_MZ == _hostota.currentVersion_MZ )
					{
						/* If downloaded Image version is equal to current version: update was successful */
						hostOtaNotificationUpdate( 	eNotifyUpdateSuccess, _hostota.currentVersion_MZ );
						setImageState( eOTA_PAL_ImageState_Valid );
						IotLogInfo( "Host update successful, current version: %5.2f", _hostota.currentVersion_MZ );
					}
					else
					{
						/* If downloaded Image version is not equal to current version: update failed */
						hostOtaNotificationUpdate( eNotifyUpdateFailed, 0 );
						setImageState( eOTA_PAL_ImageState_Invalid );
						IotLogInfo( "Host update failed, current version: %5.2f", _hostota.currentVersion_MZ );
					}

				}

				/* Pend on an update from AWS */
				hostOtaNotificationUpdate( eNotifyWaitForImage, 0 );
				IotLogInfo( "Host OTA Update: pend on image download" );

				/* wait for an update from AWS, if wait times out, check if a valid image is already present */
				if( IotSemaphore_TimedWait( &_hostota.iotUpdateSemaphore, HOSTOTA_PEND_TIMEOUT_MS ) == false )
				{
					IotLogInfo( "Host OTA Update: time-out expired" );
				}
				IotLogInfo( "_hostOtaTask -> Idle" );
				_hostota.state = eHostOtaIdle;							/* back to image verification */
				break;

			case eHostOtaUpdateAvailable:												/* Update image is available */
				/* Inform host - post Update Available SHCI Event */
				shci_PostResponse( &updateAvailableEvent, sizeof( updateAvailableEvent ) );

				IotLogInfo( "_hostOtaTask -> WaitBootme" );
				_hostota.state = eHostOtaWaitBootme;
				break;

			case eHostOtaWaitBootme:
				/* Wait for host to reset and start sending BootMe messages */
				if( _hostota.bBootme )
				{
					IotLogInfo(" Bootloader is active" );
    				IotLogInfo( "_hostOtaTask -> Transfer" );

    				_hostota.pStep = &blSteps[ 0 ];											/* start image transfer with first step */
					_hostota.state = eHostOtaTransfer;
					_hostota.bStartTransfer = true;
				}
				break;

			case eHostOtaTransfer:
				if( NULL != _hostota.pStep )											/* If transfer is not complete */
				{
					ImageTransfer( _hostota.pStep );									/* Run current transfer step */
				}
				else
				{
    				IotLogInfo( "_hostOtaTask ->Activate" );
					_hostota.state = eHostOtaActivate;
				}
#ifdef	LEGACY
				if( mzXfer_Idle == PIC32MZ_ImageTransfer( _hostota.bStartTransfer ) )
				{
    				IotLogInfo( "_hostOtaTask ->Activate" );
					_hostota.state = eHostOtaActivate;
				}
				_hostota.bStartTransfer = false;
#endif
				break;

			case eHostOtaActivate:
				_hostota.imageState = eOTA_PAL_ImageState_PendingCommit;
				NVS_Set( NVS_HOSTOTA_STATE, &_hostota.imageState, NULL );
				IotLogInfo( "_hostOtaTask -> WaitReset" );
				_hostota.state = eHostOtaWaitReset;
				break;

			case eHostOtaWaitReset:
				break;

			case eHostOtaError:
				break;

			default:
				_hostota.state = eHostOtaIdle;
				break;

    	}
    	vTaskDelay( 10 / portTICK_PERIOD_MS );
    }

	IotSemaphore_Destroy( &_hostota.iotUpdateSemaphore );

}



/* ************************************************************************* */
/* ************************************************************************* */
/* **********        I N T E R F A C E   F U N C T I O N S        ********** */
/* ************************************************************************* */
/* ************************************************************************* */

/**
 * @brief Initialize Host OTA submodule
 *
 * @param[in]	notifyCb	Handler to be called for Host OTA Update notifications
 */
int32_t hostOta_init( _hostOtaNotifyCallback_t notifyCb )
{
	/* Save the notification handler */
	_hostota.notifyHandler = notifyCb;

	_hostota.imageState = eOTA_PAL_ImageState_Unknown;

	/* Register callback for OTA Status update */
#ifdef	LEGACY_BLE_INTERFACE
	/* Register for Update to BLE Characteristic OtaStatus */
	bleInterface_registerUpdateCB( eOtaStatusIndex, &vOtaStatusUpdate );
#else
	/* Register for SHCI Host Update Response */
	shci_RegisterCommand( eHostUpdateResponse, &vOtaStatusUpdate );
#endif

	/* Create Task on new thread */
#ifndef	NO_HOST_OTA_TASK
    xTaskCreate( _hostOtaTask, "HostOta_task", HOST_OTA_STACK_SIZE, NULL, HOST_OTA_TASK_PRIORITY, &_hostota.taskHandle );
    if( NULL == _hostota.taskHandle )
	{
        return ESP_FAIL;
    }
    {
    	IotLogInfo( "host_ota_task created" );
    }
#else
    IotLogInfo( "\n\n*** Host OTA Task Bypassed ***\n\n" );
#endif
    return	ESP_OK;
}

/**
 * @brief	Getter for iotUpdateSemaphore
 *
 * @return	Pointer to iotUpdateSemaphore
 */
IotSemaphore_t * hostOta_getSemaphore( void )
{
	IotLogInfo( "hostOta_getSemaphore: iotUpdateSempahore = %p", &_hostota.iotUpdateSemaphore );
	return &_hostota.iotUpdateSemaphore;
}

OTA_PAL_ImageState_t hostOta_getImageState( void )
{
	IotLogInfo( "Get ImageState = %d", _hostota.imageState);
	return _hostota.imageState;
}

void hostOta_setImageState( OTA_ImageState_t eState )
{
	IotLogInfo( "Set ImageState = %d", eState);
	_hostota.imageState = eState;
}

/**
 * @brief	Is Host OTA pending on image update
 */
bool hostOta_pendUpdate( void )
{
	return( ( _hostota.state == eHostOtaPendUpdate ) ? true : false );
}

/**
 * @brief	Get OTA PAL Function Table
 *
 * @return	Pointer to function table
 */
const AltProcessor_Functions_t * hostOta_getFunctionTable( void )
{
	return &hostOtaFunctions;
}
