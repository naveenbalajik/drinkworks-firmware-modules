/**
 * @file	event_records.c
 *
 *	For Model-A, Dispense Records are created and saved by the host processor to serial flash.
 *	This module accesses those records, sequentially, starting at index 0, formats the data
 *	as JSON records, and stores those Event Records in a local FIFO.
 *
 *	Asynchronously, this module reads the Event Records from the FIFO and pushes them to
 *	AWS, using an MQTT topic.
 *
 * Created on: September 3, 2020
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
#include	"shadow_updates.h"

/* Debug Logging */
#include "event_record_logging.h"

extern int getUTC( char * buf, size_t size);


#define	ISO8601_BUFF_SIZE	21				/**< Buffer size to hold ISO 8601 Date/Time string, including null-terminator */

#define	TICKS_PER_SECOND	120				/**< Ticks per second, to convert Elapsed Time to Seconds */

/**
 *	@brief	Device Information structure, extended for ESP32, returned by Read Device Information command
 */
typedef struct
{
	_shciOpcode_t		opCode;				/**< SHCI OpCode */
	uint32_t			index;				/**< Event Record Index */
} __attribute__((packed)) _eventRecordWriteIndex_t;

/**
 * @brief	Enumerated Dispense Record Status
 */
enum RecordStatus
{
	eNoError =								0x00,
	eUnknown_Error =						0x01,
	eTop_of_Tank_Error =					0x02,
	eCarbonator_Fill_Timeout_Error =		0x03,
	eOver_Pressure_Error =					0x04,
	eCarbonation_Timeout_Error =			0x05,
	eError_Recovery_Brew =					0x06,
	eHandle_Lift_Error =					0x07,
	ePuncture_Mechanism_Error =				0x08,
	eCarbonation_Mechanism_Error =			0x09,
	eCleaning_Cycle_Completed =				0x80,
	eRinsing_Cycle_Completed =				0x81,
	eCO2_Module_Attached =					0x82,
	eFirmware_Update_Passed =				0x83,
	eFirmware_Update_Failed =				0x84,
	eDrain_Cycle_Complete =					0x85,
	eFreezeEventUpdate =					0x86,
	eCritical_Error_OverTemp =				0x87,
	eCritical_Error_PuncMechFail =			0x88,
	eCritical_Error_TrickleFillTmout =		0x89,
	eCritical_Error_ClnRinCWTFillTmout =	0x8A,
	eCritical_Error_ExtendedOPError = 		0x8B,
	eCritical_Error_BadMemClear = 			0x8C,
	eBLE_ModuleReset = 						0xE0,		/**< Module reset detected by comparing Firmware Version Characteristic */
	eBLE_IdleStatus =						0xE1,		/**< Module reporting Idle Status, unexpectedly */
	eBLE_StandbyStatus =					0xE2,		/**< Module reporting Standby Status, unexpectedly */
	eBLE_ConnectedStatus =					0xE3,		/**< Module reporting Connected Status, unexpectedly */
	eBLE_HealthTimeout =					0xE4,		/**< Module did not respond to a health check within timeout period */
	eBLE_ErrorState =						0xE5,		/**< Resetting from Error State, entered from CONFIG or SETUP */
	eBLE_MultiConnectStat =					0xE6,		/**< Multiple Connected Status messages received when in WaitForConnection state */
	eBLE_MaxCriticalTimeout =				0xE7,		/**< Consecutive timeouts of critical response commands exceeds threshold */
	eUnknownStatus =						0xFF
};
typedef	uint8_t	_recordStatus_t;

/*
 * @brief	Look-up table to convert enumerated Dispense Record Status values to text strings
 *
 */
typedef struct
{
	const _recordStatus_t	status;
	const char *text;
} _recordStatusEntry_t;

const static _recordStatusEntry_t recordStatusTable[] =
{
		{ eNoError,								"Dispense Completed" },
		{ eUnknown_Error,						"Error: Unknown" },
		{ eTop_of_Tank_Error,					"Error: Top-of-Tank" },
		{ eCarbonator_Fill_Timeout_Error,		"Error: Carbonator Fill Timeout" },
		{ eOver_Pressure_Error,					"Error: Over Pressure" },
		{ eCarbonation_Timeout_Error,			"Error: Carbonation Timeout" },
		{ eError_Recovery_Brew,					"Error: Recovery Brew" },
		{ eHandle_Lift_Error,					"Error: Handle Lift" },
		{ ePuncture_Mechanism_Error,			"Error: Puncture Mechanism" },
		{ eCarbonation_Mechanism_Error,			"Error: Carbonation Mechanism" },
		{ eCleaning_Cycle_Completed,			"Cleaning Cycle Completed" },
		{ eRinsing_Cycle_Completed,				"Rinsing Cycle Completed" },
		{ eCO2_Module_Attached,					"CO2 Cylinder Attached" },
		{ eFirmware_Update_Passed,				"Firmware Update Passed" },
		{ eFirmware_Update_Failed,				"Firmware Update Failed" },
		{ eDrain_Cycle_Complete,				"Drain Cycle Complete" },
		{ eFreezeEventUpdate,					"Freeze Event Update" },
		{ eCritical_Error_OverTemp,				"Critical Error: OverTemp" },
		{ eCritical_Error_PuncMechFail,			"Critical Error: PuncMechFail" },
		{ eCritical_Error_TrickleFillTmout,		"Critical Error: TrickleFillTmout" },
		{ eCritical_Error_ClnRinCWTFillTmout,	"Critical Error: ClnRinCWTFillTmout" },
		{ eCritical_Error_ExtendedOPError,		"Critical Error: ExtendedOPError" },
		{ eCritical_Error_BadMemClear,			"Critical Error: BadMemClear" },
		{ eBLE_ModuleReset,						"BLE: ModuleReset" },
		{ eBLE_IdleStatus,						"BLE: IdleStatus" },
		{ eBLE_StandbyStatus,					"BLE: StandbyStatus" },
		{ eBLE_ConnectedStatus,					"BLE: ConnectedStatus" },
		{ eBLE_HealthTimeout,					"BLE: HealthTimeout" },
		{ eBLE_ErrorState,						"BLE: ErrorState" },
		{ eBLE_MultiConnectStat,				"BLE: MultiConnectStat" },
		{ eBLE_MaxCriticalTimeout,				"BLE: MaxCriticalTimeout" },
		{ eUnknownStatus,						"Unknown Status" }
};

typedef enum
{
	ePublishRead,
	ePublishWaitComplete,
	ePublishWaitShadowUpdate
} _publishStates_t;



/**
 * @brief Drinkworks Dispense Record Data characteristic format
 */
typedef struct
{
	int32_t		index;															/**< Record Index */
	uint16_t	year;															/**< Year: e.g. 2018 */
	uint8_t		month;															/**< Month:	1 - 12 */
	uint8_t		date;															/**< Day-of-Month: 1 - 31 */
	uint8_t		hour;															/**< Hour: 00 - 23 */
	uint8_t		minute;															/**< Minute: 00 - 59 */
	uint8_t		second;															/**< Second: 00 - 59 */
	uint8_t		Status;															/**< Status: 0 = success, otherwise error code */
	uint16_t	PodId;															/**< Recipe IF (barcode #2) */
	uint16_t	ElapsedTime;													/**< Elapsed time, ticks */
	uint16_t	PeakPressure;													/**< Peak carbonator pressure, ADC count */
	uint16_t	SKUId;															/**< Catalog ID (barcode #1) (SKU Code) */
	uint16_t	FirmwareVersion;												/**< Firmware Version, e.g. 241 for "v2.41" (optional) */
	uint32_t	FreezeEvents;													/**< Freeze events count (optional) */
	uint16_t	CwtTemperature;													/**< Cold Water Tank Temperature, ADC count (optional) */
} __attribute__ ((packed))  _dispenseRecord_t;

#define	DISPENSE_RECORD_MAX_SIZE	( sizeof( _dispenseRecord_t ) )
#define	DISPENSE_RECORD_MIN_SIZE	( DISPENSE_RECORD_MAX_SIZE - 8 )			// subtract out optional bytes

#define	CWT_ENTRY_MIN_SIZE			( DISPENSE_RECORD_MAX_SIZE )				// Minimum size for a CWT entry
#define	FREEZE_ENTRY_MIN_SIZE		( DISPENSE_RECORD_MAX_SIZE - 2 )			// Minimum size for a FreezeEvents entry
#define	FIRMWARE_ENTRY_MIN_SIZE		( DISPENSE_RECORD_MAX_SIZE - 6 )			// Minimum size for a Firmware entry

#define	EVENT_RECORD_STACK_SIZE    ( 3076 )

#define	EVENT_RECORD_TASK_PRIORITY	( 5 )

static const char recordSeparator[] = ", ";
static const char recordFooter[] = "]}}";
#define	footerSize	( sizeof( recordFooter ) )
#define	MAX_EVENT_RECORD_SIZE	512			//256
#define	MAX_RECORDS_PER_MESSAGE	10
static char rawBuffer[64];						//57 is good for 28 bytes

/**
 * @brief	Event Record control structure
 */
typedef struct
{
	TaskHandle_t		taskHandle;												/**< handle for Event Record Task */
	fifo_handle_t		fifoHandle;												/**< FIFO handle */

	struct event_record_nvs_s
	{
		int32_t			lastReceivedIndex;										/**< Record Index received in last Event Record */
		int32_t			nextRequestIndex;										/**< Record Index to be used for next request */
	} nvs;																		/**< Control items to be stored in NVS, as a blob */

	bool				updateNvs;												/**< true: nvs items need to be saved in NVS */
	uint32_t			lastReportedIndex;										/**< last Record Index reported by Host */
	int32_t  			lastRequestIndex;										/**< Record Index used for last request */
	NVS_Items_t			key;													/**< NVS key to save Event Record nvs items */
	time_t				contextTime;											/**< Time value used as context for MQTT callback */
	_publishStates_t	publishState;											/**< Publish Event Record state-machine state */
	bool				publishComplete;										/**< Flag set to true when MQTT publish completes */
	bool				publishSuccess;											/**< Flag set to true when MQTT publish is successful */
	bool				shadowUpdateComplete;									/**< Flag set to true when Shadow Update completes */
	bool				shadowUpdateSuccess;									/**< Flag set to true when Shadow Update is successful */
	int32_t				highestReadIndex;										/**< Highest Record Index value read from FIFO */
	int32_t				lastPublishedIndex;										/**< Last/Highest Record Index successfully published, stored in NVS */
} event_records_t;


static event_records_t _evtrec =
{
	.updateNvs = false,
	.lastReportedIndex = 0,
	.lastRequestIndex = -1,
	.publishState = ePublishRead,
	.highestReadIndex = -1,
};

/**
 * @brief	MQTT topics for Event Records (develop and production)
 *
 * Basic Ingest, which is a lower cost data flow, uses Rule names for the topic
 * starting with "$aws/rules/"
 */
const char EventRecordPublishTopicDevelop[] = "$aws/rules/Homebar_event_record_devel";
const char EventRecordPublishTopicPoduction[] = "$aws/rules/Homebar_event_record_prod";

/**
 * @brief	Shadow variable for index of last published event record
 */
const char shadowLastPublishedIndex[] = "LastPublishedIndex";

/**
 * @brief	Format Byte Array as a hex string
 *
 *	A static format buffer is used, and the array formated.
 *
 *	@param[in]	pData	Pointer to byte array
 *	@param[in]	size	Size of byte array to be printed
 */
const char* pszNibbleToHex = {"0123456789ABCDEF"};

static char * formatHexByteArray(const uint8_t *pData, size_t size )
{
	int		i;
	char	*ptr;
	uint8_t	nibble;

	/* use static buffer */
	ptr = &rawBuffer[0];

	/* format each byte */
	for (i = 0; i < size; ++i)
	{
		nibble = *pData >> 4;							// High Nibble
		*ptr++ = pszNibbleToHex[ nibble ];
		nibble = *pData & 0x0f;							// Low Nibble
		*ptr++ = pszNibbleToHex[ nibble ];
		pData++;
	}
	*ptr = '\0';										// terminate

	return rawBuffer;
}

/**
 * @brief	Look-up text for status value
 *
 * @param[in]	Status value
 * @return		Pointer to textual string
 */
static const char * statusText( uint8_t status)
{
	int i;

	for( i = 0; 1 ; ++i )
	{
		/* terminate search on match or reaching end of list */
		if( ( eUnknownStatus == status ) || ( recordStatusTable[ i ].status == status ) )
		{
			break;
		}
	}
	return( recordStatusTable[ i ].text );
}

/**
 * @brief	Format Date/Time of Dispense Record using ISO 8601 standard
 *
 * A buffer is allocated from the heap to hold the formatted string.
 * Buffer must be freed by caller.
 * Example output:	2020-09-07T11:20:15Z
 *
 * @param[in]	p	Pointer to Dispense Record
 *
 * @return		Pointer to formatted ASCII string, NULL if any error encountered
 */
static char * formatDateTime( _dispenseRecord_t *p )
{
	int n;
	char *buffer = pvPortMalloc( ISO8601_BUFF_SIZE );		/* allocate buffer */

	if( NULL != buffer )
	{
		/* Validate the Date/Time fields */
		if( ( 2000 <= p->year ) && ( 2099 >= p->year ) &&
			( 0 < p->month )    && ( 12 >= p->month) &&
			( 0 < p->date )     && ( 31 >= p->date) &&
			( 23 >= p->hour) &&
			( 59 >= p->minute) &&
			( 59 >= p->second) )
		{
			n = snprintf(buffer, ISO8601_BUFF_SIZE, "%4d-%02d-%02dT%02d:%02d:%02dZ",  p->year,  p->month,  p->date,  p->hour,  p->minute,  p->second);

			/* check for incomplete formatting */
			if( ( 0 > n ) || ( ISO8601_BUFF_SIZE <= n ) )
			{
				vPortFree( buffer );						// release buffer
				buffer = NULL;
			}
		}

	}
	return buffer;
}

/**
 * @brief	convertTemperature
 *
 * Convert ADC Temperature value to Celcius
 * Equations for Calculating Temperature in °C from A/D value(ADC = x):
 * 		<= 17°C:	°C = 0.000002383038x^2 + 0.012865539727x - 33.744428921424
 * 		>  17°C:	°C = 0.00000000000467700355x^4 - 0.00000004668271047472x^3 + 0.000180445916138172x^2 - 0.294237511718683x + 167.820530124722
 *
 * @param[in]	adc		Temperature value in ADC count
 * @return		Temperature in degrees Celcius
 */
static double convertTemperature( uint16_t adc)
{
	const double a1 = -33.744428921424;
	const double b1 = 0.012865539727;
	const double c1 = 0.000002383038;

	const double a2 = 167.820530124722;
	const double b2 = -0.294237511718683;
	const double c2 = 0.000180445916138172;
	const double d2 = -0.0000000466827104747;
	const double e2 = 0.00000000000467700355;

	double celcius;

	if (adc < 1059) celcius = -20.0;                                    // Min permitted
	else if (adc > 3693) celcius = 63.0;                                // Max permitted
	else if (adc <= 2642)                                               // <= 17°C
	{
		celcius = (c1 * adc * adc) + (b1 * adc) + a1;
	}
	else                                                                //  >  17°C
	{
		celcius = (e2 * adc * adc * adc * adc) + (d2 * adc * adc * adc) + (c2 * adc * adc) + (b2 * adc) + a2;
	}
	return celcius;
}


/**
 * @brief	convertPressure
 *
 * Convert ADC to Pressure value in PSI
 *
 * @param[in]	ADC		Pressure reading in ADC count
 * @return		Pressure in PSI
 */
static double convertPressure( uint16_t ADC)
{
	double PeakPressure = 0;

    if (ADC != 0)
    {
        PeakPressure = (((double)(ADC)) * 0.04328896) - 15.95;    // convert from ADC to PSI
    }

    return PeakPressure;
}

/**
 * @brief Format Event Record Data as JSON
 *
 *	Format buffer is allocated from heap.
 *	Caller must free allocated buffer after use.
 *
 * @param[in]	pDispenseRecord		Pointer to Dispense Record
 * @param[in]	size				Dispense Record size, in bytes
 * @return		Pointer to formatted JSON record
 */
static char *formatEventRecord( _dispenseRecord_t	*pDispenseRecord, uint16_t size )
{
	char * pCommon = NULL;
	char * pSpecial = NULL;
	char * pJSON = NULL;

	int32_t	lenCommon = 0, lenSpecial = 0;

	/* format Date/Time per ISO 8601 */
	char *DateTimeString = formatDateTime( pDispenseRecord );
	char *RawString = formatHexByteArray( ( uint8_t * ) pDispenseRecord, size );

	switch( pDispenseRecord->Status )
	{
		/* Normal Dispense Records */
		case	eNoError:
		case	eUnknown_Error:
		case	eTop_of_Tank_Error:
		case	eCarbonator_Fill_Timeout_Error:
		case	eOver_Pressure_Error:
		case	eCarbonation_Timeout_Error:
		case	eError_Recovery_Brew:
		case	eHandle_Lift_Error:
		case	ePuncture_Mechanism_Error:
		case	eCarbonation_Mechanism_Error:
			/* Cold Water Tank Temperature (optional, only present if sufficient size) */
			if( CWT_ENTRY_MIN_SIZE <= size )
			{
				lenSpecial = mjson_printf( &mjson_print_dynamic_buf, &pSpecial,
						"{%Q:%d, %Q:%d, %Q:%d, %Q:%f, %Q:%f}",
						"CatalogID",       pDispenseRecord->PodId,
						"BeverageID",      pDispenseRecord->SKUId,
						"CycleTime",       pDispenseRecord->ElapsedTime / TICKS_PER_SECOND,
						"PeakPressure",    convertPressure( pDispenseRecord->PeakPressure ),
						"CwtTemperature",  convertTemperature( pDispenseRecord->CwtTemperature)
						);
			}
			else
			{
				lenSpecial = mjson_printf( &mjson_print_dynamic_buf, &pSpecial,
						"{%Q:%d, %Q:%d, %Q:%d, %Q:%f}",
						"CatalogID",       pDispenseRecord->PodId,
						"BeverageID",      pDispenseRecord->SKUId,
						"CycleTime",       pDispenseRecord->ElapsedTime / TICKS_PER_SECOND,
						"PeakPressure",    convertPressure( pDispenseRecord->PeakPressure )
						);
			}
			break;

		/* Firmware Update */
		case	eFirmware_Update_Passed:
			/* Firmware Version (optional, only present if sufficient size) */
			if( FIRMWARE_ENTRY_MIN_SIZE <= size )
			{
				lenSpecial = mjson_printf( &mjson_print_dynamic_buf, &pSpecial,
						"{%Q:%f}",
						"FirmwareVersion", ( ( ( double ) pDispenseRecord->FirmwareVersion ) / 100 )
						);
			}
			break;

		/* Freeze Event Update (optional, only present if sufficient size) */
		case	eFreezeEventUpdate:
			if( FREEZE_ENTRY_MIN_SIZE <= size )
			{
				lenSpecial = mjson_printf( &mjson_print_dynamic_buf, &pSpecial,
						"{%Q:%d}",
						"FreezeEvents",    pDispenseRecord->FreezeEvents
						);
			}
			break;

		/* Extended Pressure */
		case	eCritical_Error_ExtendedOPError:
			lenSpecial = mjson_printf( &mjson_print_dynamic_buf, &pSpecial,
					"{%Q:%f}",
					"PeakPressure",    convertPressure( pDispenseRecord->PeakPressure )
					);
			break;

		/* Over Temperature */
		case	eCritical_Error_OverTemp:
			if( CWT_ENTRY_MIN_SIZE <= size )
			{
				lenSpecial = mjson_printf( &mjson_print_dynamic_buf, &pSpecial,
						"{%Q:%f}",
						"CwtTemperature",  convertTemperature( pDispenseRecord->CwtTemperature)
						);
			}
			break;

		/* Error, status is valid */
		case	eCleaning_Cycle_Completed:
		case	eRinsing_Cycle_Completed:
		case	eCO2_Module_Attached:
		case	eFirmware_Update_Failed:
		case	eDrain_Cycle_Complete:
		case	eCritical_Error_PuncMechFail:
		case	eCritical_Error_TrickleFillTmout:
		case	eCritical_Error_ClnRinCWTFillTmout:
		case	eCritical_Error_BadMemClear:
		case	eBLE_ModuleReset:
		case	eBLE_IdleStatus:
		case	eBLE_StandbyStatus:
		case	eBLE_ConnectedStatus:
		case	eBLE_HealthTimeout:
		case	eBLE_ErrorState:
		case	eBLE_MultiConnectStat:
		case	eBLE_MaxCriticalTimeout:
			break;

		/* Unknown Status */
		default:
			pDispenseRecord->Status = eUnknownStatus;
			/* fall through */

	}

	/* format the common elements */
	lenCommon = mjson_printf( &mjson_print_dynamic_buf, &pCommon,
			"{%Q:%d, %Q:%Q, %Q:%d, %Q:%Q, %Q:%Q}",
			"Index",           pDispenseRecord->index,
			"DateTime",        DateTimeString,
			"Status",          pDispenseRecord->Status,
			"StatusText",      statusText( pDispenseRecord->Status ),
			"raw",             RawString
			);

	vPortFree( DateTimeString );				/* free date/time buffer */

	/* Merge the common and special buffers */
	mjson_merge(pCommon, lenCommon, pSpecial, lenSpecial, mjson_print_dynamic_buf, &pJSON);

	/* Free format buffers */
	free( pCommon );
	free( pSpecial );

	return( pJSON );
}


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

//			IotLogInfo( jsonBuffer );
			printf( jsonBuffer );

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

/**
 * @brief	Request Event Record
 *
 * Request Event Record at <i>index</i>.
 * <i>index</i> is sent to host using <i>eEventRecordWriteIndex</i> opcode.
 * Host will respond by sending record data using <i>eEventRecordData</i> command.
 *
 * @param[in]	index	Record index
 */
static void requestRecord( uint32_t index )
{

	_eventRecordWriteIndex_t	buffer;

	buffer.opCode = eEventRecordWriteIndex;
	buffer.index = index;

	IotLogDebug( "requestRecord: %d", index );

	shci_PostResponse( ( uint8_t * )&buffer, sizeof( buffer ) );

}

/**
 * @brief	Fetch Dispense Records from Host
 *
 * In general, if lastFetchedIndex = n, then fetchIndex = (n + 1)
 * Anomaly is when zero records have been fetched, handle by initializing lastFetchedIndex to -1
 *
 * fetch @0, 16-byte, lastFetchedIndex = 0, lastReceivedIndex = 0
 * fetch @1, 32-byte, lastFetchedIndex = 1, lastReceivedIndex = 1
 * fetch @2, 32-byte, lastFetchedIndex = 2, lastReceivedIndex = 3
 * fetch @4, 32-byte, lastFetchedIndex = 4, lastReceivedIndex = 5
 */
static void fetchRecords( void )
{
	IotLogDebug( "fetchRecords: lastRequestIndex = %d, nextRequestIndex = %d, lastReportRecord = %d, lastRecievedRecord = %d",
			_evtrec.lastRequestIndex,
			_evtrec.nvs.nextRequestIndex,
			_evtrec.lastReportedIndex,
			_evtrec.nvs.lastReceivedIndex
			);


	if( _evtrec.nvs.nextRequestIndex < _evtrec.lastReportedIndex )							/* if records are available */
	{
		if( _evtrec.nvs.nextRequestIndex != _evtrec.lastRequestIndex )						/* and request isn't a repeat */
		{
			requestRecord( _evtrec.nvs.nextRequestIndex );									/* request record */
			_evtrec.lastRequestIndex = _evtrec.nvs.nextRequestIndex;
		}
		else if ( ( _evtrec.nvs.nextRequestIndex + 1 ) < _evtrec.lastReportedIndex )		/* If RequestIndex can be incremented, and still be less than reported */
		{
			_evtrec.nvs.nextRequestIndex++;													/* Increment, request record on next pass through */
		}
	}
}

/**
 * @brief	Read one or more FIFO records, format as a single JSON Object.
 *
 *	A buffer is allocated from the heap, and record(s) written to buffer.
 *	Caller must release buffer after processing it.
 *
 * Example format, extra whitespace added for clarity:
 *
 *	 {
 *	 "serialNumber":"99AJ99AM2688",
 *	 "requestType":"Formatted",
 *	 "createdAt":"2020-09-02T17:56:16.664Z",
 *	 "body":
 *		 {
 *		 "logs": [
 *			{
 *			  "Index": 266,
 *			  "DateTime": "2019-02-22T10:35:12",
 *			  "Status": 7,
 *			  "CatalogID": 177,
 *			  "BeverageID": 846,
 *			  "CycleTime": 29.379910000000002,
 *			  "PeakPressure": 3.7464768,
 *			  "FirmwareVersion": 0.0,
 *			  "FreezeEvents": 0
 *			},
 *			{
 *			  "Index": 265,
 *			  "DateTime": "2019-02-22T10:35:12",
 *			  "Status": 6,
 *			  "CatalogID": 176,
 *			  "BeverageID": 847,
 *			  "CycleTime": 29.379910000000002,
 *			  "PeakPressure": 3.7464768,
 *			  "FirmwareVersion": 0.0,
 *			  "FreezeEvents": 0
 *			}
 *		]}
 *	}
 *
 *	@param[in]	n	Number of records to read from FIFO
 *	@return		Pointer to allocated buffer, null-terminated
 */
static char * readRecords( int n )
{
	size_t	bufferSize;
	size_t	recordSize;

	char utc[ 28 ] = { 0 };
	char sernum[13] = { 0 };
	size_t length = sizeof( sernum );
	// Get Serial Number from NV storage
	bleGap_fetchSerialNumber(sernum, &length);

	char *buffer;
	char *record;

	char *header = NULL;
	size_t	headerSize;
	double value;

	/* Get Current Time, UTC */
	getUTC( utc, sizeof( utc ) );

	/* Format header */
	headerSize = mjson_printf( &mjson_print_dynamic_buf, &header,
			"{%Q:%Q, %Q:%Q, %Q:%Q, %Q:{ %Q: [",
			"serialNumber",       sernum,
			"requestType",        "Formatted",
			"createdAt",          utc,
			"body",
			"logs"
			);

	/* Allocate a buffer sufficient to hold n records */
	bufferSize = ( n * MAX_EVENT_RECORD_SIZE ) + headerSize + footerSize;
	buffer = pvPortMalloc( bufferSize );

	/* Copy header into buffer, then free header */
	strcpy( buffer, header );
	free( header );

	// Allocate a buffer for a single record */
	record = pvPortMalloc( MAX_EVENT_RECORD_SIZE );

	IotLogInfo( "readRecords: %d, bufferSize = %d", n, bufferSize );

	/* Read n records into buffer, separate records with ", " */
	while( n-- )
	{
		recordSize = MAX_EVENT_RECORD_SIZE;
		fifo_get( _evtrec.fifoHandle, record, &recordSize );
		IotLogInfo( "get record: %d bytes", recordSize );

		/* parse json record for index value */
		value = 0;
		if( mjson_get_number( record, recordSize, "$.Index", &value ) )
		{
			int32_t index = ( int32_t ) value;
			if( index > _evtrec.highestReadIndex )
			{
				_evtrec.highestReadIndex = index;
				IotLogInfo( "Highest FIFO Read Index = %d", _evtrec.highestReadIndex );
			}
		}
		record[ recordSize ] = '\0';							/* terminated record */
		strcat( buffer, record );								/* Append record */

		if( n )													/* for all but last record */
		{
			strcat( buffer, recordSeparator );					/* Append separator */
		}
	}

	vPortFree( record );

	/* Add footer */
	strcat( buffer, recordFooter );							/* Append Footer */

	return( buffer );
}

/**
 * @brief	Event Record Publish Complete Callback
 *
 * This function is called when the MQTT message publish completes.
 * The param structure contains a results field; value IOT_MQTT_SUCCESS indicates message was successfully published.
 * If message is successfully published and the context reference matches the value used when sending the message,
 * the FIFO read(s) can be committed.
 *
 * @param[in]	reference	Pointer to context reference - a TimeValue is used
 * @param[in]	param		Pointer to callback parameter structure
 */
static void vEventRecordPublishComplete(void * reference, IotMqttCallbackParam_t * param )
{
	time_t *context = reference;

	_evtrec.publishComplete = true;

	if( ( IOT_MQTT_SUCCESS == param->u.operation.result ) && ( *context == _evtrec.contextTime ) )
	{
		IotLogInfo( "EventRecord: Publish Complete success" );
		_evtrec.publishSuccess = true;
	}
	else
	{
		IotLogInfo( "EventRecord: Publish Complete failed: result = %d, context received = %d, expected = %d",
				param->u.operation.result,
				*context,
				_evtrec.contextTime );
	}
}

/**
 * @brief	Event Record Shadow Update Complete Callback
 *
 * This function is called when the Shadow Update completes.
 * The param structure contains a results field; value AWS_IOT_SHADOW_SUCCESS indicates shadow was successfully updated.
 * If shadow is successfully updated and the context reference matches the value used when sending the update request,
 * the LastPublishIndex will be updated on AWS.
 *
 * @param[in]	pItem	Pointer Shadow Item that was updated
 */
static void vEventRecordShadowUpdateComplete( _shadowItem_t *pItem )
{
	IotLogInfo( "EventRecord: Shadow Update success" );
	_evtrec.shadowUpdateComplete = true;
	_evtrec.shadowUpdateSuccess = true;
}

/**
 * @brief	Publish Event Records from FIFO to AWS
 *
 * If there are records in the FIFO, and an MQTT connection has been established:
 * 		- Read records, up to a maximum count
 * 		- Format records as a single JSON
 * 		- Send records to MQTT topic
 * 		- When callback received, on successful posting, update FIFO pointers
 *
 * 	After records have been sent, but before the callback is received, the
 * 	eventRecords task should NOT BE BLOCKED.  Additional pushes can be blocked,
 * 	but fetches should not be affected.
 */
static void publishRecords( const char *topic )
{
	IotMqttCallbackInfo_t publishCallback = IOT_MQTT_CALLBACK_INFO_INITIALIZER;
	AwsIotShadowCallbackInfo_t shadowCallback = AWS_IOT_SHADOW_DOCUMENT_INFO_INITIALIZER;

	char * jsonBuffer = NULL;
	uint16_t	nRecords;

	switch( _evtrec.publishState )
	{
		case ePublishRead:
			if( mqtt_IsConnected() )
			{
				nRecords = fifo_size( _evtrec.fifoHandle );
				if( 0 < nRecords )
				{
					/* Limit number of records per message */
					nRecords = ( MAX_RECORDS_PER_MESSAGE < nRecords ) ? MAX_RECORDS_PER_MESSAGE : nRecords;

					jsonBuffer = readRecords( nRecords );
					if( NULL != jsonBuffer )
					{
//						IotLogDebug( jsonBuffer );				/* Log message will likely be truncated */
//printf( "publishRecords: %s -> %s\n", jsonBuffer, topic);
						/* Set callback function */
						publishCallback.function = vEventRecordPublishComplete;

						/* Clear flags */
						_evtrec.publishComplete = false;
						_evtrec.publishSuccess = false;

						/* Use Time Value as context */
						_evtrec.contextTime =  getTimeValue();
						publishCallback.pCallbackContext = &_evtrec.contextTime;

						mqtt_SendMsgToTopic( topic, strlen( topic ), jsonBuffer, strlen( jsonBuffer ), &publishCallback );

						vPortFree( jsonBuffer );					/* free buffer after if is processed */

						_evtrec.publishState = ePublishWaitComplete;
					}
				}
			}
			break;

		case ePublishWaitComplete:
			/* Wait for MQTT publish to complete */
			if( _evtrec.publishComplete )
			{
				/* If successful, commit FIFO Read(s), and record Last Published Index */
				if( _evtrec.publishSuccess )
				{
					IotLogInfo( "publishRecords success - commit FIFO Read(s)" );
					fifo_commitRead( _evtrec.fifoHandle, true );

					/* Track Last Published Index */
					_evtrec.lastPublishedIndex = _evtrec.highestReadIndex;

					/* Update Last Published Index, NVS will be updated by shadow module */
					IotLogInfo( "Update Last Published Index: %d", _evtrec.lastPublishedIndex );
					shadowUpdates_publishedIndex( _evtrec.lastPublishedIndex, &vEventRecordShadowUpdateComplete );

					/* Clear flags */
					_evtrec.shadowUpdateComplete = false;
					_evtrec.shadowUpdateSuccess = false;

					_evtrec.publishState = ePublishWaitShadowUpdate;
				}
				else
				{
					IotLogError(" Error publishing Event Record(s) - Abort FIFO read" );
					fifo_commitRead( _evtrec.fifoHandle, false );
					_evtrec.publishState = ePublishRead;
				}
			}
			break;

		case ePublishWaitShadowUpdate:
			if( _evtrec.shadowUpdateComplete )
			{
				if( _evtrec.shadowUpdateSuccess )
				{
					IotLogInfo( "Shadow Update: %s = %d success", shadowLastPublishedIndex, _evtrec.lastPublishedIndex );
				}
				else
				{
					IotLogError( "Shadow Update: fail");
				}
				_evtrec.publishState = ePublishRead;
			}
			break;

		default:
			_evtrec.publishState = ePublishRead;
			break;
	}
}

/**
 * @brief	Event Records Task
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
static void _eventRecordsTask(void *arg)
{
	esp_err_t err;
	size_t size;

	vTaskDelay( 10000 / portTICK_PERIOD_MS );

	IotLogInfo( "_eventRecordsTask" );

    while( 1 )
	{
    	/* Only process records if user has opted in to Data Sharing */
    	if( shadowUpdates_getDataShare() )
    	{

			/* fetch Dispense Records from Host, put to FIFO*/
			fetchRecords();

			/* get Event Records from FIFO, push to AWS */
			if( shadowUpdates_getProductionRecordTopic() )
			{
				publishRecords( EventRecordPublishTopicPoduction );		/* Prod */
			}
			else
			{
				publishRecords( EventRecordPublishTopicDevelop );		/* Dev */
			}

			/* Update NVS, if needed */
			if( _evtrec.updateNvs )
			{
				size = sizeof( struct event_record_nvs_s );
				err = NVS_Set( _evtrec.key, &_evtrec.nvs, &size );							// Update NVS
				if( ESP_OK != err )
				{
					IotLogError( "Error update Event Record NVS" );
				}
				else
				{
					IotLogInfo( " update evtrec nvs, last received = %d, last request = %d ", _evtrec.nvs.lastReceivedIndex, _evtrec.nvs.nextRequestIndex );
				}
				_evtrec.updateNvs = false;													// clear flag
			}
    	}
		vTaskDelay( 1000 / portTICK_PERIOD_MS );
    }
}



/* ************************************************************************* */
/* ************************************************************************* */
/* **********        I N T E R F A C E   F U N C T I O N S        ********** */
/* ************************************************************************* */
/* ************************************************************************* */

/**
 * @brief Initialize Event Records submodule - register commands
 */
int32_t eventRecords_init( fifo_handle_t fifo, NVS_Items_t nvsKey )
{
	esp_err_t	err = ESP_OK;
	size_t size;

	_evtrec.fifoHandle = fifo;
	_evtrec.key = nvsKey;

	/*
	 * Restore Event Record access variables from NVS Storage
	 */
	if( ESP_OK == err)
	{
		size = sizeof( struct event_record_nvs_s );
		err = NVS_Get( _evtrec.key, &_evtrec.nvs, &size );
		if( err != ESP_OK )
		{
			_evtrec.nvs.lastReceivedIndex = -1;
			_evtrec.nvs.nextRequestIndex = 0;
			size = sizeof( struct event_record_nvs_s );
			err = NVS_Set( _evtrec.key, &_evtrec.nvs, &size );				// Update NVS
		}
	}

	IotLogInfo( "Initializing Event Records:" );
	IotLogInfo( "  Handle = %p", ( (uint32_t *) _evtrec.fifoHandle ) );
	IotLogInfo( "  lastReportedIndex = %d", _evtrec.lastReportedIndex );
	IotLogInfo( "  lastReceivedIndex = %d", _evtrec.nvs.lastReceivedIndex );
	IotLogInfo( "  nextRequestIndex = %d",  _evtrec.nvs.nextRequestIndex );

	/* Register commands */
	shci_RegisterCommand( eEventRecordData, &vUpdateEventRecordData );

	/* Register callback for Dispense Record Count update */
	bleInterface_registerUpdateCB( eDispRecCountIndex, &vRecordCountUpdate );

	/* Create Task on new thread */
    xTaskCreate( _eventRecordsTask, "event_record", EVENT_RECORD_STACK_SIZE, NULL, EVENT_RECORD_TASK_PRIORITY, &_evtrec.taskHandle );
    if( NULL == _evtrec.taskHandle )
	{
        return ESP_FAIL;
    }
    {
    	IotLogInfo( "eventRecords_task created" );
    }

    return	ESP_OK;
}

/**
 * @brief	Changed Topic event handle
 *
 * This function is to be registered as the callback function for the Changed Topic Event.
 * It will be call when the selected Topic (Dev vs. Prod) changes.
 * The Event FIFO is reset and the Last Recorded Event, for the new Topic, is used
 * to reset the local indexes.
 *
 * @param[in]	lastRecordedEvent	Last event, for newly selected environment (Dev/Prod), that has been published
 */
void eventRecords_onChangedTopic( uint32_t lastRecordedEvent )
{
	IotLogInfo( "eventRecords_onChangedTopic(%u)", lastRecordedEvent );

	/* Clear the FIFO */
	fifo_reset( _evtrec.fifoHandle );

	/*
	 * Set local indexes to last Recorded Event, for new Topic.
	 * Do not change _evtrec.lastReportedIndex - this tracks the last record in the MZ
	 */
	_evtrec.nvs.nextRequestIndex = lastRecordedEvent;
	_evtrec.nvs.lastReceivedIndex = lastRecordedEvent;
	_evtrec.lastRequestIndex = lastRecordedEvent;

}
