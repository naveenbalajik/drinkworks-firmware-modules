/**
 * @file	TimeSync.c
 *
 * Created on: September 14, 2020
 * Author: Ian Whitehead
 */

#ifndef	TIME_SYNC_H
#define	TIME_SYNC_H

#include	<time.h>

/**
 * @brief	Abstract the RTC interface
 *
 * Three functions:
 *  - Init: Initiailize the Real Time Clock
 *  - getTime: Get current RTC value
 *  - setTime: Set RTC value
 */
typedef void ( * _rtc_init_t )( void );
typedef time_t ( * _rtc_getTime_t )( void );
typedef void ( * _rtc_setTime_t )( time_t time );

typedef struct
{
	_rtc_init_t		init;
	_rtc_getTime_t	getTime;
	_rtc_setTime_t	setTime;
} rtc_pal_t;

/**
 * @brief	Get Time value, seconds since Epoc 1/1/1970 00:00:00
 *
 * @return	Time value, seconds since Epoc 1/1/1970 00:00:00
 */
time_t getTimeValue( void );

/**
 * @brief	Get Current UTC Time, formatted per ISO 8601
 * @return 0 on success
 */
int getUTC( char * buf, size_t size);

/**
 * @brief	Initialize Time Synchronization
 */
void TimeSync_init( const rtc_pal_t * pRtcPAL );

#endif		/* TIME_SYNC_H */
