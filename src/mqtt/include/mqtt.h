/*
 * mqtt.h
 *
 *  Created on: Jun 2, 2020
 *      Author: ian.whitehead
 */

#ifndef _MQTT_H_
#define _MQTT_H_

#include "esp_err.h"

/* MQTT include. */
#include "iot_mqtt.h"

/* IoT Network */
#include "platform/iot_network.h"


/**
 * @brief Callback called when a connection to the MQTT library is made
 */
typedef void (* _mqttConnectedCallback_t)(const void * pParams);

/**
 * @brief	Check if MQTT is connected to AWS
 *
 * @return true if actively connected to AWS
 */
bool	mqtt_IsConnected(void);


/**
 * @brief	Disconnect active MQTT connection
 */
void mqtt_disconnectMqttConnection(void);

/**
 * @brief 	Get the handle of the current MQTT connection
 *
 * @param[out]    mqttConnection  Set to the handle of the current MQTT connection
 *
 * @return `ESP_OK` if there is a current connection ESP_FAIL otherwise
 */
esp_err_t	mqtt_GetMqtt(IotMqttConnection_t * mqttConnection);

/**
 * @brief	Send an MQTT message to a designated topic. QoS for the message will be 1. A provided callback will be returned upon completion of the message
 *
 * @param[in]    topic      	Topic name where the message will be sent
 * @param[in]    topicLen  		Topic name length.
 * @param[in]    msgBuf       	Message to send to the buffer
 * @param[in]    msgLen        	Message length
 * @param[out]   pCallbackInfo 	Callback information for when the function completes
 *
 * @return `ESP_OK` if publish message queued; `ESP_FAIL` if function fails before queuing of a publish operation
 * otherwise.
 */
esp_err_t	mqtt_SendMsgToTopic(const char* topic, uint32_t topicLen, const char* msgBuf, uint32_t msgLen, const IotMqttCallbackInfo_t * pCallbackInfo);

/**
 * @brief	Establish an MQTT Connection, using Client Identifier and setting Last Will and Testament message
 *
 * @param[in]    pNetworkServerInfo      Passed to the MQTT connect function when establishing the MQTT connection.
 * @param[in]    pNetworkCredentialInfo  Passed to the MQTT connect function when establishing the MQTT connection.
 * @param[in]    pNetworkInterface       Network interface to use for the connection.
 * @param[in]    pIdentifier             Client Identifier (ThingName)
 * @param[out]   pMqttConnection         Set to the handle to the new MQTT connection.
 *
 * @return `EXIT_SUCCESS` if the connection is successfully established; `EXIT_FAILURE`
 * otherwise.
 */
esp_err_t mqtt_establishMqttConnection(		void * pNetworkServerInfo,
        									void * pNetworkCredentialInfo,
											const IotNetworkInterface_t * pNetworkInterface,
											const char * pIdentifier,
											IotMqttConnection_t * pMqttConnection );

/**
 * @brief	Deinitialization function for the MQTT library. Frees resources associated with the mqtt library
 */
void	mqtt_Cleanup(void);

/**
 * @brief	Initialize the MQTT library and set a callback that triggers on a connection with the MQTT broker
 *
 * @param[in]   callback	Callback function forwhen an MQTT connection is successful
 * @param[in]	pParams		Parameters to pass to the callback function
 *
 * @return `EXIT_SUCCESS` if the mqtt library is successfully initialized; `EXIT_FAILURE`
 * otherwise.
 */
esp_err_t	mqtt_Init(_mqttConnectedCallback_t callback, const void * pParams);

#endif /* _MQTT_H_ */
