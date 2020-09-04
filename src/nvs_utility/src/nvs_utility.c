/**
 * @file nvs_utility.c
 *
 * Created on: May 4, 2020
 * 		Author: nick.weber
 */

/* Standard includes. */
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"

/* PKCS#11 includes */
#include "iot_pkcs11_config.h"

/* Flash include*/
#include "nvs_flash.h"
#include "nvs_utility.h"

/* Debug Logging */
#include "nvs_logging.h"

#define NUM_NVS_ITEMS  			(sizeof(NVS_Items)/sizeof(NVS_Items[0]))

/**
 * @brief ESP32 Partition where NVS Keys are stored
 */
#define nvsKeys_PARTITION  "nvs_keys"

/**
 * @brief List all NVS Partitions with labels and encrypted flags
 */
static NVS_Partition_Details_t NVS_Partitions[] =
{
		[ NVS_PART_NVS ] =
		{
			.label = "nvs",
			.encrypted = false,
			.initialized = false,
		},
		[ NVS_PART_STORAGE ] =
		{
			.label = pkcs11configSTORAGE_PARTITION,
			.encrypted = true,
			.initialized = false,
		},
		[ NVS_PART_PDATA ] =
		{
			.label = "pdata",
			.encrypted = false,
			.initialized = false,
		},
		[ NVS_PART_XDATA ] =
		{
			.label = "xdata",
			.encrypted = true,
			.initialized = false,
		},
		[ NVS_PART_EDATA ] =
		{
			.label = "edata",
			.encrypted = false,
			.initialized = false,
		}
};

#define NUM_PARTITIONS	( sizeof( NVS_Partitions ) / sizeof( NVS_Partition_Details_t ) )

/**
* @brief All NVS items are stored in this table. Each element in the array holds the partition, partitionNamespace, and nvsKey of that element in the NVS
*
* Item indexes should be unique. If they are not unique, then the NVS functions will use the first element in the array that matches the itemIndex
*/
static const NVS_Entry_Details_t NVS_Items[] =
{
	[ NVS_CLAIM_CERT ]        = {	.type = NVS_TYPE_BLOB, 	.partition = NVS_PART_STORAGE, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "ClaimCert" },
	[ NVS_CLAIM_PRIVATE_KEY ] = {	.type = NVS_TYPE_BLOB,	.partition = NVS_PART_STORAGE, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "ClaimPKey" },
	[ NVS_FINAL_CERT ]        = { 	.type = NVS_TYPE_BLOB,	.partition = NVS_PART_STORAGE, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "FinalCert" },
	[ NVS_FINAL_PRIVATE_KEY ] = {	.type = NVS_TYPE_BLOB,	.partition = NVS_PART_STORAGE, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "FinalPKey" },
	[ NVS_THING_NAME ]        = { 	.type = NVS_TYPE_STR,	.partition = NVS_PART_STORAGE, 		.namespace = pkcs11configSTORAGE_NS, 	.nvsKey = "ThingName" },
	[ NVS_SERIAL_NUM ]        = { 	.type = NVS_TYPE_BLOB,	.partition = NVS_PART_PDATA,		.namespace = "SysParam",				.nvsKey = "SerialNumber" },
	[ NVS_FIFO_CONTROLS ]     = { 	.type = NVS_TYPE_U32,	.partition = NVS_PART_PDATA,		.namespace = "SysParam",				.nvsKey = "FifoControls" },
	[ NVS_FIFO_MAX ]          = { 	.type = NVS_TYPE_U16,	.partition = NVS_PART_PDATA,		.namespace = "SysParam",				.nvsKey = "FifoMax" },
	[ NVS_FIFO_TEST ]          = { 	.type = NVS_TYPE_BLOB,	.partition = NVS_PART_PDATA,		.namespace = "SysParam",				.nvsKey = "Fifotest" },			/**< For test purposes only */
};

#ifdef CONFIG_NVS_ENCRYPTION
	/**
	 * @brief NVS Encryption Keys, read from nvs_keys partition
	 */
	static nvs_sec_cfg_t NVS_Keys;
#endif

/**
 * @brief Initialize NVS Partition using entry from Partition Table
 *
 * Call appropriate initialization routine depending on whether NVS_ENCRYPTION is configured
 *
 * @param[in]	pPartition	Pointer to partition table entry
 *
 * @result
 * 	- ESP_OK	success
 * 	- ESP_FAIL	Invalid parameter
 * 	- error from low level driver
 */
static int32_t _init_partition(NVS_Partition_Details_t * pPartition )
{
	esp_err_t err = ESP_OK;

	if( pPartition == NULL)
	{
		err = ESP_FAIL;
	}
	else
	{
#ifdef CONFIG_NVS_ENCRYPTION
		if( pPartition->encrypted )
		{
			err = nvs_flash_secure_init_partition( pPartition->label, &NVS_Keys );
		}
		else
		{
			err = nvs_flash_secure_init_partition( pPartition->label, NULL );
		}
#else
		err = nvs_flash_init_partition( pPartition->label );
#endif
		if(err == ESP_OK){
			pPartition->initialized = true;
		}
	}

	return err;
}

/**
 * @brief Get Pointer to NVS Item in NVS_Items table
 *
 * @param[in] nvsItem	Enumerated NVS_ITEM
 * @param[out] pItem	Pointer to NVS Table Item pointer
 *
 * @return
 * 	- ESP_OK	success
 * 	- ESP_FAIL	Item not found
 */
static int32_t _getTablePointerFromNVSItem( NVS_Items_t nvsItem, const NVS_Entry_Details_t **pItem)
{
	esp_err_t err = ESP_OK;

	if( nvsItem >= NUM_NVS_ITEMS )
	{
		err = ESP_FAIL;
		IotLogError( "ERROR: NVS Item not in master NVS table" );
	}
	else
	{
		*pItem = &NVS_Items[ nvsItem ];
	}

	return err;

}


/**
 * @brief Get the handle of a specified nvs item
 *
 * @param[in] pItem			Pointer to NVS Table Item
 * @param[in] readWrite		Define handle to be NVS_READONLY or NVS_READWRITE
 * @param[out] handle		Handle of namespace within partition
 *
 * @return
 * 	- ESP_OK 	if successful
 * 	- ESP_FAIL	if failed
 */
static int32_t _getNVSnamespaceHandle( const NVS_Entry_Details_t *pItem, int32_t readWrite, nvs_handle* handle )
{
	esp_err_t err = ESP_OK;
	NVS_Partition_Details_t *pPartition;
	const int part_idx = pItem->partition;

	/* Check that partition is valid */
	if( part_idx >= NVS_PART_END )
	{
		IotLogError( "ERROR invalid Partition: %d", part_idx );
		return ESP_FAIL;
	}
	pPartition = &NVS_Partitions[ part_idx ];

//	IotLogInfo( "%s: part_idx = %d, label = %s", __func__, part_idx, pPartition->label );
	/* Initialize (un-inited) partition, use Security Configuration if partition is encrypted */
	if( pPartition->initialized == false )
	{
		IotLogInfo( "partition[%d].initialized = false", part_idx );
		err = _init_partition(  pPartition );
	}

	if( err == ESP_OK )
	{
		err = nvs_open_from_partition( pPartition->label, pItem->namespace, readWrite, handle );
	}
	else
	{
		IotLogError( "ERROR initializing flash partition", err );
	}

	if (err != ESP_OK)
	{
		/* This can happen if namespace doesn't exist yet, so no keys stored */
		IotLogError( "FAILED NVS OPEN. Namespace \"%s\" may not exist yet, err: 0x%04X", pItem->namespace, err );
	}

	return err;
}

/**
 * @brief	Initialize the NVS module
 *
 * First, read in NVS Encryption Keys. If Keys have not been initialized, or are corrupt, generate new keys.
 * Then initialize each NVS Partition in table, erasing the partition if it contains no empty pages - this
 * may happen if the NVS partition was truncated.
 *
 * @return
 * 	- ESP_OK if successful
 * 	- ESP_FAIL if nvs_keys partition does not exist, or &NVS_Keys is NULL
 * 	- Error code from esp_partition_writ/erase APIs
 * 	- Error code from the underlying flash storage driver
 */
int32_t NVS_Initialize( void )
{
	esp_err_t err = ESP_OK;

#ifdef CONFIG_NVS_ENCRYPTION
	const esp_partition_t * key_part =  esp_partition_find_first( ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, nvsKeys_PARTITION);

	IotLogInfo( "Free heap = %d", esp_get_free_heap_size() );
	/* Attempt to read Encryption Keys from nvs_keys partition */
	if( key_part != NULL )
	{
		IotLogInfo( "NVS_Keys Partition found" );
		err = nvs_flash_read_security_cfg( key_part, &NVS_Keys );
		IotLogInfo( "nvs_flash_read_security_cfg() = 0x%08X", err );
	}
	else
	{
		IotLogError( "NVS_Keys Partition not found" );
		err = ESP_FAIL;
	}

	if( ( err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED ) || ( err == ESP_ERR_NVS_CORRUPT_KEY_PART) )
	{
		if( err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED )
		{
			IotLogInfo( "NVS Keys not initialized" );
		}
		else
		{
			IotLogError( "NVS Keys corrupt" );
		}

		/* Generate NVS Keys */
		err = nvs_flash_generate_keys( key_part, &NVS_Keys );
	}

	IotLogInfo( "Free heap = %d", esp_get_free_heap_size() );
	if( err == ESP_OK )
	{
		IotLogInfo( "NVS Keys initialized" );
		IotLog_PrintBuffer( "  XTS encryption key:", ( const uint8_t * const )&NVS_Keys.eky, 32 );
		IotLog_PrintBuffer( "  XTS tweak key:", ( const uint8_t * const )&NVS_Keys.tky, 32 );
#endif

		/* Initialize each NVS partition */
		for( int i = 0; ( i < NVS_PART_END ) && ( err == ESP_OK ); ++i )
		{
		    /* Initialize NVS */
			err = _init_partition( &NVS_Partitions[ i ] );

			IotLogInfo( "Free heap = %d", esp_get_free_heap_size() );

			/* If init fails, try erasing and reinitializing */
		    if( ( err == ESP_ERR_NVS_NO_FREE_PAGES ) || ( err == ESP_ERR_NVS_NEW_VERSION_FOUND ) )
		    {
		    	IotLogInfo(" Erase NVS Partition: %s",  NVS_Partitions[ i ].label );
		        err = nvs_flash_erase_partition(  NVS_Partitions[ i ].label );

		        if(err ==  ESP_OK){
		        	NVS_Partitions[ i ].initialized = false;
		        }

				err = _init_partition( &NVS_Partitions[ i ] );
		    }

		    if( err == ESP_OK )
		    {
		    	NVS_Partitions[ i ].initialized = true;
		    	IotLogInfo("Initialized NVS Partition: %s", NVS_Partitions[ i ].label );
		    	IotLogInfo( "Free heap = %d", esp_get_free_heap_size() );
		    }
		}
#ifdef CONFIG_NVS_ENCRYPTION
	}
#endif

	return err;
}

/**
 * @brief Get the size of a specified nvs item
 *
 * @param[in] 	nvsItem		NVS item
 * @param[out] 	size		size of the NVS item
 *
 * @return 	ESP_OK if successful, -1 if failed
 */
int32_t NVS_pGet_Size_Of(  const NVS_Entry_Details_t *pItem, uint32_t* size )
{
	esp_err_t err;
	nvs_handle handle;


	err = _getNVSnamespaceHandle( pItem, NVS_READONLY, &handle );

	if( err == ESP_OK )
	{
		switch( pItem->type )
		{
			case NVS_TYPE_U8:
				*size = sizeof(uint8_t);
				break;

			case NVS_TYPE_I8:
				*size = sizeof(int8_t);
				break;

			case NVS_TYPE_U16:
				*size = sizeof(uint16_t);
				break;

			case NVS_TYPE_I16:
				*size = sizeof(int16_t);
				break;

			case NVS_TYPE_U32:
				*size = sizeof(uint32_t);
				break;

			case NVS_TYPE_I32:
				*size = sizeof(int32_t);
				break;

			case NVS_TYPE_U64:
				*size = sizeof(uint64_t);
				break;

			case NVS_TYPE_I64:
				*size = sizeof(int64_t);
				break;

			case NVS_TYPE_STR:
				err = nvs_get_str( handle, pItem->nvsKey, NULL, size );
				break;

			case NVS_TYPE_BLOB:
				err = nvs_get_blob( handle, pItem->nvsKey, NULL, size );
				break;

			default:
				break;
		}
	}

	if (err != ESP_OK)
	{
		IotLogDebug( "Unable to get NVS Item size" );
	}

	nvs_close( handle );

	return err;
}

/**
 * @brief Get the size of a specified nvs item
 *
 * @param[in] 	nvsItem		NVS item
 * @param[out] 	size		size of the NVS item
 *
 * @return 	ESP_OK if successful, -1 if failed
 */
int32_t NVS_Get_Size_Of( NVS_Items_t nvsItem, uint32_t* size )
{
	esp_err_t err;
	const NVS_Entry_Details_t * pItem;

	err = _getTablePointerFromNVSItem( nvsItem, &pItem );

	if( err == ESP_OK )
	{
		err = NVS_pGet_Size_Of( pItem, size );
	}

	return err;
}

/**
 * @brief Get the value of an nvs item using an NVS_Entry_details_t pointer
 *
 * @param[in]  pItem		Pointer to NVS_Entry_details_t item
 * @param[out] pOutput		Output value
 * @param[out] pSize		size of the output.  Only used for strings and blobs
 *
 * @return 	ESP_OK if successful, -1 if failed
 */
int32_t NVS_pGet( const NVS_Entry_Details_t *pItem, void* pOutput, void* pSize )
{
	esp_err_t err;
	nvs_handle handle;
	// Initialize output size to 0


	// Open the namespace handle
	err = _getNVSnamespaceHandle( pItem, NVS_READONLY, &handle );

	if( err == ESP_OK )
	{
		switch( pItem->type )
		{
			case NVS_TYPE_U8:
				err = nvs_get_u8( handle, pItem->nvsKey, pOutput );
				break;

			case NVS_TYPE_I8:
				err = nvs_get_i8( handle, pItem->nvsKey, pOutput );
				break;

			case NVS_TYPE_U16:
				err = nvs_get_u16( handle, pItem->nvsKey, pOutput );
				break;

			case NVS_TYPE_I16:
				err = nvs_get_i16( handle, pItem->nvsKey, pOutput );
				break;

			case NVS_TYPE_U32:
				err = nvs_get_u32( handle, pItem->nvsKey, pOutput );
				break;

			case NVS_TYPE_I32:
				err = nvs_get_i32( handle, pItem->nvsKey, pOutput );
				break;

			case NVS_TYPE_U64:
				err = nvs_get_u64( handle, pItem->nvsKey, pOutput );
				break;

			case NVS_TYPE_I64:
				err = nvs_get_i64( handle, pItem->nvsKey, pOutput );
				break;

			case NVS_TYPE_STR:
				err = nvs_get_str( handle, pItem->nvsKey, pOutput, pSize );
				break;

			case NVS_TYPE_BLOB:
				err = nvs_get_blob( handle, pItem->nvsKey, pOutput, pSize );
				break;

			default:
				IotLogError( "NVS ERROR: unknown Item type");
				err = ESP_FAIL;
				break;
		}
	}

	if( err != ESP_OK )
	{
		IotLogError( "ERROR getting NVS Item" );
	}

	nvs_close( handle);

	return err;
}

/**
 * @brief Get the value of a specified nvs item
 *
 * @param[in] nvsItem		NVS item
 * @param[out] pOutput		Output value
 * @param[out] pSize		size of the output.  Only used for strings and blobs
 *
 * @return 	ESP_OK if successful, -1 if failed
 */
int32_t NVS_Get( NVS_Items_t nvsItem, void* pOutput, void* pSize )
{
	esp_err_t err;
	// Initialize output size to 0
	const NVS_Entry_Details_t *pItem;

	err = _getTablePointerFromNVSItem( nvsItem, &pItem );

	// Use pItem to perform "Get"
	if( err == ESP_OK )
	{
		err = NVS_pGet( pItem, pOutput, pSize );
	}

	return err;
}

/**
 * @brief Set the value of an NVS item using an NVS_Entry_details_t pointer
 *
 * Only set the value in nvs if the value is different then what is currently stored in nvs
 *
 * @param[in] pItem			Pointer to NVS_Entry_Details_t item
 * @param[in] pInput		Input value to set
 * @param[in] pSize			size of the input. Only used for blobs
 *
 * @return 	ESP_OK if successful, -1 if failed
 */
int32_t NVS_pSet( const NVS_Entry_Details_t *pItem, void* pInput, void* pSize )
{
	esp_err_t err;
	nvs_handle handle;
	void *currentVal = NULL;
	uint32_t currentSize = 0;

	// Open the namespace handle
	err = _getNVSnamespaceHandle( pItem, NVS_READWRITE, &handle );

	if( err == ESP_OK )
	{
		switch( pItem->type )
		{
			case NVS_TYPE_U8:
				currentVal = pvPortMalloc( sizeof(uint8_t) );
				err = NVS_pGet( pItem, currentVal, NULL );

				if( err != ESP_OK || memcmp( pInput, currentVal, sizeof(uint8_t) ) != 0 )
				{
					err = nvs_set_u8( handle, pItem->nvsKey, *(uint8_t*)pInput );
				}
				else
				{
					IotLogDebug( "NVS item %s already set to %u, ignoring write", pItem->nvsKey, *(uint8_t*)pInput );
				}

				vPortFree( currentVal );
				break;

			case NVS_TYPE_I8:
				currentVal = pvPortMalloc( sizeof(int8_t) );
				err = NVS_pGet( pItem, currentVal, NULL);

				if( err != ESP_OK || memcmp( pInput, currentVal, sizeof(int8_t) ) != 0 )
				{
					err = nvs_set_i8( handle, pItem->nvsKey, *(int8_t*)pInput );
				}
				else
				{
					IotLogInfo( "NVS item %s already set to %d, ignoring write", pItem->nvsKey, *(int8_t*)pInput );
				}

				vPortFree( currentVal );
				break;

			case NVS_TYPE_U16:
				currentVal = pvPortMalloc( sizeof(uint16_t) );
				err = NVS_pGet( pItem, currentVal, NULL );

				if( err != ESP_OK || memcmp( pInput, currentVal, sizeof(uint16_t) ) != 0 )
				{
					err = nvs_set_u16( handle, pItem->nvsKey, *(uint16_t*)pInput );
				}
				else
				{
					IotLogDebug( "NVS item %s already set to %u, ignoring write", pItem->nvsKey, *(uint16_t*)pInput );
				}

				vPortFree( currentVal );
				break;

			case NVS_TYPE_I16:
				currentVal = pvPortMalloc( sizeof(int16_t) );
				err = NVS_pGet( pItem, currentVal, NULL );

				if( err != ESP_OK || memcmp( pInput, currentVal, sizeof(int16_t) ) != 0 )
				{
					err = nvs_set_i16( handle, pItem->nvsKey, *(int16_t*)pInput );
				}
				else
				{
					IotLogInfo( "NVS item %s already set to %d, ignoring write", pItem->nvsKey, *(int16_t*)pInput );
				}

				vPortFree( currentVal );
				break;

			case NVS_TYPE_U32:
				currentVal = pvPortMalloc( sizeof(uint32_t) );
				err = NVS_pGet( pItem, currentVal, NULL );

				if( err != ESP_OK || memcmp(pInput, currentVal, sizeof(uint32_t)) != 0 )
				{
					err = nvs_set_u32( handle, pItem->nvsKey, *(uint32_t*)pInput );
				}
				else
				{
					IotLogInfo( "NVS item %s already set to %u, ignoring write", pItem->nvsKey, *(uint32_t*)pInput );
				}

				vPortFree( currentVal );
				break;

			case NVS_TYPE_I32:
				currentVal = pvPortMalloc( sizeof(int32_t) );
				err = NVS_pGet( pItem, currentVal, NULL );

				if( err != ESP_OK || memcmp( pInput, currentVal, sizeof(int32_t) ) != 0 )
				{
					err = nvs_set_i32( handle, pItem->nvsKey, *(int32_t*)pInput );
				}
				else
				{
					IotLogInfo( "NVS item %s already set to %d, ignoring write", pItem->nvsKey, *(int32_t*)pInput );
				}

				vPortFree( currentVal );
				break;

			case NVS_TYPE_U64:
				currentVal = pvPortMalloc( sizeof(uint64_t) );
				err = NVS_pGet( pItem, currentVal, NULL );

				if( err != ESP_OK || memcmp(pInput, currentVal, sizeof(uint64_t)) != 0 )
				{
					err = nvs_set_u64( handle, pItem->nvsKey, *(uint64_t*)pInput );
				}
				else
				{
					IotLogInfo( "NVS item %s already set to %llu, ignoring write", pItem->nvsKey, *(uint64_t*)pInput );
				}

				vPortFree( currentVal );
				break;

			case NVS_TYPE_I64:
				currentVal = pvPortMalloc( sizeof(int64_t) );
				err = NVS_pGet( pItem, currentVal, NULL );

				if( err != ESP_OK || memcmp( pInput, currentVal, sizeof(int64_t) ) != 0 )
				{
					err = nvs_set_i64( handle, pItem->nvsKey, *(int64_t*)pInput );
				}
				else{
					IotLogInfo( "NVS item %s already set to %lld, ignoring write", pItem->nvsKey, *(int64_t*)pInput );
				}

				vPortFree( currentVal );
				break;

			case NVS_TYPE_STR:
				err = NVS_pGet_Size_Of( pItem, &currentSize );

				if( err == ESP_OK )
				{
					currentVal = pvPortMalloc( currentSize );
				}

				if( err == ESP_OK )
				{
					err = NVS_pGet( pItem, currentVal, &currentSize );
				}

				if( err != ESP_OK || memcmp( pInput, currentVal, currentSize ) != 0 )
				{
					err = nvs_set_str( handle, pItem->nvsKey, pInput );
				}
				else
				{
					IotLogInfo( "NVS item %s already set to requested string, ignoring write", pItem->nvsKey );
				}

				if( currentVal != NULL )
				{
					vPortFree( currentVal );
				}
				break;

			case NVS_TYPE_BLOB:
				if( pSize == NULL )
				{
					err = ESP_FAIL;
					break;
				}
				IotLogDebug( "NVS_Set, Blob, pSize = %d", *(size_t *)pSize);

				err = NVS_pGet_Size_Of( pItem, &currentSize );

				if( err == ESP_OK )
				{
					currentVal = pvPortMalloc( currentSize );
				}

				if( err == ESP_OK )
				{
					err = NVS_pGet( pItem, currentVal, &currentSize );
				}

				if( err != ESP_OK || memcmp( pInput, currentVal, *(size_t*)pSize ) != 0 )
				{
					err = nvs_set_blob( handle, pItem->nvsKey, pInput, *(size_t*)pSize );
				}
				else
				{
					IotLogInfo( "NVS item %s already set to requested blob, ignoring write", pItem->nvsKey );
				}

				if( currentVal != NULL )
				{
					vPortFree( currentVal );
				}
				break;

			default:
				IotLogError( "NVS ERROR, unknown Item type" );
				err = ESP_FAIL;
				break;
		}
	}

	if( err == ESP_OK )
	{
		err = nvs_commit( handle );
	}
	else
	{
		IotLogError( "ERROR setting NVS Item" );
	}

	if( err != ESP_OK )
	{
		IotLogError( "ERROR Committing Handle" );
	}

	nvs_close( handle );

	return err;
}

/**
 * @brief Set the value of a specified nvs item
 *
 * Only set the value in nvs if the value is different then what is currently stored in nvs
 *
 * @param[in] nvsItem		NVS item
 * @param[in] pInput		Input value to set
 * @param[in] pSize			size of the input. Only used for blobs
 *
 * @return 	ESP_OK if successful, -1 if failed
 */
int32_t NVS_Set( NVS_Items_t nvsItem, void* pInput, void* pSize )
{
	esp_err_t err;
	const NVS_Entry_Details_t *pItem;

	err = _getTablePointerFromNVSItem( nvsItem, &pItem );

	// Open the namespace handle
	if( err == ESP_OK )
	{
		err = NVS_pSet( pItem, pInput, pSize );
	}

	return err;
}

/**
 * @brief Erase a NVS key
 *
 * @param[in] nvsItem		NVS item to be erased
 *
 * @return 	ESP_OK if successful, -1 if failed
 */
int32_t NVS_EraseKey(NVS_Items_t nvsItem)
{
	esp_err_t err;
	nvs_handle handle;
	const NVS_Entry_Details_t *pItem;

	err = _getTablePointerFromNVSItem( nvsItem, &pItem );

	// Open the namespace handle
	if( err == ESP_OK )
	{
		err = _getNVSnamespaceHandle( pItem, NVS_READWRITE, &handle );
	}


	// Erasing item
	if( err == ESP_OK )
	{
		err = nvs_erase_key( handle, pItem->nvsKey );
	}

	if( err == ESP_OK )
	{
		err = nvs_commit( handle );
	}
	else
	{
		IotLogError( "ERROR deleting Key" );
	}

	if( err != ESP_OK )
	{
		IotLogError( "ERROR Committing Handle" );
	}

	nvs_close( handle );

	return err;
}
