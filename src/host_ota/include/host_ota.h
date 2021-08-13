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
 *
 * This type is used in the queue for communicating from Ota_Update module to Host_Ota module.
 */
typedef enum
{
	eChecking,														/**< Checking if a PIC Update is available */
	eImageDownloading,												/**< A PIC Image is being downloaded */
	eDownloadComplete,												/**< PIC Download complete (contents not validated) */
	eImageAvailable,												/**< A PIC Update Image is available (Image transfer will start momentarily) */
	eNoImageAvailable,												/**< No PIC Image is available */
	eUnknown,														/**< Unknown status - default */
} hostOta_status_t;

/**
 * @brief Host OTA Queue element type
 */
typedef	struct
{
	hostOta_status_t		message;
} hostota_QueueItem_t;


/**
 * @brief	Host Image Available/Unavailable type
 *
 * This type is used to qualify the SHCI eHostOtaUpdateAvailable event
 */
enum
{
	eHostImage_Available 		= 0x5979,							/**< Host Image is available */
	eHostImage_Unavailable		= 0x4e6e							/**< No Host Image is available */
};

typedef	uint16_t _available_t;

/**
 * @brief	SHCI Host Ota Update Available Event data structure
 */
typedef struct
{
	uint8_t			opcode;											/**< Opcode: eHostOtaUpdateAvailable */
	_available_t	parameter;										/**< Parameter: available/unavailable */
} __attribute__((packed)) _updateAvailable_t;


/**
 * @brief Callback called for Host OTA Update Notification
 */
typedef void (* _hostOtaNotifyCallback_t)( char *pJson );

int32_t hostOta_init( _hostOtaNotifyCallback_t notifyCb );

OTA_PAL_ImageState_t hostOta_getImageState( void );

OTA_Err_t hostOta_setImageState( OTA_ImageState_t eState );

bool hostOta_pendUpdate( void );

const AltProcessor_Functions_t * hostOta_getFunctionTable( void );

hostOta_Interface_t * hostOta_getInterface( void );

#endif		/*	HOST_OTA_H	*/
