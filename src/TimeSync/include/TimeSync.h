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
 * @brief	Get Current UTC Time, formatted per ISO 8601
 * @return 0 on success
 */
int getUTC( char * buf, size_t size);

/**
 * @brief	Initialize Time Synchronization
 */
void TimeSync_init( void );

#endif		/* TIME_SYNC_H */
