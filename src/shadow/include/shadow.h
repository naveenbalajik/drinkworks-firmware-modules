/*
 * shadow.h
 *
 *  Created on: Sep 9, 2020
 *      Author: Nicholas.Weber
 */

#ifndef MODULES_SRC_SHADOW_INCLUDE_SHADOW_H_
#define MODULES_SRC_SHADOW_INCLUDE_SHADOW_H_

#include "aws_iot_shadow.h"

/**
 * @brief Update the "reported" portion of the shadow
 * Input should be of the format: "{"Characteristic":"Value", "Characteristic2":"Value2"}"
 * The "state" and "reported" portions of the shadow update are provided by the function. They should not
 * be passed to the function as part of the update JSON
 *
 * @param[in] mqttConnection 	The MQTT connection used for Shadows.
 * @param[in] updateJSON		The shadow update in JSON format. Should not include the "reported" portion
 * @param[in] sizeJSON			Size of JSON string
 * @param[in] pCallbackInfo		Callback info after shadow update ACK'd by AWS
 *
 * @return `EXIT_SUCCESS` if all Shadow updates were sent; `EXIT_FAILURE`
 * otherwise.
 */
int updateReportedShadow(IotMqttConnection_t mqttConnection,
        				const char * updateJSON,
						int	sizeJSON,
						const AwsIotShadowCallbackInfo_t * pCallbackInfo);

/**
 * @brief Initialize the Shadow library.
 *
 * @return `EXIT_SUCCESS` if all libraries were successfully initialized;
 * `EXIT_FAILURE` otherwise.
 */
int shadow_init(void);

#endif /* MODULES_SRC_SHADOW_INCLUDE_SHADOW_H_ */
