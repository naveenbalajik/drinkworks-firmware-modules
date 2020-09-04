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

#define	PHASE_0			0
#define	PHASE_1			1
#define	PHASE_2			2
#define	PHASE_3			3
#define	TEST1_STEP_PUT	0
#define	TEST1_STEP_GET	1


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

/**
 * @brief	Test Control structure
 *
 * Maintain test control state so that it can be saved in NVS and restored,
 * allowing testing to be resumed after a power cycle.
 */
typedef struct
{
	uint32_t	cycle;					/**< cycle count for complete test suite */
	uint32_t	proc;					/**< Test procedure */
	uint32_t	phase;					/**< Test phase */
	uint32_t	step;					/**< Test step */
	uint32_t	index;					/**< Test index */
	bool		bComplete;				/**< Test complete flag */
	uint32_t	error;					/**< Test error count */
} testControl_t;

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
static int32_t fifo_test1( testControl_t *pTest, fifo_handle_t fifo, uint32_t startIndex, uint32_t count )
{
	esp_err_t err = ESP_OK;

	switch( pTest->phase )
	{
		case PHASE_0:									// empty the FIFO before starting, unless resuming
			IotLogInfo( "fifo_test_1: start" );
			err = fifo_empty_test( fifo );
			IotLogInfo( "  emptied" );
			pTest->index = startIndex;					// initialize test index
			pTest->step = TEST1_STEP_PUT;				// initialize test step
			pTest->bComplete = false;					// initialize test complete flag
			pTest->error = 0;							// Clear error count
			++pTest->phase;								// Next phase
			break;

		case PHASE_1:
			switch( pTest->step )
			{
				case TEST1_STEP_PUT:
					IotLogInfo( "  index = %d, head = %d", pTest->index, fifo_getHead( fifo ) );
					if( ESP_OK == err )
					{
						IotLogDebug( "  fifo_test_1: write" );
						err = put_test_data( fifo, pTest->index );				// Write record
					}
					else
					{
						IotLogError( "  put_test_data: %d", err );
						++pTest->error;											// increment error count
					}

					if( ESP_OK == err )
					{
						IotLogDebug( "  fifo_test_1: check full" );
						if( fifo_full( fifo ) )
						{
							IotLogError( "  fifo_full error" );
							err = ESP_FAIL;
							++pTest->error;											// increment error count
						}
					}

					if( ESP_OK == err )
					{
						IotLogDebug( "  fifo_test_1: check empty" );
						if( fifo_empty( fifo ) )
						{
							IotLogError( "  fifo_empty error" );
							err = ESP_FAIL;
							++pTest->error;											// increment error count
						}
					}

					if (ESP_OK == err )
					{
						pTest->step = 1;
					}
					break;

				case TEST1_STEP_GET:
					if( ESP_OK == err )
					{
						IotLogDebug( "  fifo_test_1: get/verify" );
						err = get_verify_test_data( fifo, pTest->index );			// read/verify record
					}

					if( ESP_OK == err )
					{
						IotLogDebug( "  fifo_test_1: check size" );
						if( 0 != fifo_size( fifo ) )
						{
							IotLogError( "  fifo_size" );
							err = ESP_FAIL;
							++pTest->error;											// increment error count
						}
					}

					if( ESP_OK == err )
					{
						IotLogDebug( "  fifo_test_1: check full" );
						if( fifo_full( fifo ) )
						{
							IotLogError( "  fifo_full error" );
							err = ESP_FAIL;
							++pTest->error;											// increment error count
						}
					}

					if( ESP_OK == err )
					{
						IotLogDebug( "  fifo_test_1: check empty" );
						if( false == fifo_empty( fifo ) )
						{
							IotLogError( "  fifo_empty error" );
							err = ESP_FAIL;
							++pTest->error;											// increment error count
						}
					}

					if( ESP_OK == err )											/* Step passed */
					{
						if( pTest->index >= ( startIndex + count ) )			/* if phase is complete */
						{
							pTest->phase++;										/* increment phase */
						}
						else
						{
							pTest->index++;										/* increment index */
							pTest->step = TEST1_STEP_PUT;						/* toggle test step */
						}
					}
					break;

				default:
					break;
			}
			break;


		case PHASE_2:												/* Test is complete */
			IotLogInfo( "fifo_test1: complete" );
			if( pTest->error )
			{
				IotLogInfo( "  FAILED, error count = %d", pTest->error );
			}
			else
			{
				IotLogInfo("  PASSED" );
			}
			pTest->bComplete = true;
			break;

		default:
			break;
	}
	return err;
}

static void reset_test( testControl_t *pTest)
{
	pTest->proc = 0;
	pTest->phase = 0;
	pTest->index = 0;
	pTest->step = 0;
	pTest->error = 0;
	pTest->bComplete = false;
}

/**
 * @brief	Event FIFO test sequencer
 *
 * All tests are executed as single steps, the test being called multiple times until the test completes.
 * All looping is performed in this upper level sequencer.
 * Test parameters are maintained outside of the individual tests, and saved in NVS, allowing
 * tests to be resumed after a power cycle event.
 */
void fifo_test( void )
{
	esp_err_t err = ESP_OK;
	fifo_handle_t fifo;
	testControl_t	test = { 0 };
	bool bAllDone = false;
	size_t size = sizeof( testControl_t );

	fifo = fifo_init( NVS_PART_EDATA, "EventRecords", "EVR", FIFO_SIZE, NVS_FIFO_CONTROLS, NVS_FIFO_MAX );
	if( NULL != fifo )
	{
		IotLogInfo( "Fifo Initialization complete" );
		/* TODO: restore test controls from NVS */

		/*
		 * Restore FIFO Test parameters from NVS Storage, set to defaults if not value not currently stored in NVS
		 */
		if( ESP_OK == err)
		{
			err = NVS_Get( NVS_FIFO_TEST, &test, &size );
			if( err != ESP_OK )
			{
				reset_test( &test );									// initialize test parameters
				test.cycle = 1;											// set cycle count to one
				test.bComplete = false;

				size = sizeof( testControl_t );
				err = NVS_Set( NVS_FIFO_TEST, &test, &size );			// Update NVS
			}
		}

		if( ESP_OK == err )
		{
			if( ( 0 == test.proc ) && ( 0 == test.phase ) && ( 0 == test.index ) && ( 0 == test.step ) )
			{
				IotLogInfo( "Starting event_fifo test, cycle = %d", test.cycle );
			}
			else
			{
				IotLogInfo( "Restarting event_fifo test, cycle = %d", test.cycle );
			}

			while( !bAllDone )
			{
				switch( test.proc )
				{
					case 0:
						fifo_test1( &test, fifo, 1234, 100 );				// Run fifo test #1
						if( test.bComplete )								// When test is complete
						{
							++test.proc;									// move on to next test
						}
						break;

					default:												// All tests complete
						reset_test( &test );								// Reset test parameters so test suite can be re-run
						test.cycle++;										// increment cycle count
						bAllDone = true;									// terminate test loop
						break;
				}

				/* Save test controls to NVS */
				size = sizeof( testControl_t );
				err = NVS_Set( NVS_FIFO_TEST, &test, &size );			// Update NVS
				if( ESP_OK != err )
				{
					IotLogError( "fifo_test: error saving test parameters" );
					break;													// exit test loop
				}

				vTaskDelay( 500 / portTICK_PERIOD_MS );                     // Add delay so log messaging can catch up
			}
		}
	}
}
