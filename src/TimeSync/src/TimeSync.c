/**
 * @file	TimeSync.c
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

#include "bleInterface.h"
#include "bleInterface_internal.h"
#include "bleFunction.h"
#include "TimeSync_logging.h"
#include "nvs_utility.h"
//#include "sntp.h"
#include "../../../../freertos/vendors/espressif/esp-idf/components/lwip/include/apps/sntp/sntp.h"
#include "../../../../freertos/vendors/espressif/esp-idf/components/lwip/lwip/src/include/lwip/apps/sntp.h"

#define	DELTA_TIME_THRESHOLD	5			/**< Time delta threshold, in seconds, to initiate synchronizing host clock */


/*
 * @ brief	How frequently to check RTC synchronization, in seconds
 */
#define	CHECK_SYNC_TIME		( ( ( (23 * 24) + 59 ) * 60 ) + 55 )
//#define	CHECK_SYNC_TIME		( 60 )

typedef	struct
{
	bool	ntp_sync;
	time_t	last_sync;
} _time_sync_t;

static _time_sync_t	time_sync =
{
		.ntp_sync = false,
		.last_sync = 0L,
};

/**
 * @brief	Date/Time structure used by host/SHCI
 */
typedef	struct
{
	uint16_t year;				/**< Year: e.g. 2020 */
	uint8_t month;				/**< Month: 1 - 12 */
	uint8_t date;				/**< Day of Month: 1 - 31 */
	uint8_t hour;				/**< Hour: 0 - 23 */
	uint8_t minute;				/**< Minute: 0 - 59 */
	uint8_t second;				/**< Second: 0 - 59 */
	uint8_t	weekday;			/**< Day of Week: 1 - 7, 1 = Monday */
	uint8_t	fraction;			/**< Faction of second */
	uint8_t	reason;				/**< Reason for update */
} __attribute__ ((packed)) _host_time_t;

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
 * @brief	Current Time update handler
 *
 * This handler will be called whenever the Current Time BLE characteristic value
 * is updated.
 *
 * @param[in]	pData	Pointer to characteristic value
 * @param[in]	size	Size, in bytes, of characteristic value
 */
static void vCurrentTimeUpdate( const void *pData, const uint16_t size )
{
	int err = 0;
	struct timeval tv;

	const _host_time_t *htime = pData;
	_host_time_t	updateTime;
	struct tm time;
	time_t host_time;
	time_t	delta_time;

	/* Only continue if local clock has been synchronized to NTP server */
	if( time_sync.ntp_sync )
	{
		err = gettimeofday( &tv, NULL);
		if( 0 == err )
		{
			/* periodically check host RTC with local clock */
			if( ( 0L == time_sync.last_sync) || ( CHECK_SYNC_TIME < ( tv.tv_sec - time_sync.last_sync ) ) )
			{
				time_sync.last_sync = tv.tv_sec;					// update time last sync'd

				/* convert from host_time_t to tm */
				time.tm_year = htime->year - 1900;
				time.tm_mon = htime->month - 1;
				time.tm_mday = htime->date;
				time.tm_hour = htime->hour;
				time.tm_min = htime->minute;
				time.tm_sec = htime->second;
				time.tm_isdst = 0;

				/* convert broken-down time into time since the Epoch */
				host_time = mktime( &time );
				if( -1 == host_time )
				{
					IotLogError( "Error converting host time" );
				}
				else
				{
					/* compute positive time delta */
					if( host_time > tv.tv_sec )
					{
						delta_time = host_time - tv.tv_sec;
					}
					else
					{
						delta_time = tv.tv_sec - host_time;
					}

					IotLogInfo( " host_time = %d, ntp = %d, delta = %d", host_time, tv.tv_sec, delta_time );

					/* If time delta exceeds threshold, update host RTC */
					if( DELTA_TIME_THRESHOLD < delta_time )
					{
						IotLogInfo( "TODO: Update Host RTC" );
						struct tm *gm = gmtime( (time_t *)&tv.tv_sec );

						updateTime.year = gm->tm_year + 1900;
						updateTime.month = gm->tm_mon + 1;
						updateTime.date = gm->tm_mday;
						updateTime.hour = gm->tm_hour;
						updateTime.minute = gm->tm_min;
						updateTime.second = gm->tm_sec;
						updateTime.weekday = ( gm->tm_wday ? gm->tm_wday : 7 );		/* POSIX: day of week [0,6] (Sunday = 0) -> BLE [1,7] 1 = Monday */
						updateTime.fraction = 0;
						updateTime.reason = 0;

						bleFunction_writeCharacteristic( CURRENT_TIME_HANDLE, (uint8_t *)&updateTime, sizeof( _host_time_t ) );
					}
				}
			}
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
 */
void TimeSync_init( void )
{
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, "pool.ntp.org");
	sntp_init();
	sntp_set_time_sync_notification_cb( &sntp_sync_time_cb);

	/* Register callback for Current Time update */
	bleInterface_registerUpdateCB( eCurrentTimeIndex, &vCurrentTimeUpdate );
}
