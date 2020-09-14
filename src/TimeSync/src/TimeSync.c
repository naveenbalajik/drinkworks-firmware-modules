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


//#include "iot_config.h"

/* FreeRTOS includes.. */

//#include "FreeRTOS.h"
//#include "task.h"

//#include "application.h"

/* AWS System includes. */
//#include "iot_system_init.h"
//#include "iot_logging_task.h"

#if !AFR_ESP_LWIP
//#include "FreeRTOS_IP.h"
//#include "FreeRTOS_Sockets.h"
#endif

//#include "driver/uart.h"
//#include "tcpip_adapter.h"
//#include "shci.h"
#include "bleInterface.h"
//#include "bleFunction.h"
//#include "espFunction.h"
//#include "wifiFunction.h"
#include "app_logging.h"
#include "nvs_utility.h"
//#include "event_fifo.h"
//#include "event_records.h"
#include "sntp.h"
//#include "../freertos/vendors/espressif/esp-idf/components/lwip/lwip/src/include/lwip/apps/sntp.h"

#define	DELTA_TIME_THRESHOLD	5			/**< Time delta threshold, in seconds, to initiate synchronizing host clock */

/*
 * @ brief	How frequently to check RTC synchronization, in seconds
 */
#define	CHECK_SYNC_TIME		( ( ( (23 * 24) + 59 ) * 60 ) + 55 )

#ifdef	WORKING

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
	uint16_t year;
	uint8_t month;
	uint8_t date;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint8_t	weekday;
} __attribute__ ((packed)) _host_time_t;

static void sntp_sync_time_cb(struct timeval *tv)
{
	char buf[28];
	struct tm *gm = gmtime( &tv->tv_sec );

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
	int err;
	struct timeval tv;

	_host_time_t *htime = pData;
	struct tm time;
	time_t host_time;
	time_t	delta_time;

	/* Only continue if local clock has been synchronized to NTP server */
	if( time_sync.ntp_sync )
	{
		err = gettimeofday( &tv, NULL);
		if( 0 == err )
		{
			IotLogInfo( "ntp time = %d", tv.tv_sec );

			/* periodically check host RTC with local clock */
			if( ( 0L == time_sync.last_sync) || ( CHECK_SYNC_TIME < ( tv.tv_sec - time_sync.last_sync ) ) )
			{
				time_sync.last_sync = tv.tv_sec;					// update time last sync'd

				/* convert from host_time_t to tm */
				time.tm_year = htime->year - 1900;
				time.tm_mon = htime->month;
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
					IotLogInfo( " host_time = %d", host_time );

					IotLogInfo( " host_time = %d, ntp = %d", host_time, tv.tv_sec );

					/* compute positive time delta */
					if( host_time > tv.tv_sec )
					{
						delta_time = host_time - tv.tv_sec;
					}
					else
					{
						delta_time = host_time - tv.tv_sec;
					}

					/* If time delta exceeds threshold, update host RTC */
					if( DELTA_TIME_THRESHOLD < delta_time )
					{
						IotLogInfo( "TODO: Update Host RTC" );
					}
				}
			}
		}
	}
}

#endif

/* ************************************************************************* */
/* ************************************************************************* */
/* **********        I N T E R F A C E   F U N C T I O N S        ********** */
/* ************************************************************************* */
/* ************************************************************************* */

/**
 * @brief	Get Current UTC Time, formatted per ISO 8601
 * @return 0 on success
 */
int getUTC( char * buf, size_t size)
{
#ifdef	WORKING
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
#else
	return -1;
#endif
}

/**
 * @brief	Initialize Time Synchronization
 */
void TimeSync_init( void )
{
#ifdef	WORKING
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, "pool.ntp.org");
	sntp_init();
	sntp_set_time_sync_notification_cb( &sntp_sync_time_cb);

	/* Register callback for Current Time update */
	bleInterface_registerUpdateCB( eCurrentTimeIndex, &vCurrentTimeUpdate );
#endif
}
