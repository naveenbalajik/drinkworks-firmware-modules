/**
 * @file	host_ota.h
 *
 * Created on: September 17, 2020
 * Author: Ian Whitehead
 */

#ifndef	HOST_OTA_H
#define	HOST_OTA_H

#include	<stdint.h>
#include "aws_iot_ota_agent.h"


int32_t hostOta_init( void );

IotSemaphore_t *hostOta_getSemaphore( void );

OTA_PAL_ImageState_t hostOta_getImageState( void );

void hostOta_setImageState( OTA_ImageState_t eState );

#endif		/*	HOST_OTA_H	*/
