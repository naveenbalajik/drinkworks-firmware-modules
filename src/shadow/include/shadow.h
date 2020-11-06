/*
 * shadow.h
 *
 *  Created on: Sep 9, 2020
 *      Author: Nicholas.Weber
 */

#ifndef MODULES_SRC_SHADOW_INCLUDE_SHADOW_H_
#define MODULES_SRC_SHADOW_INCLUDE_SHADOW_H_

#include "aws_iot_shadow.h"
#include "nvs_utility.h"
#include	"json.h"


typedef void (* _shadowDeltaCallback_t)( void *pItem, const uint8_t *pData, uint16_t size );
typedef void (* updateCompleteCallback_t)( void  *pItem );

typedef	struct {
	_jsonItem_t				jItem;
	updateCompleteCallback_t handler;			/**< Callback function called upon shadow item update complete */
	bool					bUpdate;			/**< If true, Shadow update is required for item */
	NVS_Items_t 			nvsItem;			/**< Associated Enumerated NVS Item, -1 if none */
} _shadowItem_t;




/**
 * @brief Initialize the Shadow library.
 *
 * @return `EXIT_SUCCESS` if all libraries were successfully initialized;
 * `EXIT_FAILURE` otherwise.
 */
int shadow_init(void);


void shadow_initItemList( _shadowItem_t *pShadowDeltaList );

int shadow_connect( IotMqttConnection_t mqttConnection, const char * pThingName );

void shadow_disconnect( void );

void shadow_updateReported( void );

#endif /* MODULES_SRC_SHADOW_INCLUDE_SHADOW_H_ */
