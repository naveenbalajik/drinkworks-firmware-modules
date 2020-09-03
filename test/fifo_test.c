/**
 * @file	fifo_test.c
 *
 * Test functions for event fifo
 */

#include	<stdint.h>
#include	<string.h>
#include	"event_fifo.h"
#include	"nvs_utility.h"
#include	"esp_err.h"

/* Debug Logging */
#include "nvs_logging.h"

#define		FIFO_SIZE		500

const static char testRecordTemplate[] =
		"{"
			"\"Index\": %d,"
			"\"DateTime\": \"2019-02-22T10:35:12\","
			"\"Status\": 7,"
			"\"CatalogID\": 177,"
			"\"BeverageID\": 846,"
			"\"CycleTime\": 29.37,"
			"\"PeakPressure\": 3.74,"
			"\"FirmwareVersion\": 0.0,"
			"\"FreezeEvents\": 0"
		"}";


/* Event FIFO Testing
 *
 * Use NVS items to hold test parameters so that test can be interrupted with power-cycle events
 * and resume where it left off.
 *
 * 1.	Test complete FIFO
 * 		a) For record count = 2 x FIFO_SIZE
 * 		b) Write unique data
 * 		c) Verify fifo_size = 1, fifo_full = 0, fifo_empty = 0
 * 		d) Read record
 * 		e) Verify fifo_size = 0, fifo_full = 0, fifo_empty = 1
 * 		f) Compare data
 * 		g) Next record
 *
 * 	2.	Fill FIFO
 * 		a) For record count = FIFO_SIZE
 * 		b) Write unique data
 * 		c) Verify fifo_size, fifo_full, fifo_empty
 * 		d) Next record
 * 		e) While not fifo_empty
 * 		f) Read record
 * 		g) Verify fifo_size, fifo_full, fifo_empty
 * 		h) Compare data
 * 		i) Next record
 *
 * 	3.	Over-fill FIFO
 * 		a) For record count = FIFO_SIZE + N
 * 		b) Write unique data
 * 		c) Verify fifo_size, fifo_full, fifo_empty
 * 		d) Next record
 * 		e) While not fifo_empty
 * 		f) Read record
 * 		g) Verify fifo_size, fifo_full, fifo_empty
 * 		h) Compare data
 * 		i) Next record
 *
 */

static char *test_data( uint32_t index )
{
	char *buffer;
	size_t size;
	int n;

	/* Allocate storage for test data */
	size = sizeof( testRecordTemplate ) + 10;
	buffer = pvPortMalloc( size );

	if( NULL != buffer )
	{
		n = snprintf( buffer, size, testRecordTemplate, index );

		if( ( 0 > n ) || ( n >= size ) )				// If n is negative or greater than or equal to buffer size
		{
			vPortFree( buffer );
			buffer = NULL;											// string formatting error
		}

	}
	return buffer;
}

static int32_t put_test_data( fifo_handle_t fifo, uint32_t index )
{
	esp_err_t err = ESP_OK;
	char *testrecord;

	testrecord = test_data( index );									// generate test record
	if( NULL != testrecord )
	{
		err = fifo_put( fifo, testrecord, strlen( testrecord ) );
		if( ESP_OK != err )
		{
			IotLogError( " put_test_data: error storing test record: %d", index );
		}
		vPortFree( testrecord );
	}
	else
	{
		IotLogError( " put_test_data: error generating test record %d", index );
		err = ESP_FAIL;
	}
	return err;
}

static int32_t get_verify_test_data( fifo_handle_t fifo, uint32_t index )
{
	esp_err_t err = ESP_OK;
	char *testrecord;
	char *readrecord;
	size_t size = sizeof( testRecordTemplate ) + 10;

	testrecord = test_data( index );									// generate test record

	if( NULL != testrecord )
	{
		readrecord = pvPortMalloc( size );

		if( NULL != readrecord )
		{
			err = fifo_get( fifo, readrecord, &size );
			if( ESP_OK != err )
			{
				IotLogError( " get_verify_test_data: error reading test record: %d", index );
			}
			else
			{
				if( size != strlen( testrecord ) )
				{
					IotLogError( " get_verify_test_data: size mismatch record: %d (%d vs %d)", index, size, strlen( testrecord ) );
					err = ESP_FAIL;
				}
				if( ( ESP_OK == err ) && ( 0 != memcmp( testrecord, readrecord, size ) ) )
				{
					IotLogError( " get_verify_test_data: data mismatch record: %d", index );
					err = ESP_FAIL;
				}
			}
			vPortFree( readrecord );
		}
		vPortFree( testrecord );
	}
	else
	{
		IotLogError( " get_verify_test_data: error generating test record %d", index );
		err = ESP_FAIL;
	}
	return err;

}

static int32_t fifo_empty_test( fifo_handle_t fifo )
{
	esp_err_t err = ESP_OK;
	char *readrecord;
	size_t size = sizeof( testRecordTemplate ) + 10;

	IotLogDebug( "fifo_empty_test" );

	if( 0 == fifo_size( fifo ) )
	{
		IotLogDebug( "  FIFO is empty" );
	}
	else
	{
		readrecord = pvPortMalloc( size );									// allocate a buffer

		while( fifo_size( fifo ) && ( ESP_OK == err ) )						// while fifo is not empty, and there are no errors
		{
			IotLogInfo( "  size = %d", fifo_size( fifo ) );

			err = fifo_get( fifo, readrecord, &size );						// read record
			if( ESP_OK != err )
			{
				IotLogError( " fifo_empty_test: error reading record" );
			}
		};

		vPortFree( readrecord );											// free buffer
	}
	return err;
}

/**
 *  * 1.	Test complete FIFO
 * 		a) For record count = 2 x FIFO_SIZE
 * 		b) Write unique data
 * 		c) Verify fifo_size = 1, fifo_full = 0, fifo_empty = 0
 * 		d) Read record
 * 		e) Verify fifo_size = 0, fifo_full = 0, fifo_empty = 1
 * 		f) Compare data
 * 		g) Next record
 *
 */
static int32_t fifo_test1( fifo_handle_t fifo, uint32_t startIndex, uint32_t count )
{
	esp_err_t err = ESP_OK;
	uint32_t index;

	IotLogInfo( "fifo_test_1: start" );
	err = fifo_empty_test( fifo );												// empty the FIFO before starting, unless resuming
	IotLogInfo( "  emptied" );

	for( index = startIndex; ( index < ( startIndex + count ) && ( ESP_OK == err ) ); ++index )
	{
		IotLogInfo( "  index = %d, head = %d", index, fifo_getHead( fifo ) );

		if( ESP_OK == err )
		{
			IotLogDebug( "  fifo_test_1: write" );
			err = put_test_data( fifo, index );									// Write record
		}

		if( ESP_OK == err )
		{
			IotLogDebug( "  fifo_test_1: check size" );
			if( 1 != fifo_size( fifo ) )
			{
				IotLogError( "  fifo_size" );
				err = ESP_FAIL;
			}
		}
		else
		{
			IotLogError( "  put_test_data: %d", err );
		}

		if( ESP_OK == err )
		{
			IotLogDebug( "  fifo_test_1: check full" );
			if( fifo_full( fifo ) )
			{
				IotLogError( "  fifo_full error" );
				err = ESP_FAIL;
			}
		}

		if( ESP_OK == err )
		{
			IotLogDebug( "  fifo_test_1: check empty" );
			if( fifo_empty( fifo ) )
			{
				IotLogError( "  fifo_empty error" );
				err = ESP_FAIL;
			}
		}

		if( ESP_OK == err )
		{
			IotLogDebug( "  fifo_test_1: get/verify" );
			err = get_verify_test_data( fifo, index );							// read/verify record
		}

		if( ESP_OK == err )
		{
			IotLogDebug( "  fifo_test_1: check size" );
			if( 0 != fifo_size( fifo ) )
			{
				IotLogError( "  fifo_size" );
				err = ESP_FAIL;
			}
		}

		if( ESP_OK == err )
		{
			IotLogDebug( "  fifo_test_1: check full" );
			if( fifo_full( fifo ) )
			{
				IotLogError( "  fifo_full error" );
				err = ESP_FAIL;
			}
		}

		if( ESP_OK == err )
		{
			IotLogDebug( "  fifo_test_1: check empty" );
			if( false == fifo_empty( fifo ) )
			{
				IotLogError( "  fifo_empty error" );
				err = ESP_FAIL;
			}
		}
		vTaskDelay( 500 / portTICK_PERIOD_MS );						// Add delay so log messaging can catch up
	}
	return err;
}

void fifo_test( void )
{
	esp_err_t err = ESP_OK;
	fifo_handle_t fifo;

	IotLogInfo( "Starting event_fifo test" );

	fifo = fifo_init( NVS_PART_EDATA, "EventRecords", "EVR", FIFO_SIZE, NVS_FIFO_HEAD, NVS_FIFO_TAIL, NVS_FIFO_FULL, NVS_FIFO_MAX );
	if( NULL != fifo )
	{
		IotLogInfo( "Fifo Initialization complete" );
		err = fifo_test1( fifo, 1234, 100 );
		if( ESP_OK != err )
		{
			IotLogError( " FIFO Test 1 Failed" );
		}
		else
		{
			IotLogInfo( " FIFO Test 1 Passed" );
		}
	}
}
