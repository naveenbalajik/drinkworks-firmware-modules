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
 * @brief	Bitwise enumerated RTC Status
 */
typedef enum
{
	eRtc_Unknown =		0x00,			/**< Status unknown */
	eRtc_HalPresent =	0x01,			/**< RTC HAL is present */
	eRtc_Detected =		0x02,			/**< RTC has been detected */
	eRtc_BatEnable =	0x04,			/**< RTC battery is enabled (i.e. Battery present) */
	eRtc_Syncd =		0x08,			/**< System Time is synchronized to SNTP server */
	eRtc_SysTime = 		0x10			/**< System Time, not RTC, returned */
} rtcStatus_t;

typedef	uint8_t	_rtcStatus_t;

/**
 * @brief	Abstract the RTC interface
 *
 * Four functions:
 *  - Init: Initialize the Real Time Clock
 *  - getStatus: Get RTC Status
 *  - getTime: Get current RTC value
 *  - setTime: Set RTC value
 */
typedef void ( * _rtc_init_t )( void );
typedef _rtcStatus_t ( * _rtc_getStatus_t )( void );
typedef time_t ( * _rtc_getTime_t )( void );
typedef void ( * _rtc_setTime_t )( time_t time );

typedef struct
{
	_rtc_init_t			init;
	_rtc_getStatus_t	getStatus;
	_rtc_getTime_t		getTime;
	_rtc_setTime_t		setTime;
} rtc_hal_t;

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
void TimeSync_init( const rtc_hal_t * pRtcHAL );

#endif		/* TIME_SYNC_H */
