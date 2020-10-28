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

#ifdef DEPRECIATED
typedef enum
{
	JSON_NONE,
	JSON_STRING,
	JSON_NUMBER,
	JSON_INTEGER,
	JSON_UINT16,
	JSON_UINT32,
	JSON_BOOL
} json_type_t;

typedef union
{
	char *string;
	double *number;
	int16_t *integer;
	uint16_t *integerU16;
	uint32_t *integerU32;
	bool *truefalse;
} json_value_t;
#endif

typedef void (* _shadowDeltaCallback_t)( void *pItem, const uint8_t *pData, uint16_t size );
typedef void (* updateCompleteCallback_t)( void  *pItem );

typedef	struct {
	_jsonItem_t				jItem;
//	const char *			section;
//	const char *			key;
//	const json_type_t		jType;
//	json_value_t			jValue;
//	_shadowDeltaCallback_t	handler;
	updateCompleteCallback_t handler;			/**< Callback function called upon shadow item update complete */
	bool					bUpdate;			/**< If true, Shadow update is required for item */
	NVS_Items_t 			nvsItem;			/**< Associated Enumerated NVS Item, -1 if none */
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
//int updateReportedShadow(const char * updateJSON,
//							int	sizeJSON,
//							const AwsIotShadowCallbackInfo_t * pCallbackInfo);

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
