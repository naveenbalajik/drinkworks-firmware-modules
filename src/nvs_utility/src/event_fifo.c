/**
 * @file	event_fifo.c
 */

#include	<stdint.h>
#include	<stdbool.h>
#include	<stdio.h>
#include	<string.h>
#include	"event_fifo.h"
#include	"nvs_utility.h"
#include	"esp_err.h"

/* Debug Logging */
#include "nvs_logging.h"

#define	MAX_NAMESPACE_LENGTH	15												/**< Maximum length of null-terminated Namespace string */
#define	MAX_KEY_LENGTH			15												/**< Maximum length of null-terminated NVS Key string */
#define	MAX_KEYSUFFIX_LENGTH	4												/**< Maximum length of formatted NVS Key suffix */
#define	MAX_KEYPREFIX_LENGTH	( MAX_KEY_LENGTH - MAX_KEYSUFFIX_LENGTH )		/**< MAximum length of null-terminated NVS Key prefix */

#define		FIFO_NOT_FULL	0
#define		FIFO_FULL		1

/*
 * NVS Key is formatted using snprintf( key, MAX_KEY_LENGTH, "%s%d", prefix, suffix )
 */

struct fifo_s
{
	union
	{
		struct
		{
			uint32_t head : 15;					/**< Head Index, elements are added at this location */
			uint32_t full : 1;					/**< flag, true when buffer is full */
			uint32_t tail : 15;					/**< Tail Index, elements are removed at this location */
			uint32_t : 1;						/**< spare bit */
		};
		uint32_t	controls;					/**< All fifo control items consolidated into 32-bit item */
	};
	uint16_t max;								/**< Size of the buffer */
	NVS_Entry_Details_t entry;
	NVS_Items_t	controlsKey;
	NVS_Items_t	maxKey;

	char namespace[ MAX_NAMESPACE_LENGTH ];
	char keyPrefix[ MAX_KEYPREFIX_LENGTH ];
	char key[ MAX_KEY_LENGTH ];
};

#define	MAX_KEY_SIZE	8					/**< Maximum Blob Key size, including null-terminator */
/* ************************************************************************** */
/* ************************************************************************** */
/* Section: Local Functions                                                   */
/* ************************************************************************** */
/* ************************************************************************** */

/**
 * @brief	Save FIFO controls in NVS
 *
 * Save the FIFO head, tail and full items in NVS
 *
 * @param[in] fifo	Handle of FIFO
 * @return
 * 	- ESP_OK if controls are saved without issue
 * 	- ESP_FAIL if error saving controls
 */
static int32_t save_controls( fifo_handle_t fifo)
{
	esp_err_t err = ESP_OK;

	if( fifo == NULL)
	{
		err = ESP_FAIL;
	}

	if( ESP_OK == err )
	{
		err = NVS_Set( fifo->controlsKey, &fifo->controls, NULL );			// Update controls in NVS
		IotLogDebug( "save_controls, head = %d, tail = %d, full = %d", fifo->head, fifo->tail, fifo->full );
	}

	return err;
}

/************************************************************************/
/**
 * @brief	Advance Head Pointer
 *
 * Advance the Head pointer.  If the buffer is full, the oldest element in the
 * buffer (accessed by the tail index) is overwritten and thus lost.
 *
 * @param[in] fifo	Handle of FIFO
 * @return
 * 	- ESP_OK if all pointers are saved without issue
 * 	- ESP_FAIL if error saving pointers
 */
/************************************************************************/
static int32_t advance_pointer( fifo_handle_t fifo )
{
	esp_err_t err;

	if( fifo == NULL )
	{
		IotLogError( "advance_pointer: Error" );
		err = ESP_FAIL;
	}
	else
	{
		if( fifo->full )
		{
			fifo->tail = (fifo->tail + 1) % fifo->max;
		}

		fifo->head = (fifo->head + 1) % fifo->max;
		fifo->full = (fifo->head == fifo->tail) ? FIFO_FULL : FIFO_NOT_FULL;
		IotLogDebug( "advance_pointer, head = %d, tail = %d, full = %d", fifo->head, fifo->tail, fifo->full );
		err = save_controls( fifo );
	}

	return err;
}


/************************************************************************/
/**
 * @brief	Retreat Tail Pointer
 *
 * Retreat the Tail pointer.  The <i>full</i> flag is cleared.
 *
 * @param[in] fifo	Handle of FIFO
 * @return
 * 	- ESP_OK if all pointers are saved without issue
 * 	- ESP_FAIL if error saving pointers
 */
/************************************************************************/
static int32_t retreat_pointer( fifo_handle_t fifo )
{
	esp_err_t err;

	if( fifo == NULL )
	{
		IotLogError( "retreat_pointer: Error" );
		err = ESP_FAIL;
	}
	else
	{
		fifo->full = FIFO_NOT_FULL;
		fifo->tail = (fifo->tail + 1) % fifo->max;
		err = save_controls( fifo );
	}

	return	err;
}

/**
 * @brief	Format Key to access blob
 *
 *	Example Blob Key:	"EVR256"
 *	An 8-byte buffer will accommodate keys up to "EVR9999"
 *
 * @param[in] 	fifo	Fifo handle
 * @param[in]	index	Key Index
 * @return
 * 		- <i>true</i> if key string is completely written
 * 		- <i>false</i> if formatting error
 */
static bool formatKey( fifo_handle_t fifo, const size_t index )
{
	int n;

	if( fifo == NULL )
	{
		IotLogError( "formatKey: error" );
		return false;												//  error
	}
	else
	{

		n = snprintf( fifo->key, sizeof( fifo->key ), "%s%d", fifo->keyPrefix, index );

		if( (0 < n) && ( sizeof( fifo->key ) > n ) )				// If n is non-negative and less than buffer size
		{
			fifo->entry.nvsKey = fifo->key;
			IotLogDebug( "formatKey: %s", fifo->entry.nvsKey );
			return true;											// string completely written
		}
		else
		{
			return false;											// string formatting error
		}
	}
}

/* ************************************************************************** */
/* ************************************************************************** */
/* Section: Interface Functions                                               */
/* ************************************************************************** */
/* ************************************************************************** */

/************************************************************************/
/**
 * @brief		Initialize a FIFO
 *
 *  - A FIFO is created in designated NVS <i>Partition</i> and <i>Name-space</i>.
 *  - Elements will be stored using the designated <i>keyPrefix</i>.
 *  - The maximum number of elements to be stored is <i>nItems</i>
 *  - NVS keys <i>headKey, tailKey, fullKey, maxKey</i> will be used to store the FIFO pointers
 *  - If the FIFO has been previously created, the pointers will be restored from NVS
 *
 * @param[in] partition		enumerated NVS Partition
 * @param[in] namespace		Namespace string
 * @param[in] keyPrefix		NVS access Key Prefix, string
 * @param[in] nItems		Number of items FIFO can hold
 * @param[in] controlKey	NVS key for storing fifo control data
 * @param[in] maxKey		NVS key for storing max pointer
 *
 * @return		FIFO handle, NULL if error encountered during initialization
 */
/************************************************************************/
fifo_handle_t fifo_init(const NVS_Partitions_t partition, const char * namespace, const char *keyPrefix, const uint16_t nItems, NVS_Items_t controlsKey, NVS_Items_t maxKey )
{
	esp_err_t	err = ESP_OK;

	void *buffer = NULL;

	/* Allocate storage for FIFO control structure */
	buffer = pvPortMalloc( sizeof( fifo_t ) );

	if( buffer == NULL )
	{
		IotLogError( "fifo_init: Error\n" );
		return NULL;
	}

	fifo_handle_t fifo = ( fifo_handle_t ) buffer;

	fifo->controlsKey = controlsKey;
	fifo->maxKey = maxKey;

	fifo->entry.partition = partition;

	/* Save namespace */
	if( MAX_NAMESPACE_LENGTH > strlen( namespace ) )
	{
		strcpy( fifo->namespace, namespace );
		fifo->entry.namespace = (const char *) fifo->namespace;
	}
	else
	{
		IotLogError( "Namespace string is too long" );
		err= ESP_FAIL;
	}

	/* save NVS Key Prefix */
	if( ESP_OK == err)
	{
		if( MAX_KEYPREFIX_LENGTH > strlen( keyPrefix ) )
		{
			strcpy( fifo->keyPrefix, keyPrefix );
		}
		else
		{
			IotLogError( "keyPrefix string is too long" );
			err= ESP_FAIL;
		}
	}

	fifo->entry.type = NVS_TYPE_BLOB;																	/* FIFO items are Blobs */

	/*
	 * Restore FIFO controls from NVS Storage, set to defaults if not value not currently stored in NVS
	 */
	if( ESP_OK == err)
	{
		err = NVS_Get( fifo->controlsKey, &fifo->controls, NULL );
		if( err != ESP_OK )
		{
			fifo->head = 0;
			fifo->tail = 0;
			fifo->full = FIFO_NOT_FULL;
			err = NVS_Set( fifo->controlsKey, &fifo->controls, NULL );			// Update NVS
		}
	}

	/*
	 * Restore FIFO Max index from NVS Storage, use function argument nItems if not value not currently stored in NVS
	 */
	if( ESP_OK == err)
	{
		err = NVS_Get( fifo->maxKey, &fifo->max, NULL );
		if( err != ESP_OK )
		{
			fifo->max = nItems;
			err = NVS_Set( fifo->maxKey, &fifo->max, NULL );				// Update NVS
		}
	}

	IotLogInfo( "Initializing FIFO:" );
	IotLogInfo( "  Handle = %p", ( (uint32_t *) fifo ) );
	IotLogInfo( "  Head = %d", fifo->head );
	IotLogInfo( "  Tail = %d", fifo->tail );
	IotLogInfo( "  Full = %d", fifo->full);
	IotLogInfo( "  Max  = %d", fifo->max );

	return ( ( ESP_OK == err ) ? fifo : NULL );
}

/************************************************************************/
/**
 * @brief	Reset FIFO
 *
 * @param[in] fifo	FIFO handle
 */
/************************************************************************/
void fifo_reset( fifo_handle_t fifo )
{
	if( fifo == NULL )
	{
		IotLogError( "fifo_reset: Error\n" );
	}
	else
	{
		fifo->head = 0;
		fifo->tail = 0;
		fifo->full = FIFO_NOT_FULL;
	}
}

/************************************************************************/
/**
 * @brief	FIFO full?
 *
 * @param[in] fifo	FIFO handle
 * @return
 *		- <i>true</i> if buffer is full
 *		- <i>false</i> if buffer is not full
 */
/************************************************************************/
bool fifo_full( fifo_handle_t fifo )
{
	if( fifo == NULL )
	{
		IotLogError( "fifo_full: Error\n" );
		return true;
	}
	else
	{
		return ( FIFO_FULL == fifo->full );
	}
}

/************************************************************************/
/**
 * @brief	FIFO empty?
 *
 * @param[in] fifo	FIFO handle
 * @return
 *		- <i>true</i> if buffer is empty
 *		- <i>false</i> if buffer is not empty
 */
/************************************************************************/
bool fifo_empty( fifo_handle_t fifo )
{
	if( fifo == NULL )
	{
		IotLogError( "fifo_empty: Error\n" );
		return false;
	}
	else
	{
		return( (FIFO_NOT_FULL == fifo->full) && ( fifo->head == fifo->tail ) );
	}
}

/************************************************************************/
/**
 * @brief	FIFO Capacity
 *
 * @param[in] fifo	FIFO handle
 * @return	Size of FIFO (number of elements)
 */
/************************************************************************/
uint16_t fifo_capacity( fifo_handle_t fifo )
{
	if( fifo == NULL )
	{
		IotLogError( "fifo_capacity: Error\n" );
		return 0;
	}
	else
	{
		return fifo->max;
	}
}

/************************************************************************/
/**
 * @brief	FIFO Size
 *
 * @param[in] fifo	FIFO handle
 * @return	Number of elements used in FIFO
 */
/************************************************************************/
uint16_t fifo_size( fifo_handle_t fifo )
{
	uint16_t size = fifo->max;

	if( fifo == NULL )
	{
		IotLogError( "fifo_size: Error\n" );
		size = 0;
	}
	else
	{
		if( FIFO_NOT_FULL == fifo->full )
		{
			if( fifo->head >= fifo->tail )
			{
				size = ( fifo->head - fifo->tail );
			}
			else
			{
				size = ( fifo->max + fifo->head - fifo->tail );
			}
		}
	}
	return size;
}

/************************************************************************/
/**
 * @brief	Add Blob to FIFO
 *
 * If the FIFO is full, the oldest Blob in the FIFO (accessed by
 * the tail index) is overwritten and thus lost.
 *
 * @param[in] fifo		FIFO handle
 * @param[in] blob		Pointer to Blob to be added to the FIFO
 * @param[in] length	Length of binary value to set, in bytes
 * @return
 * 	- ESP_OK if next element stored without error
 * 	- ESP_FAIL if error storing element
 */
/************************************************************************/
int32_t fifo_put( fifo_handle_t fifo, const void* blob, size_t length)
{
	esp_err_t err = ESP_OK;


	if( ( fifo == NULL ) || ( blob == NULL ) )
	{
		IotLogError( "fifo_put: Error\n" );
		err = ESP_FAIL;
	}

	if( ( ESP_OK == err ) && !formatKey( fifo, fifo->head ) )
	{
		IotLogError( "fifo_get: key format error" );
		err = ESP_FAIL;
	}

	if( ESP_OK == err )
	{
		err = NVS_pSet( &fifo->entry, blob, &length );
		IotLogDebug( "NVS_pSet: %d", err );
	}

	if( ESP_OK == err )
	{
		err = advance_pointer( fifo );
		IotLogDebug( "advance_pointer: %d", err );
	}

	return err;
}

/************************************************************************/
/**
 * @brief	Get next available element from the FIFO
 *
 * The next available element is accessed via the <i>tail</i> pointer.
 *
 * @param[in]		fifo	FIFO handle
 * @param[out]		blob	Destination pointer
 * @param[in|out]	pLength	Length of destination buffer as input, number of bytes copies as output
 * @return
 * 	- ESP_OK if next element retrieved without error
 * 	- ESP_FAIL if error retrieving element
 */
/************************************************************************/
int32_t fifo_get( fifo_handle_t fifo, void* blob, size_t *pLength )
{
	esp_err_t	err = ESP_OK;

	if( ( fifo == NULL ) || ( blob == NULL ) )
	{
		IotLogError( "fifo_get: Error\n" );
		err = ESP_FAIL;
	}

	if( ( ESP_OK == err ) && fifo_empty( fifo ) )
	{
		IotLogInfo( "fifo_get: empty" );
		err = ESP_FAIL;
	}

	if( ( ESP_OK == err ) && !formatKey( fifo, fifo->tail ) )
	{
		IotLogError( "fifo_put: key format error" );
		err = ESP_FAIL;
	}

	if( ESP_OK == err )
	{
		err = NVS_pGet( &fifo->entry, blob, pLength );
	}

	if( ESP_OK == err )
	{
		err = retreat_pointer( fifo );
	}

	return err;
}

/**
 * @brief	Getter for FIFO Head
 *
 * Only intended of test usage
 */
uint16_t fifo_getHead( fifo_handle_t fifo)
{
	return fifo->head;
}

/* *****************************************************************************
 End of File
 */
