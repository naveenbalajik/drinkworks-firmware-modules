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

#define		TEST_START_INDEX	0x471				/**< random start index */

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
	uint32_t	startIndex;				/**< Test start index, roll from test to test, and cycle to cycle */
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

/**
 * @brief	Create Test Data Record
 *
 * Create a data record, in JSON format, using index.
 * A buffer in allocated from the heap, and must be freed after the buffer is no
 * longer needed.
 *
 * @param[in]	index	Record index, used for creating record
 * @return		Pointer to buffer containing created record
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

/**
 * @brief	Write Record to FIFO
 *
 * @param[in]	fifo	FIFO handle
 * @param[in]	index	Record index, used for creating record
 * @return		ESP_OK on success
 */
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

/**
 * @brief	Get Record and Verify data against expected value
 *
 * @param[in]	fifo	FIFO handle
 * @param[in]	index	Record index, used for data comparison
 * @return		ESP_OK on success
 */
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
			vPortFree( readrecord );					// free read buffer
		}
		vPortFree( testrecord );						// free compare buffer
	}
	else
	{
		IotLogError( " get_verify_test_data: error generating test record %d", index );
		err = ESP_FAIL;
	}
	return err;

}


/**
 * @brief	Empty the FIFO
 *
 * Read records until fifo_size is zero
 *
 * @param[in]	fifo	FIFO handle
 * @return		ESP_OK on success
 */
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

		if( NULL != readrecord )
		{
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
		else
		{
			IotLogError( " fifo_empty_test: malloc failed" );
			err = ESP_FAIL;
		}
	}
	return err;
}

/**
 * @brief	FIFO Test #1 - Alternately write and read records
 *
 * Test complete FIFO
 * 		a) For record count = 2 x FIFO_SIZE
 * 		b) Write unique data
 * 		c) Verify fifo_size = 1, fifo_full = 0, fifo_empty = 0
 * 		d) Read record
 * 		e) Verify fifo_size = 0, fifo_full = 0, fifo_empty = 1
 * 		f) Compare data
 * 		g) Next record
 *
 * param[in|out] 	pTest		Pointer to test control parameters
 * param[in]		fifo		FIFO handle
 * param[in]		startIndex	Starting record index, used for creating/verifying records
 * param[in]		count		Number of records to Write/Read
 * @return		ESP_OK on success
 */
static int32_t fifo_test1( testControl_t *pTest, fifo_handle_t fifo, uint32_t startIndex, uint32_t count )
{
	esp_err_t err = ESP_OK;

	switch( pTest->phase )
	{
		case PHASE_0:									// empty the FIFO before starting
			IotLogInfo( "fifo_test1: start" );
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
						IotLogDebug( "  fifo_test1: write" );
						err = put_test_data( fifo, pTest->index );				// Write record
					}

					if( ESP_OK != err )
					{
						IotLogError( "  put_test_data: %d", err );
						++pTest->error;											// increment error count
					}

					if( ESP_OK == err )
					{
						IotLogDebug( " fifo_test1: check size" );
						if( 1 != fifo_size( fifo ) )							// expect size to be 1
						{
							IotLogError( "  fifo_size error" );
							err = ESP_FAIL;
							++pTest->error;										// increment error count
						}
					}

					if( ESP_OK == err )
					{
						IotLogDebug( "  fifo_test1: check full" );
						if( fifo_full( fifo ) )
						{
							IotLogError( "  fifo_full error" );
							err = ESP_FAIL;
							++pTest->error;											// increment error count
						}
					}

					if( ESP_OK == err )
					{
						IotLogDebug( "  fifo_test1: check empty" );
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
						IotLogDebug( "  fifo_test1: get/verify" );
						err = get_verify_test_data( fifo, pTest->index );			// read/verify record
					}

					if (ESP_OK == err )
					{
						++pTest->index;												// read complete, increment index
					}
					else
					{
						IotLogError( "  get_verify_test_data: %d", err );
						++pTest->error;												// increment error count
					}

					if( ESP_OK == err )
					{
						IotLogDebug( "  fifo_test1: check size" );
						if( 0 != fifo_size( fifo ) )
						{
							IotLogError( "  fifo_size" );
							err = ESP_FAIL;
							++pTest->error;											// increment error count
						}
					}

					if( ESP_OK == err )
					{
						IotLogDebug( "  fifo_test1: check full" );
						if( fifo_full( fifo ) )
						{
							IotLogError( "  fifo_full error" );
							err = ESP_FAIL;
							++pTest->error;											// increment error count
						}
					}

					if( ESP_OK == err )
					{
						IotLogDebug( "  fifo_test1: check empty" );
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

/**
 * @brief	FIFO Test 2
 *
 * This test writes a number of records into the FIFO, then reads them back.
 *
 * If the number of records written is less than FIFO_SIZE, fifo_full should not be set
 * and all the records should be able to be read back.
 *
 * If the number of records written is equal to FIFO_SIZE, fifo_full should be set
 * and all the records should be able to be read back.
 *
 * If the number of records written is greater than FIFO_SIZE, fifo_full should be set
 * but only the last FIFO_SIZE records should be able to be read back.
 *
 * In each case, after all the available records are read back, fifo_empty should be true.
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
 * param[in|out] 	pTest		Pointer to test control parameters
 * param[in]		fifo		FIFO handle
 * param[in]		startIndex	Starting record index, used for creating/verifying records
 * param[in]		count		Number of records to Write/Read
 * @return		ESP_OK on success
 */
static int32_t fifo_test2( testControl_t *pTest, fifo_handle_t fifo, uint32_t startIndex, uint32_t count )
{
	esp_err_t err = ESP_OK;
	uint32_t	nRecords;

	switch( pTest->phase )
	{
		case PHASE_0:									// empty the FIFO before starting
			IotLogInfo( "fifo_test2: start" );
			err = fifo_empty_test( fifo );
			IotLogInfo( "  emptied" );
			pTest->index = startIndex;					// initialize test index
			pTest->step = 0;							// initialize test step
			pTest->bComplete = false;					// initialize test complete flag
			pTest->error = 0;							// Clear error count
			++pTest->phase;								// Next phase
			break;

		case PHASE_1:									// Write Records, no steps required
			IotLogInfo( "  Write, index = %d, head = %d, size = %d", pTest->index, fifo_getHead( fifo ), fifo_size( fifo ) );
			if( ESP_OK == err )
			{
				IotLogDebug( "  fifo_test2: write" );
				err = put_test_data( fifo, pTest->index );				// Write record
			}

			if( ESP_OK == err )
			{
				++pTest->index;											// write completed, increment index
			}
			else
			{
				IotLogError( "  put_test_data: %d", err );
				++pTest->error;											// increment error count
			}

			if( ESP_OK == err )
			{
				IotLogDebug( "  fifo_test2: check capacity" );
				if( FIFO_SIZE != fifo_capacity( fifo ) )
				{
					IotLogError( "  fifo_capacity error" );
					err = ESP_FAIL;
					++pTest->error;											// increment error count
				}
			}


			if( ESP_OK == err )
			{
				IotLogDebug( "  fifo_test2: check size" );
				nRecords = pTest->index - startIndex;						// records written

				if( ( ( nRecords <= FIFO_SIZE ) && ( nRecords != fifo_size( fifo ) ) ) ||
					( ( nRecords == FIFO_SIZE ) && ( FIFO_SIZE != fifo_size( fifo ) ) ) )
				{
					IotLogError( "  fifo_size error" );
					err = ESP_FAIL;
					++pTest->error;											// increment error count
				}
			}


			if( ESP_OK == err )
			{
				IotLogDebug( "  fifo_test2: check full" );
				/*
				 * fifo_full should be set after FIFO_SIZE records have been written
				 * otherwise it should be clear
				 */
				if( ( ( FIFO_SIZE != fifo_size( fifo ) ) && (  fifo_full( fifo ) ) ) ||
					( ( FIFO_SIZE == fifo_size( fifo ) ) && ( !fifo_full( fifo ) ) ) )
				{
					IotLogError( "  fifo_full error" );
					err = ESP_FAIL;
					++pTest->error;											// increment error count
				}
			}

			if( ESP_OK == err )
			{
				IotLogDebug( "  fifo_test2: check empty" );
				if( fifo_empty( fifo ) )
				{
					IotLogError( "  fifo_empty error" );
					err = ESP_FAIL;
					++pTest->error;											// increment error count
				}
			}

			if (ESP_OK == err )											/* iteration passed */
			{
				if( pTest->index >= ( startIndex + count ) )			/* if phase is complete */
				{
					if( fifo_full( fifo ) )								/* if FIFO is full */
					{
						pTest->index -= fifo_capacity( fifo );			/* Adjust for overwritten records */
						IotLogInfo( "  fifo_test2: %d records overwritten", (pTest->index - startIndex ) );
					}
					else
					{
						pTest->index = startIndex;						/* set index to start index */
					}

					pTest->phase++;										/* increment phase */
				}
			}
			break;

		case PHASE_2:									// Read/Verify Records, no steps required
			IotLogInfo( "  Get, index = %d, tail = %d, size = %d", pTest->index, fifo_getTail( fifo ), fifo_size ( fifo ) );
			if( ESP_OK == err )
			{
				IotLogDebug( "  fifo_test2: get/verify" );
				err = get_verify_test_data( fifo, pTest->index );			// read/verify record
			}

			if( ESP_OK == err )
			{
				++pTest->index;												// read complete, increment index
			}
			else
			{
				IotLogError( "  get_verify_test_data: %d", err );
				++pTest->error;												// increment error count
			}

			if( ESP_OK == err )
			{
				/*
				 * Check size, size = (startIndex + Count - pTest->index)
				 */
				IotLogDebug( "  fifo_test2: check size" );
				if( ( startIndex + count - pTest->index ) != fifo_size( fifo ) )
				{
					IotLogError( "  fifo_size" );
					err = ESP_FAIL;
					++pTest->error;											// increment error count
				}
			}

			if( ESP_OK == err )
			{
				IotLogDebug( "  fifo_test2: check full" );
				if( fifo_full( fifo ) )
				{
					IotLogError( "  fifo_full error" );
					err = ESP_FAIL;
					++pTest->error;											// increment error count
				}
			}

			if( ESP_OK == err )
			{
				IotLogDebug( "  fifo_test2: check empty" );
				/*
				 * Check empty, since fifo_size was checked above, if fifo_size is zero,
				 * fifo_empty should be true.
				 */
				if( ( ( 0 == fifo_size( fifo ) ) && !fifo_empty( fifo ) ) ||
					( ( 0 != fifo_size( fifo ) ) &&  fifo_empty( fifo ) ) )
				{
					IotLogError( "  fifo_empty error" );
					err = ESP_FAIL;
					++pTest->error;											// increment error count
				}
			}

			if( ESP_OK == err )											/* Iteration passed */
			{
				if( pTest->index >= ( startIndex + count ) )			/* if phase is complete */
				{
					pTest->phase++;										/* increment phase */
				}
			}
			break;

		case PHASE_3:												/* Test is complete */
			IotLogInfo( "fifo_test2: complete" );
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


/**
 * @brief	Reset test parameters
 *
 * All test-level parameters, except for cycle count, are reset to default values
 *
 * param[in]	pTest	Pointer to test parameter structure
 */
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
				test.startIndex = TEST_START_INDEX;						// initialize start index
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
						fifo_test1( &test, fifo, test.startIndex, ( FIFO_SIZE * 2 ) );		// Run fifo test #1
						break;

					case 1:
						fifo_test2( &test, fifo, test.startIndex, 10 );						// short run of test #2
						break;

					case 2:
						fifo_test2( &test, fifo, test.startIndex, FIFO_SIZE );				// test #2 - exactly fill FIFO
						break;

					case 3:
						fifo_test2( &test, fifo, test.startIndex, ( FIFO_SIZE + 15 ) );		// test #2 - over fill FIFO
						break;

					default:												// All tests complete
						reset_test( &test );								// Reset test parameters so test suite can be re-run
						test.cycle++;										// increment cycle count
						bAllDone = true;									// terminate test loop
						break;
				}

				if( test.bComplete )										// When test is complete
				{
					test.startIndex = test.index;							// set start index for next test, to ending index of previous test
					test.phase = 0;
					test.index = 0;
					test.step = 0;
					test.error = 0;
					test.bComplete = false;
					++test.proc;											// move on to next test
				}

				/* Save test controls to NVS */
				size = sizeof( testControl_t );
				err = NVS_Set( NVS_FIFO_TEST, &test, &size );			// Update NVS
				if( ESP_OK != err )
				{
					IotLogError( "fifo_test: error saving test parameters" );
					break;													// exit test loop
				}

				vTaskDelay( 250 / portTICK_PERIOD_MS );                     // Add delay so log messaging can catch up
			}
		}
	}
}
