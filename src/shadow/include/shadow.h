/*
 * shadow.h
 *
 *  Created on: Sep 9, 2020
 *      Author: Nicholas.Weber
 */

#ifndef MODULES_SRC_SHADOW_INCLUDE_SHADOW_H_
#define MODULES_SRC_SHADOW_INCLUDE_SHADOW_H_

#include "aws_iot_shadow.h"

typedef enum
{
	JSON_NONE,
	JSON_STRING,
	JSON_NUMBER,
	JSON_INTEGER,
	JSON_BOOL
} json_type_t;

typedef union
{
	char *string;
	double *number;
	int16_t *integer;
	bool *truefalse;
} json_value_t;

typedef void (* _shadowDeltaCallback_t)( void *pItem, const uint8_t *pData, uint16_t size );

typedef	struct {
	const char *			section;
	const char *			key;
	const json_type_t		jType;
	json_value_t			jValue;
	_shadowDeltaCallback_t	handler;
	bool					bUpdate;			/**< If true, Shadow update is required for item */
} _shadowItem_t;



/**
 * @brief Update the "reported" portion of the shadow
 * Input should be of the format: "{"Characteristic":"Value", "Characteristic2":"Value2"}"
 * The "state" and "reported" portions of the shadow update are provided by the function. They should not
 * be passed to the function as part of the update JSON
 *
 * @param[in] updateJSON		The shadow update in JSON format. Should not include the "reported" portion
 * @param[in] sizeJSON			Size of JSON string
 * @param[in] pCallbackInfo		Callback info after shadow update ACK'd by AWS
 *
 * @return `EXIT_SUCCESS` if all Shadow updates were sent; `EXIT_FAILURE`
 * otherwise.
 */
int updateReportedShadow(const char * updateJSON,
							int	sizeJSON,
							const AwsIotShadowCallbackInfo_t * pCallbackInfo);

/**
 * @brief Initialize the Shadow library.
 *
 * @return `EXIT_SUCCESS` if all libraries were successfully initialized;
 * `EXIT_FAILURE` otherwise.
 */
int shadow_init(void);


void shadow_InitDeltaCallbacks( _shadowItem_t *pShadowDeltaList );

int shadow_connect( IotMqttConnection_t mqttConnection, const char * pThingName );


#endif /* MODULES_SRC_SHADOW_INCLUDE_SHADOW_H_ */
