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
#include	"freertos/task.h"
#include	"event_records.h"
#include	"bleGap.h"
#include	"mqtt.h"
#include	"TimeSync.h"
#include	"nvs_utility.h"
#include	"shadow.h"
#include	"esp_partition.h"
#include	"../../../../freertos/vendors/espressif/esp-idf/components/bootloader_support/include_bootloader/bootloader_flash.h"
#include	"../../../../freertos/libraries/3rdparty/mbedtls/include/mbedtls/sha256.h"

/* Debug Logging */
#include "host_ota_logging.h"



#define	HOST_OTA_STACK_SIZE    ( 2048 )

#define	HOST_OTA_TASK_PRIORITY	( 7 )

/**
 * @brief	Host OTA State Machine States
 */
typedef enum
{
	eHostOtaIdle,
	eHostOtaParseJSON,
	eHostOtaVerifyImage,
	eHostOtaTransfer,
	eHostOtaActivate,
	eHostOtaError
} _hostOtaStates_t;

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

#define	OTA_PKT_DLEN		128						// Bootloader Transfer Packet Data Length
#define	OTA_PKT_DLEN_HALF	( OTA_PKT_DLEN / 2 )	// Bootloader Transfer Packet Data Length

/**
 * @brief	OTA Command packet definition for PIC32MZ transfer
 */
typedef struct {
	uint8_t		length;								/**< Number of bytes in transfer packet, including this byte */
	uint8_t		command;							/**< Command OpCode */
	uint16_t	uid;								/**< 16-bit unique ID */
	uint16_t	crc;								/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaInit_t;				/**< Init Command */

typedef struct {
	uint8_t		length;								/**< Number of bytes in transfer packet, including this byte */
	uint8_t		command;							/**< Command OpCode */
	uint16_t	crc;								/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaErase_t;			/**< Erase Command */

typedef struct {
	uint8_t		length;								/**< Number of bytes in transfer packet, including this byte */
	uint8_t		command;							/**< Command OpCode */
	uint32_t	address;							/**< Address */
	uint32_t	buffer[ OTA_PKT_DLEN ];				/**< Data Buffer */
	uint16_t	crc;								/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaData_t;				/**< Data Command */

typedef struct {
	uint8_t		length;								/**< Number of bytes in transfer packet, including this byte */
	uint8_t		command;							/**< Command OpCode */
	uint32_t	address;							/**< Address */
	uint32_t	buffer[ OTA_PKT_DLEN_HALF ];		/**< Data Buffer */
	uint16_t	crc;								/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaShortData_t;		/**< Data Command */

typedef struct {
	uint8_t		length;								/**< Number of bytes in transfer packet, including this byte */
	uint8_t		command;							/**< Command OpCode */
	uint32_t	startAddr;							/**< Start Address of image */
	uint32_t	imgLength;							/**< Image Length */
	uint8_t		sha256hash[ 32 ];					/**< 256-bit SHA256 Hash Code */
	uint16_t	crc;								/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaVerify_t;			/**< Verify Command */

typedef struct {
	uint8_t		length;								/**< Number of bytes in transfer packet, including this byte */
	uint8_t		command;							/**< Command OpCode */
	uint16_t	crc;								/**< 16-bit CCITT-CRC of complete packet */
}  __attribute__ ((packed)) _otaReset_t;			/**< Reset Command */


typedef struct
{
	_shciOpcode_t		opCode;						/**< SHCI OpCode */
	uint16_t			charHandle;					/**< BLE Characteristic Handle */
	union
	{
		_otaInit_t		Init;						/**< Init Command */
		_otaErase_t		Erase;						/**< Erase Command */
		_otaData_t		Data;						/**< Data Command */
		_otaShortData_t	ShortData;					/**< Data Command, half data buffer */
		_otaVerify_t	Verify;						/**< Verify Command */
		_otaReset_t		Reset;						/**< Reset Command */
	};
}  __attribute__ ((packed)) _otaCommand_t;

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
	sha256_t			sha256Plain;
	sha256_t			sha256Encrypted;
	_mzXfer_state_t		mzXfer_state;
	_otaCommand_t		*pCommand_mzXfer;

} host_ota_t;


static host_ota_t _hostota =
{
		.state = eHostOtaIdle,
};

/**
 * @brief	Command/Status definitions for PIC32MZ transfer
 */
enum _otaOpcode
{
	eOTA_INIT_CMD =			0x01,
	eOTA_ERASE_CMD =		0x02,
	eOTA_DATA_CMD =			0x03,
	eOTA_VERIFY_CMD =		0x04,
	eOTA_RESET_CMD =		0x05,

	eOTA_INIT_ACK =			0x41,
	eOTA_ERASE_ACK =		0x42,
	eOTA_DATA_ACK =			0x43,
	eOTA_VERIFY_ACK =		0x44,

	eOTA_INIT_NACK =		0x81,
	eOTA_ERASE_NACK =		0x82,
	eOTA_DATA_NACK =		0x83,
	eOTA_VERIFY_NACK =		0x84,
	eOTA_RESET_NACK =		0x85,
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


#ifdef	PLACE_HOLDER

/**
 * @brief Event Record Data Command handler
 *
 * Parameter data is a legacy (Model-A) Dispense Record. Received data is formatted as
 * a JSON packet then sent to the Event Record FIFO.
 *
 * @param[in]	pData	Pointer to data buffer
 * @param[in]	size	Number of parameter bytes (does not include the command OpCode)
 */
static void vUpdateEventRecordData( uint8_t *pData, uint16_t size )
{
	_dispenseRecord_t	*pDispenseRecord;
	char * jsonBuffer = NULL;

	shci_postCommandComplete( eEventRecordData, eCommandSucceeded );

	/* sanity check the data parameters */
	if( ( NULL != pData ) && ( DISPENSE_RECORD_MIN_SIZE <= size ) && ( size <= DISPENSE_RECORD_MAX_SIZE ) )
	{
		pDispenseRecord = ( _dispenseRecord_t * ) pData;						// cast pData to dispense record pointer

		/* Only process records with index greater than the last received index, or if lastReceivedIndex = -1 */
		if( pDispenseRecord->index > _evtrec.nvs.lastReceivedIndex )
		{
			_evtrec.nvs.lastReceivedIndex = pDispenseRecord->index;
			_evtrec.nvs.nextRequestIndex = _evtrec.nvs.lastReceivedIndex + 1;
			_evtrec.updateNvs = true;												// Update NVS on Event Record Task

			IotLogDebug( "Received index = %d", pDispenseRecord->index );

			/* format record as JSON */
			jsonBuffer = formatEventRecord( pDispenseRecord, size );

			/* Save record in FIFO */
			fifo_put( _evtrec.fifoHandle, jsonBuffer, strlen( jsonBuffer ) );

			IotLogInfo( jsonBuffer );

			free( jsonBuffer );						/* free mjson format buffer */
		}
		else
		{
			IotLogInfo( "Same received index, current = %d, last = %d", pDispenseRecord->index, _evtrec.nvs.lastReceivedIndex );
		}
	}
}

/**
 * @brief	Dispense Record Count update handler
 *
 * This handler will be called whenever the Dispense Record Count BLE characteristic value
 * is updated.
 *
 * NOTE: RecordCount is actually the index following the last record written.
 * i.e.	If one (16-byte) record is written at index = 0, RecordCount = 1
 *
 * @param[in]	pData	Pointer to characteristic value
 * @param[in]	size	Size, in bytes, of characteristic value
 */
static void vRecordCountUpdate( const void *pData, const uint16_t size )
{
	const uint32_t	*pCount = pData;

	_evtrec.lastReportedIndex = *pCount;
	IotLogInfo( "lastReportedIndex = %d", _evtrec.lastReportedIndex );

}
#endif

static void printsha256( const char *tag, sha256_t *sha)
{
	int i;

	printf( "SHA256%s: ", tag );
	for (i = 0; i < 32; ++i)
	{
		printf( "%02x", sha->x[ i ] );
	}
	printf("\n");
}

static void printhex( const uint8_t *pData, size_t size)
{
	size_t n;
	while( size )
	{
		n = ( size > 16 ) ? 16 : size;
		size -= n;
		while (n-- )
		{
			printf("%02X ", *pData++ );
		}
		printf("\n");
	}
}

/**
 * @brief	PIC32MZ Image Transfer
 *
 */
static void PIC32MZ_ImageTransfer( void )
{
	static size_t	imageAddress;
	static size_t	remaining;
	static size_t	size;
	static uint32_t	targetAddress;

	switch( _hostota.mzXfer_state )
	{
		case mzXfer_Idle:
			break;

		case mzXfer_Init:
			/* Allocate a command buffer */
			_hostota.pCommand_mzXfer = pvPortMalloc( sizeof( _otaCommand_t ) );

			_hostota.pCommand_mzXfer->opCode = eClientWriteCharacteristicValue;
			_hostota.pCommand_mzXfer->charHandle = OTA_COMMAND_HANDLE;

			_hostota.pCommand_mzXfer->Init.command = eOTA_INIT_CMD;
			_hostota.pCommand_mzXfer->Init.length = sizeof( _otaInit_t );
			_hostota.pCommand_mzXfer->Init.uid = 0;		/* FIXME */
			_hostota.pCommand_mzXfer->Init.crc = crc16_ccitt_compute( ( uint8_t *) _hostota.pCommand_mzXfer->Init, sizeof( _otaInit_t ) );

			/* Post SHCI Command to message queue, length is command size + 3, for the SHCI header */
			shci_PostResponse( ( uint8_t * )&_hostota.pCommand_mzXfer, ( sizeof( _otaInit_t ) + 3 ) );

			_hostota.mzXfer_state = mzXfer_Init_Ack;
			break;

		case mzXfer_Init_Ack:
			if( ack)
			{
				_hostota.mzXfer_state = mzXfer_Erase;
			}
			break;

		case mzXfer_Erase:
			_hostota.pCommand_mzXfer->opCode = eClientWriteCharacteristicValue;
			_hostota.pCommand_mzXfer->charHandle = OTA_COMMAND_HANDLE;

			_hostota.pCommand_mzXfer->Erase.command = eOTA_ERASE_CMD;
			_hostota.pCommand_mzXfer->Erase.length = sizeof( _otaErase_t );
			_hostota.pCommand_mzXfer->Erase.crc = crc16_ccitt_compute( ( uint8_t *) _hostota.pCommand_mzXfer->Erase, sizeof( _otaErase_t ) );

			/* Post SHCI Command to message queue, length is command size + 3, for the SHCI header */
			shci_PostResponse( ( uint8_t * )&_hostota.pCommand_mzXfer, ( sizeof( _otaErase_t ) + 3 ) );

			_hostota.mzXfer_state = mzXfer_Erase_Ack;
			break;

		case mzXfer_Erase_Ack:
			if( ack)
			{
				/* TODO - Initialize flash read */

				imageAddress = _hostota.partition->address + _hostota.PaddingBoundary;
				targetAddress = _hostota.LoadAddress;
				remaining = _hostota.ImageSize;

				_hostota.mzXfer_state = mzXfer_Data;
			}
			break;

		case mzXfer_Data:
			_hostota.pCommand_mzXfer->opCode = eClientWriteCharacteristicValue;
			_hostota.pCommand_mzXfer->charHandle = OTA_COMMAND_HANDLE;

			_hostota.pCommand_mzXfer->Data.command = eOTA_DATA_CMD;

			/* Read a chunk of Image from Flash */
			size = ( OTA_PKT_DLEN < remaining ) ? OTA_PKT_DLEN : remaining;
			bootloader_flash_read( imageAddress, _hostota.pCommand_mzXfer->Data.buffer, size, true );

			if( OTA_PKT_DLEN == size )				/* Full data command */
			{
				_hostota.pCommand_mzXfer->Data.length = sizeof( _otaData_t );
				_hostota.pCommand_mzXfer->Data.address = targetAddress;
				_hostota.pCommand_mzXfer->Data.crc = crc16_ccitt_compute( ( uint8_t *) _hostota.pCommand_mzXfer->Data, sizeof( _otaData_t ) );

				/* Post SHCI Command to message queue, length is command size + 3, for the SHCI header */
				shci_PostResponse( ( uint8_t * )&_hostota.pCommand_mzXfer, ( sizeof( _otaData_t ) + 3 ) );
				_hostota.mzXfer_state = mzXfer_Verify;
			}
			else if( OTA_PKT_DLEN_HALF == size )	/* Half Data command */
			{
				_hostota.pCommand_mzXfer->ShortData.length = sizeof( _otaShortData_t );
				_hostota.pCommand_mzXfer->ShortData.address = targetAddress;
				_hostota.pCommand_mzXfer->ShortData.crc = crc16_ccitt_compute( ( uint8_t *) _hostota.pCommand_mzXfer->ShortData, sizeof( _otaShortData_t ) );

				/* Post SHCI Command to message queue, length is command size + 3, for the SHCI header */
				shci_PostResponse( ( uint8_t * )&_hostota.pCommand_mzXfer, ( sizeof( _otaShortData_t ) + 3 ) );
				_hostota.mzXfer_state = mzXfer_Verify;
			}
			else
			{
				IotLogError( "Error: Image Size is not a multiple of %d", OTA_PKT_DLEN_HALF );
				_hostota.mzXfer_state = mzXfer_Error;
			}
			break;

		case mzXfer_Data_Ack:
			if( ack)
			{
				if( Done )
				{
					_hostota.mzXfer_state = mzXfer_Verify;
				}
				else
				{
					/* Set variables for next flash read */
					targetAddress += size;
					imageAddress += size;
					remaining -= size;

					_hostota.mzXfer_state = mzXfer_Data;

				}
			}
			break;


		case mzXfer_Verify:
				_hostota.pCommand_mzXfer->opCode = eClientWriteCharacteristicValue;
				_hostota.pCommand_mzXfer->charHandle = OTA_COMMAND_HANDLE;

				_hostota.pCommand_mzXfer->Verify.command = eOTA_VERIFY_CMD;
				_hostota.pCommand_mzXfer->Verify.length = sizeof( _otaErase_t );
				_hostota.pCommand_mzXfer->Verify.startAddr = _hostota.LoadAddress;
				_hostota.pCommand_mzXfer->Verify.imgLength = _hostota.ImageSize;
				memcpy( _hostota.pCommand_mzXfer->Verify.sha256hash, &_hostota.sha256Plain, 32 );
				_hostota.pCommand_mzXfer->Verify.crc = crc16_ccitt_compute( ( uint8_t *) _hostota.pCommand_mzXfer->Verify, sizeof( _otaVerify_t ) );

				/* Post SHCI Command to message queue, length is command size + 3, for the SHCI header */
				shci_PostResponse( ( uint8_t * )&_hostota.pCommand_mzXfer, ( sizeof( _otaVerify_t ) + 3 ) );

				_hostota.mzXfer_state = mzXfer_Verify_Ack;
			break;

		case mzXfer_Verify_Ack:
			if( ack)
			{
				_hostota.mzXfer_state = mzXfer_Reset;
			}
			break;

		case mzXfer_Reset:
			_hostota.pCommand_mzXfer->opCode = eClientWriteCharacteristicValue;
			_hostota.pCommand_mzXfer->charHandle = OTA_COMMAND_HANDLE;

			_hostota.pCommand_mzXfer->Reset.command = eOTA_RESET_CMD;
			_hostota.pCommand_mzXfer->Reset.length = sizeof( _otaErase_t );
			_hostota.pCommand_mzXfer->Reset.crc = crc16_ccitt_compute( ( uint8_t *) _hostota.pCommand_mzXfer->Reset, sizeof( _otaReset_t ) );

			/* Post SHCI Command to message queue, length is command size + 3, for the SHCI header */
			shci_PostResponse( ( uint8_t * )&_hostota.pCommand_mzXfer, ( sizeof( _otaReset_t ) + 3 ) );
			_hostota.mzXfer_state = mzXfer_Complete;
			break;

		case mzXfer_Complete:
			if( NULL != _hostota.pCommand_mzXfer )
			{
				/* Release buffer */
				vPortFree( _hostota.pCommand_mzXfer );
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


	vTaskDelay( 10000 / portTICK_PERIOD_MS );

	IotLogInfo( "_hostOtaTask" );

	pBuffer = pvPortMalloc( 512 );

    while( 1 )
	{

    	switch( _hostota.state )
    	{
			case eHostOtaIdle:
				/* hardwrire partition for initial development */
				_hostota.partition = esp_partition_find_first( 0x44, 0x57, "pic_fw" );
				IotLogInfo("host ota: partition address = %08X, length = %08X", _hostota.partition->address, _hostota.partition->size );

				/* Read the first 512 bytes from the Flash partition */
				bootloader_flash_read(_hostota.partition->address, pBuffer, 512, true );

				_hostota.state = eHostOtaParseJSON;
				break;

			case eHostOtaParseJSON:
				json = (char *) pBuffer;
				json_length = strlen( json );

				printf( "JSON[%d] = %s\n", json_length, json );

				mjson_get_number( json, json_length, "$.PaddingBoundary", &value );
				_hostota.PaddingBoundary = ( uint32_t ) value;
				printf( "  PaddingBoundary = %d\n", _hostota.PaddingBoundary );

				mjson_get_number( json, json_length, "$.LoadAddress", &value );
				_hostota.LoadAddress = ( uint32_t ) value;
				printf( "  LoadAddress = 0x%08X\n", _hostota.LoadAddress );

				mjson_get_number( json, json_length, "$.ImageSize", &value );
				_hostota.ImageSize = ( uint32_t ) value;
				printf( "  ImageSize = 0x%08X\n", _hostota.ImageSize );

				mjson_get_number( json, json_length, "$.Offset", &value );
				_hostota.Offset = ( uint32_t ) value;
				printf( "  Offset = 0x%08X\n", _hostota.Offset );

				mjson_get_number( json, json_length, "$.Version_MZ", &_hostota.Version_MZ );
				printf( "  Version_MZ = %f\n", _hostota.Version_MZ );

				mjson_get_base64(json, json_length, "$.SHA256Plain", &_hostota.sha256Plain, sizeof( sha256_t ) );
				printsha256( "Plain", &_hostota.sha256Plain );

				mjson_get_base64(json, json_length, "$.SHA256Encrypted", &_hostota.sha256Encrypted, sizeof( sha256_t ) );
				printsha256( "Encrypted", &_hostota.sha256Encrypted );

				_hostota.state = eHostOtaVerifyImage;
				break;

			case eHostOtaVerifyImage:

				mbedtls_sha256_init( &ctx );
				mbedtls_sha256_starts( &ctx, 0 );						/* SHA-256, not 224 */

				address = _hostota.partition->address + _hostota.PaddingBoundary;

				for( remaining = _hostota.ImageSize; remaining ; ( remaining -= size ), ( address += size ) )
				{
					/* read a block of image */
					size = ( 512 < remaining ) ? 512 : remaining;
					bootloader_flash_read( address, pBuffer, size, true );
					/* add to hash */
					mbedtls_sha256_update( &ctx, pBuffer, size );
				}
				/* get result */
				mbedtls_sha256_finish( &ctx, ( uint8_t * ) &hash );

				printsha256( "Hash", &hash);

				if( 0 == memcmp( &_hostota.sha256Encrypted, &hash, 32 ) )
				{
					IotLogInfo( "Image SHA256 Hash matches metadata SHA256Encrypted" );
					_hostota.state = eHostOtaTransfer;
				}
				else
				{
					IotLogError( "Image SHA256 Hash does not match metadata SHA256Encrypted" );
					_hostota.state = eHostOtaError;
				}
				break;

			case eHostOtaTransfer:
				PIC32MZ_ImageTransfer();
				break;

			case eHostOtaActivate:
				break;

			case eHostOtaError:
				break;

			default:
				_hostota.state = eHostOtaIdle;
				break;

    	}
    	vTaskDelay( 10 / portTICK_PERIOD_MS );
    }
}



/* ************************************************************************* */
/* ************************************************************************* */
/* **********        I N T E R F A C E   F U N C T I O N S        ********** */
/* ************************************************************************* */
/* ************************************************************************* */

/**
 * @brief Initialize Host OTA submodule
 */
int32_t host_ota_init( void )
{
//	esp_err_t	err = ESP_OK;
//	size_t size;


	/* Register callback for Dispense Record Count update */
//	bleInterface_registerUpdateCB( eDispRecCountIndex, &vRecordCountUpdate );

	/* Create Task on new thread */
    xTaskCreate( _hostOtaTask, "HostOta_task", HOST_OTA_STACK_SIZE, NULL, HOST_OTA_TASK_PRIORITY, &_hostota.taskHandle );
    if( NULL == _hostota.taskHandle )
	{
        return ESP_FAIL;
    }
    {
    	IotLogInfo( "host_ota_task created" );
    }

    return	ESP_OK;
}
