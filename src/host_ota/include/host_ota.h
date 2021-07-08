/**
 * @file	host_ota.h
 *
 * Created on: September 17, 2020
 * Author: Ian Whitehead
 */

#ifndef	HOST_OTA_H
#define	HOST_OTA_H

#include	<stdint.h>
#include	"aws_iot_ota_agent.h"
#include	"ota_update.h"

/**
 * @brief	Host OTA Status enumerated type
 */
typedef enum
{
	eChecking,				/**< Checking if a PIC Update is available */
	eImageDownloading,		/**< A PIC Image is being downloaded */
	eImageAvailable,		/**< A PIC Update Image is available (Image transfer will start momentarily) */
	eNoImageAvailable,		/**< No PIC Image is available */
	eUnknown,				/**< Unknown status - default */
} hostOta_status_t;

/**
 * @brief Host OTA Queue element type
 */
typedef	struct
{
	hostOta_status_t		message;
} hostota_QueueItem_t;

///**
// * @brief Type definition of function to call to get Host Ota Update module's status
// */
//typedef bool (* hostOtaPendUpdateCallback_t)( void );
//
///**
// * @brief	All Interface items for Host Ota module
// */
//typedef struct
//{
//	const AltProcessor_Functions_t * pal_functions;
//	IotSemaphore_t * pSemaphore;
//	hostOtaPendUpdateCallback_t function;
//	QueueHandle_t	queue;
//} hostOta_Interface_t;

/**
 * @brief Callback called for Host OTA Update Notification
 */
typedef void (* _hostOtaNotifyCallback_t)( char *pJson );

int32_t hostOta_init( _hostOtaNotifyCallback_t notifyCb );

IotSemaphore_t *hostOta_getSemaphore( void );

OTA_PAL_ImageState_t hostOta_getImageState( void );

OTA_Err_t hostOta_setImageState( OTA_ImageState_t eState );

bool hostOta_pendUpdate( void );

const AltProcessor_Functions_t * hostOta_getFunctionTable( void );

hostOta_Interface_t * hostOta_getInterface( void );

#endif		/*	HOST_OTA_H	*/
