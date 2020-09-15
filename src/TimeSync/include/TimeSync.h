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
void TimeSync_init( void );

#endif		/* TIME_SYNC_H */
