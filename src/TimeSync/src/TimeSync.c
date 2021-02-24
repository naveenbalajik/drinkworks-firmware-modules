/**
 *	@file	TimeSync.c
 *
 *	@brief	Synchronize System Time, and RTC,  with SNTP server
 *
 * Created on: September 14, 2020
 * Author: Ian Whitehead
 */

#include	<stdlib.h>
#include	<stdio.h>
#include	<stdint.h>
#include	<stdbool.h>
#include	<string.h>
#include	<time.h>

#ifdef	HOST_RTC
#include "bleInterface.h"
#include "bleInterface_internal.h"
#include "bleFunction.h"
#endif
#include "shci.h"
#include "TimeSync.h"
#include "TimeSync_logging.h"
//#include "nvs_utility.h"
//#include "sntp.h"
#include "../../../../freertos/vendors/espressif/esp-idf/components/lwip/include/apps/sntp/sntp.h"
#include "../../../../freertos/vendors/espressif/esp-idf/components/lwip/lwip/src/include/lwip/apps/sntp.h"

#define	DELTA_TIME_THRESHOLD	5			/**< Time delta threshold, in seconds, to initiate synchronizing host clock */

#define	TIMESYNC_STACK_SIZE    ( 1024 )		/**< Task Stack size, 20 bytes free 2/23/2021 */

#define	TIMESYNC_TASK_PRIORITY	( 2 )

#define	TIMESYNC_TASK_NAME	( "TimeSync" )

/*
 * @ brief	How frequently to check RTC synchronization, in seconds
 */
#define	CHECK_SYNC_TIME		( ( ( (23 * 24) + 59 ) * 60 ) + 55 )
//#define	CHECK_SYNC_TIME		( 60 )

/**
 * @brief	Date/Time structure used by host/SHCI
 */
typedef	struct
{
	uint8_t year;				/**< Year: since 1900 */
	uint8_t month;				/**< Month: 0 - 11 */
	uint8_t mday;				/**< Day of Month: 1 - 31 */
	uint8_t hour;				/**< Hour: 0 - 23 */
	uint8_t minute;				/**< Minute: 0 - 59 */
	uint8_t second;				/**< Second: 0 - 59 */
	uint8_t	wday;				/**< Day of Week: 0 - 6, 0 = Sunday */
} __attribute__ ((packed)) _host_time_t;


/**
 * @brief	Bitwise enumerated RTC Status
 */
typedef enum
{
	eRtc_Unknown =		0x00,			/**< Status unknown */
	eRtc_HalPresent =	0x01,			/**< RTC HAL is present */
	eRtc_Detected =		0x02,			/**< RTC has been detected */
	eRtc_Syncd =		0x04,			/**< System Time is synchronized to SNTP server */
	eRtc_SysTime = 		0x08			/**< System Time, not RTC, returned */
} rtcStatus_t;


/**
 * @brief	Get Time command response packet
 */
typedef struct
{
	_shciOpcode_t		opCode;							/**< Event OpCode */
	_shciOpcode_t		command;						/**< Command OpCode */
	_errorCodeType_t	errorCode;						/**< Error Code */
	_host_time_t		timeDate;						/**< Time/Date */
	uint8_t				status;
}  __attribute__ ((packed)) _timeResponse_t;


/**
 * @brief	TimeSync variables
 */
typedef	struct
{
	TaskHandle_t	taskHandle;							/**< handle for TimeSync Task */
	bool			ntp_sync;							/**< true: System Time synchronized to SNTP server */
	time_t			last_sync;							/**< Time when last synchronized to SNTP server */
	const rtc_hal_t * 	hal;							/**< RTC Hardware Abstraction Layer */
} _time_sync_t;

static _time_sync_t	time_sync =
{
	.ntp_sync = false,
	.last_sync = 0L,
	.hal = NULL,
};


/**
 * @brief	SNTP Time Synchronization Callback
 *
 * This function is called when a time synchronization of the local clock to NTP server occurs.
 *
 * @param[in]	tv	Pointer to timeval structure, containing time value from NTP server
 */
static void sntp_sync_time_cb(struct timeval *tv)
{
	char buf[28];
	struct tm *gm = gmtime( (time_t *)&tv->tv_sec );

	strftime( buf, 28, "%Y-%m-%dT%H:%M:%SZ", gm);
	IotLogInfo( "sntp_sync_time_cb: %s", buf);
	time_sync.ntp_sync = true;
}


/**
 * @brief	Get Time/Date command handler
 *
 * If an RTC HAL is present, use it to read the RTC time/date value.
 * If RTC HAL is not present, or RTC fails, return system time/date.
 * response.status contains flags indicating status of RTC
 *
 * @param[in]	pData	Pointer to command parameters
 * @param[in]	size	Size of command parameter data
 */
static void vGetTime( const uint8_t *pData, const uint16_t size )
{
	_timeResponse_t	response;
	struct timeval tv;
	rtcStatus_t status = eRtc_Unknown;

	IotLogInfo( "vGetTime" );

	/* If HAL getTime function is valid, use to read the RTC */
	if( NULL != time_sync.hal->getTime )
	{
		status |= eRtc_HalPresent;
		tv.tv_sec = time_sync.hal->getTime();
		if( -1 == tv.tv_sec )
		{
			IotLogError( "Error getting RTC time" );
		}
		else
		{
			status |= eRtc_Detected;
		}

	}

	/* No HAL getTime function, or can't access RTC, return system time */
	if( ( status & eRtc_Detected ) != eRtc_Detected )
	{
		/* get System Time */
		if( 0 == gettimeofday( &tv, NULL ) )
		{
			/* Update status, if no errors */
			status |= eRtc_SysTime;
		}
	}

	/* tv has valid timeval, if RTC or System Time was accessed without error */
	if( status & ( eRtc_Detected | eRtc_SysTime ) )
	{
		/* If system Time is synchronized to SNTP server, set status bit */
		if( time_sync.ntp_sync )
		{
			status |= eRtc_Syncd;
		}

		/* break down time value to components */
		struct tm *gm = gmtime( &tv.tv_sec );

		response.opCode		= eCommandComplete;
		response.command	= eTimeGet;
		response.errorCode	= eCommandSucceeded;
		response.timeDate.year		= ( uint8_t )gm->tm_year;
		response.timeDate.month		= ( uint8_t )gm->tm_mon;
		response.timeDate.mday		= ( uint8_t )gm->tm_mday;
		response.timeDate.hour		= ( uint8_t )gm->tm_hour;
		response.timeDate.minute	= ( uint8_t )gm->tm_min;
		response.timeDate.second	= ( uint8_t )gm->tm_sec;
		response.timeDate.wday		= ( uint8_t )gm->tm_wday;		/* POSIX: day of week [0,6] (Sunday = 0) */

		response.status = ( uint8_t )status;						/* Bit Mapped Status */

		IotLogInfo( "vGetTime: %02d:%02d:%2d",  response.timeDate.hour, response.timeDate.minute, response.timeDate.second );

		shci_PostResponse( ( const uint8_t * ) &response, sizeof( response ) );
	}
	else
	{
		IotLogError( "Error accessing time" );
		shci_postCommandComplete( eTimeGet, eUnspecifiedError );		/* error accessing time */
	}
}

/**
 * @brief	Set RTC Time/Date command handler
 *
 * This function handles the SHCI eSetTime command.
 * RTC is updated via the configured HAL.
 *
 * @param[in]	pData	Pointer to command parameters
 * @param[in]	size	Size of command parameter data
 */
static void vSetTime( const uint8_t *pData, const uint16_t size )
{
	_host_time_t * pHostTime = ( _host_time_t * ) pData;
	struct tm time;
	time_t host_time;

	if( size == sizeof( _host_time_t ) )
	{
		/* If HAL setTime function is valid, use to update the RTC */
		if( NULL != time_sync.hal->setTime )
		{
			/* convert from host_time_t to tm */
			time.tm_year = pHostTime->year;
			time.tm_mon = pHostTime->month;
			time.tm_mday = pHostTime->mday;
			time.tm_hour = pHostTime->hour;
			time.tm_min = pHostTime->minute;
			time.tm_sec = pHostTime->second;
			time.tm_isdst = 0;

			/* convert broken-down time into time since the Epoch */
			host_time = mktime( &time );
			if( -1 == host_time )
			{
				IotLogError( "Error converting host time" );
				shci_postCommandComplete( eTimeSet, eInvalidCommandParameters );		/* error invalid parameter */
			}
			else
			{
				IotLogError( "Set Time" );
				time_sync.hal->setTime( ( time_t )host_time );
				shci_postCommandComplete( eTimeSet, eCommandSucceeded );				/* success setting time */
			}
		}
		else
		{
			IotLogError( "Error RTC not supported" );
			shci_postCommandComplete( eTimeSet, eRtcNotSupported );						/* error RTC not supported */
		}
	}
	else
	{
		IotLogError( "Error invalid parameter" );
		shci_postCommandComplete( eTimeSet, eInvalidCommandParameters );				/* error invalid parameter */
	}
}

/**
 * @brief	Time Sync Task
 *
 *	Synchronize the Real Time Clock, via the HAL, with system time.
 *	RTC is only synchronized with System Time, if System Time has been
 *	synchronized with SNTP server
 */
static void _TimeSyncTask( void *arg )
{

	int err = 0;
	struct timeval tv;

	time_t rtc_time;
	time_t	delta_time;

	for(;;)
	{

		/* Only continue if local clock has been synchronized to NTP server */
		if( time_sync.ntp_sync )
		{
			err = gettimeofday( &tv, NULL);
			if( 0 == err )
			{
				if( NULL != time_sync.hal )
				{
					if( NULL != time_sync.hal->getTime )
					{
						rtc_time = time_sync.hal->getTime();
						if( -1 == rtc_time )
						{
							IotLogError( "Error getting RTC time" );
						}
						else
						{
							/* compute positive time delta */
							if( rtc_time > tv.tv_sec )
							{
								delta_time = rtc_time - tv.tv_sec;
							}
							else
							{
								delta_time = tv.tv_sec - rtc_time;
							}

							IotLogInfo( " RTC time = %d, ntp = %d, delta = %d", rtc_time, tv.tv_sec, delta_time );

							/* If time delta exceeds threshold, update RTC */
							if( DELTA_TIME_THRESHOLD < delta_time )
							{
								IotLogInfo( "Updating RTC" );

								/* If HAL setTime function is valid, use to update the RTC */
								if( NULL != time_sync.hal->setTime )
								{
									time_sync.hal->setTime( ( time_t )tv.tv_sec );
								}
							}
						}
					}
				}
			}
			/* Delay for Sync Check interval */
			vTaskDelay( CHECK_SYNC_TIME * 1000 / portTICK_PERIOD_MS );
		}
		else
		{
			/* wait for NTP server synchronization */
			vTaskDelay( 1000 / portTICK_PERIOD_MS );
		}
	}
}


/* ************************************************************************* */
/* ************************************************************************* */
/* **********        I N T E R F A C E   F U N C T I O N S        ********** */
/* ************************************************************************* */
/* ************************************************************************* */

/**
 * @brief	Get Time value, seconds since Epoc 1/1/1970 00:00:00
 *
 * @return	Time value, seconds since Epoc 1/1/1970 00:00:00
 */
time_t getTimeValue( void )
{
	int err;
	struct timeval tv;

	err = gettimeofday( &tv, NULL);
	if( 0 == err )
	{
		return tv.tv_sec;
	}
	return -1;
}


/**
 * @brief	Get Current UTC Time, formatted per ISO 8601
 * @return 0 on success
 */
int getUTC( char * buf, size_t size)
{
	int err;
	struct timeval tv;

	err = gettimeofday( &tv, NULL);
	if( 0 == err )
	{
		struct tm *gm = gmtime( &tv.tv_sec );

		strftime( buf, size, "%Y-%m-%dT%H:%M:%SZ", gm);
		IotLogInfo( "getUTC: %s", buf);
	}
	return err;
}


/**
 * @brief	Initialize Time Synchronization
 *
 * @param[in]	pRtcHAL	Pointer to RTC Abstraction Layer functions
 */
void TimeSync_init( const rtc_hal_t * pRtcHAL )
{
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, "pool.ntp.org");
	sntp_init();
	sntp_set_time_sync_notification_cb( &sntp_sync_time_cb);

	/* Save pointer to RTC Abstraction */
	time_sync.hal = pRtcHAL;

	/* Initialize RTC PAL, if function is present */
	if( NULL != time_sync.hal )
	{
		if( NULL != time_sync.hal->init )
		{
			time_sync.hal->init();
		}
	}

	/* Register SHCI Get/Set Time functions */
	shci_RegisterCommand(eTimeGet, &vGetTime );
	shci_RegisterCommand(eTimeSet, &vSetTime );


	/* Create Task */
	IotLogDebug( "Create TimeSync task" );
    xTaskCreate( _TimeSyncTask, TIMESYNC_TASK_NAME, TIMESYNC_STACK_SIZE, NULL, TIMESYNC_TASK_PRIORITY, &time_sync.taskHandle );
    if( NULL == time_sync.taskHandle )
	{
       	IotLogError( "Error creating: %s task", TIMESYNC_TASK_NAME );
    }
    else
    {
    	IotLogInfo( "%s created", TIMESYNC_TASK_NAME );
    }

}
