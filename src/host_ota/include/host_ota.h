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

/**
 * @brief Callback called for Host OTA Update Notification
 */
typedef void (* _hostOtaNotifyCallback_t)( char *pJson );

int32_t hostOta_init( _hostOtaNotifyCallback_t notifyCb );

IotSemaphore_t *hostOta_getSemaphore( void );

OTA_PAL_ImageState_t hostOta_getImageState( void );

void hostOta_setImageState( OTA_ImageState_t eState );

bool hostOta_pendUpdate( void );

#endif		/*	HOST_OTA_H	*/
