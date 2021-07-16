/**
 * @file reset_support.c
 */

#include	"esp_system.h"
#include	"reset_support.h"
#include	"reset_logging.h"
#include	"TimeSync.h"

const const char * resetReasonText[] =
{
    [ ESP_RST_UNKNOWN ]		= "Unknown",    			//!< Reset reason can not be determined
    [ ESP_RST_POWERON ]		= "Power-on",    			//!< Reset due to power-on event
	[ ESP_RST_EXT ]			= "External pin",        	//!< Reset by external pin (not applicable for ESP32)
	[ ESP_RST_SW ]			= "Software",         		//!< Software reset via esp_restart
	[ ESP_RST_PANIC ]		= "Panic",      			//!< Software reset due to exception/panic
	[ ESP_RST_INT_WDT ]		= "Interrupt Watchdog",   	//!< Reset (software or hardware) due to interrupt watchdog
	[ ESP_RST_TASK_WDT ]	= "Task Watchdog",   		//!< Reset due to task watchdog
	[ ESP_RST_WDT ]			= "Watchdog",        		//!< Reset due to other watchdogs
    [ ESP_RST_DEEPSLEEP ]	= "Deep sleep",  			//!< Reset after exiting deep sleep mode
	[ ESP_RST_BROWNOUT ]	= "Brownout",   			//!< Brownout reset (software or hardware)
	[ ESP_RST_SDIO ]		= "SDIO"       				//!< Reset over SDIO
};

#define	NUM_RESET_REASONS ( ESP_RST_SDIO + 1)


_resetCallback_t _resetCallbackTable[ NUM_RESET_REASONS ] = { NULL };

/**
 * @brief	Process a System Reset
 *
 * This function should be called after the system clock has been sync'd
 * for valid date/time stamping.
 *
 * Also, some delay will filter out power-on "bounce"
 */
void reset_ProcessReason( void )
{
	esp_reset_reason_t reason;
	char utc[ 28 ] = { 0 };

	/* Get Current Time, UTC */
	getUTC( utc, sizeof( utc ) );

	/* Get Reset Reason */
	reason = esp_reset_reason();

	if( reason >= NUM_RESET_REASONS )							// ensure reason is a valid index into message table
	{
		reason = ESP_RST_UNKNOWN;
	}

	/* Log reason with date/time stamp */
	IotLogInfo( "Reset: %s @ %s", resetReasonText[ reason ], utc );

	/* Process any reasons that warrant special attention */
	if( _resetCallbackTable[ reason ] != NULL )
	{
		_resetCallbackTable[ reason ]();
	}
}

/**
 * @brief	Register a Reset Callback
 *
 * Callback function will be called when a Reset with the specified Reason occurs
 *
 * @param[in] reason  	Reset Reason
 * @param[in] handler	Callback function
 */
void reset_RegisterCallback( const esp_reset_reason_t reason, const _resetCallback_t handler )
{
	if( reason < NUM_RESET_REASONS )
	{
		_resetCallbackTable[ reason ] = handler;
	}
}
