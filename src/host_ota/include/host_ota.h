/**
 * @file	host_ota.h
 *
 * Created on: September 17, 2020
 * Author: Ian Whitehead
 */

#ifndef	HOST_OTA_H
#define	HOST_OTA_H

#include	<stdint.h>

int32_t hostOta_init( void );

IotSemaphore_t *hostOta_getSemaphore( void );

#endif		/*	HOST_OTA_H	*/
