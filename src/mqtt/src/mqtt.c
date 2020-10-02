/**
 * @file	mqtt.c
 */

/* Standard includes. */
#include <stdio.h>
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"

/* PKCS#11 includes. */
#include "iot_pkcs11_config.h"
#include "iot_pkcs11.h"
#include "iot_pkcs11_pal.h"


/* Flash include*/
#include "nvs_flash.h"
#include "nvs_utility.h"


/* MQTT include. */
#include "iot_mqtt.h"

/* IoT Network */
#include "platform/iot_network.h"

/* Debug Logging */
#include "mqtt_logging.h"

#include "mqtt.h"
#include "application.h"
#include "bleGap.h"
#include "aws_application_version.h"

/* Shadow include. */
#include "aws_iot_shadow.h"
#include "shadow.h"

/**
 * @brief The keep-alive interval used for this demo.
 *
 * An MQTT ping request will be sent periodically at this interval.
 */
#define KEEP_ALIVE_SECONDS                       ( 60 )

/**
 * @brief The Last Will and Testament topic name in this demo.
 *
 * The MQTT server will publish a message to this topic name if this client is
 * unexpectedly disconnected.
 */
// Will Topic should be dw/things/<ThingName>/update
#define WILL_TOPIC_NAME_TEMPLATE		"dw/things/%s/update"

/**
 * @brief The message to publish to expanded #WILL_TOPIC_NAME_TEMPLATE.
 */
#define WILL_MESSAGE \
	"{" \
    "\"state\":{" \
        "\"reported\":{" \
            "\"connected\":false," \
            "\"State\":\"unknown\"," \
			"\"MinToChilled\":-1" \
        "}" \
    "}" \
"}"

#define SHADOW_MESSAGE	\
	"{" \
	"\"Version_ESP\":%u.%03u,"	\
	"\"Build_ESP\":%u,"			\
	"\"connected\":true,"		\
	"\"Model\":\"%s\","			\
	"\"SerialNumber\":\"%s\""	\
	"}"

/**
* @brief The length of #WILL_MESSAGE.
*/
#define WILL_MESSAGE_LENGTH                      ( ( size_t ) ( sizeof( WILL_MESSAGE ) - 1 ) )

/**
* @brief Keepalive seconds of the MQTT connection. "AWS IoT does not support keep-alive intervals less than 30 seconds"
*/
#define	MQTT_KEEPALIVE_SECONDS			  ( 40 )

/**
* @brief Connection timeout in ms
*/
#define CONN_TIMEOUT_MS                   ( 2000UL )

/**
 * @brief Max MQTT message length. 128kB
 */
#define MAX_MQTT_PAYLOAD_LEN				( 131072 )

/**
* @brief The maximum number of times each PUBLISH will be retried.
*/
#define PUBLISH_RETRY_LIMIT                      ( 5 )

/**
* @brief A PUBLISH message is retried if no response is received within this
* time.
*/
#define PUBLISH_RETRY_MS                         ( 1000 )

/**
* @brief Timeout to receive ACK from server on shadow update
*/
#define SHADOW_TIMEOUT_MS					( 5000UL )


/**
* @brief Flag to track if MQTT is actively connected to broker
*/
static bool	_mqttConnected = false;

/**
* @brief Pointer to the current MQTT connection context.
*/
static IotMqttConnection_t		_pCurrentMqttConnection = NULL;

static _mqttConnectedCallback_t _connectedCallback = NULL;
static const void * _callbackParams = NULL;

/**
 * @brief Disconnect callback for the loss of an MQTT connection. Tracks the connected parameters
 *
 * @param[in] param Ignored. No callback context provided when registering callback.
 * @param[in] mqttCallbackParams MQTT callback details provided from MQTT service
 *
 */
static void _mqttDisconnectCallback(void * param, IotMqttCallbackParam_t * mqttCallbackParams)
{
	( void ) param;

    /* Log the reason for MQTT disconnect.*/
    switch( mqttCallbackParams->u.disconnectReason )
    {
        case IOT_MQTT_DISCONNECT_CALLED:
            IotLogInfo( "Mqtt disconnected due to invoking diconnect function.\r\n" );
            break;

        case IOT_MQTT_BAD_PACKET_RECEIVED:
            IotLogInfo( "Mqtt disconnected due to invalid packet received from the network.\r\n" );
            break;

        case IOT_MQTT_KEEP_ALIVE_TIMEOUT:
            IotLogInfo( "Mqtt disconnected due to Keep-alive response not received.\r\n" );
            break;

        default:
            IotLogInfo( "Mqtt disconnected due to unknown reason." );
            break;
    }

	_mqttConnected = false;

	_pCurrentMqttConnection = NULL;
}

/**
 * @brief Update shadow callback
 *
 * @param[in] pCallbackContext	Callback context. In this case a semaphore
 * @param[in] pCallbackParam	Not used. Information on the shadow update proved by the shadow library.
 */
static void _updateShadowCallback(void *pCallbackContext, AwsIotShadowCallbackParam_t *pCallbackParam)
{
	IotSemaphore_t* pPublishesReceived = (IotSemaphore_t*)pCallbackContext;
	IotSemaphore_Post(pPublishesReceived);
	IotLogInfo( "Shadow Connected Update Received" );
}

/**
 * @brief Update the connected parameter of the shadow
 *
 * @param[in] pIdentifier 		Thing name
 *
 * @return `ESP_OK` if the connection is successfully established; Shadow publish update failure
 * otherwise.
 */
static esp_err_t _updateShadowConnected(const char *pIdentifier)
{
	esp_err_t	err = ESP_OK;
	char 		shadowBuffer[256];
	char 		sernum[13] = {0};
	size_t 		length = sizeof( sernum );

	// Get Serial Number from NV storage
	// Note that sernum[] is defined with length 13 and zero filled, when the 12-byte blob is fetched it will become null-terminated string
	bleGap_fetchSerialNumber(sernum, &length);

	/* Create JSON Shadow Update document. Example: report Version as "1.001" */
	snprintf( shadowBuffer, 256, SHADOW_MESSAGE,
        xAppFirmwareVersion.u.x.ucMajor,
        xAppFirmwareVersion.u.x.ucMinor,
        xAppFirmwareVersion.u.x.usBuild,
		"GEN1",
		sernum
		);

	IotLogInfo( "Shadow Update: %s", shadowBuffer );

	/* Create semaphore to wait for ACK from shadow */
	IotSemaphore_t	shadowUpdateSemaphore;
	if( IotSemaphore_Create( &shadowUpdateSemaphore, 0, 1 ) == false )
	{
		IotLogError("Failed to create shadow update received semaphore");
		err = ESP_FAIL;
	}

	/* Set callback info for Shadow update complete	*/
	AwsIotShadowCallbackInfo_t updateCallback = AWS_IOT_SHADOW_CALLBACK_INFO_INITIALIZER;
	updateCallback.function = _updateShadowCallback;
	updateCallback.pCallbackContext = &shadowUpdateSemaphore;

	/* Publish Shadow Update document */
	if(err == ESP_OK){
		err = updateReportedShadow(shadowBuffer, strlen(shadowBuffer), &updateCallback);
	}

	if(err == ESP_OK){
		if( IotSemaphore_TimedWait( &shadowUpdateSemaphore, SHADOW_TIMEOUT_MS ) == false )
		{
			IotLogError( "Failed to update shadow to connected. (Semaphore Timed Out Waiting for Shadow ACK)" );
			err = ESP_FAIL;
		}
	}

	IotSemaphore_Destroy( &shadowUpdateSemaphore );

	return err;

}

/**
 * @brief 	Getter for the MQTT connection status
 *
 * @return 	true is connected. false otherwise
 */
bool	mqtt_IsConnected(void){

	return _mqttConnected;
}

/**
 * @brief 	Disconnect the current MQTT connection if there is one
 */
void mqtt_disconnectMqttConnection(void){

	if(_pCurrentMqttConnection != NULL)
	{
		IotMqtt_Disconnect(_pCurrentMqttConnection, 0);
		_pCurrentMqttConnection = NULL;
	}
}

/**
 * @brief 	Get the handle of the current MQTT connection
 *
 * @param[out]    mqttConnection  Set to the handle of the current MQTT connection
 *
 * @return `ESP_OK` if there is a current connection ESP_FAIL otherwise
 */
esp_err_t	mqtt_GetMqtt(IotMqttConnection_t * mqttConnection){

	esp_err_t err = ESP_OK;

	if(_pCurrentMqttConnection == NULL){
		err = ESP_FAIL;
	}

	if(err == ESP_OK){
		*mqttConnection = _pCurrentMqttConnection;
	}

	return err;

}

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
esp_err_t	mqtt_SendMsgToTopic(const char* topic, uint32_t topicLen, const char* msgBuf, uint32_t msgLen, const IotMqttCallbackInfo_t * pCallbackInfo){

	esp_err_t err = ESP_OK;

	IotMqttPublishInfo_t msgInfo = IOT_MQTT_PUBLISH_INFO_INITIALIZER;

	msgInfo.qos = IOT_MQTT_QOS_1;
	msgInfo.pTopicName = topic;
	msgInfo.topicNameLength = topicLen;
	msgInfo.pPayload = msgBuf;
	msgInfo.payloadLength = msgLen;
	msgInfo.retryMs = PUBLISH_RETRY_MS;
	msgInfo.retryLimit = PUBLISH_RETRY_LIMIT;

	if(msgLen >= MAX_MQTT_PAYLOAD_LEN){
		IotLogError("Error: Message payload over max MQTT message size (128kB)");
		err = ESP_FAIL;
	}

	if(_pCurrentMqttConnection == NULL){
		IotLogError("Error: No active MQTT connection. Cannot send msg to topic");
		err = ESP_FAIL;
	}

	if(err == ESP_OK){
		IotMqttError_t qos1Result = IotMqtt_Publish( _pCurrentMqttConnection, &msgInfo, 0, pCallbackInfo, NULL );
		if(qos1Result != IOT_MQTT_STATUS_PENDING){
			IotLogError("Error publishing MQTT message to topic. Err:%d", qos1Result);
			err = ESP_FAIL;
		}
	}

	if(err == ESP_OK){
		IotLogInfo("Queued publish message to Topic %.*s", topicLen, topic);
	}

	return err;
}

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
esp_err_t mqtt_establishMqttConnection(
                        void * pNetworkServerInfo,
                        void * pNetworkCredentialInfo,
                        const IotNetworkInterface_t * pNetworkInterface,
                        const char * pIdentifier,
                        IotMqttConnection_t * pMqttConnection )
{
	esp_err_t err = ESP_OK;
	IotMqttError_t connectStatus     = IOT_MQTT_STATUS_PENDING;
	IotMqttNetworkInfo_t networkInfo = IOT_MQTT_NETWORK_INFO_INITIALIZER;
    IotMqttConnectInfo_t connectInfo = IOT_MQTT_CONNECT_INFO_INITIALIZER;
    IotMqttPublishInfo_t willInfo    = IOT_MQTT_PUBLISH_INFO_INITIALIZER;
	char willTopicName[50];

	/* Set the members of the network info not set by the initializer. This
	 * struct provided information on the transport layer to the MQTT connection. */
	networkInfo.createNetworkConnection        = true;
	networkInfo.u.setup.pNetworkServerInfo     = pNetworkServerInfo;
	networkInfo.u.setup.pNetworkCredentialInfo = pNetworkCredentialInfo;
	networkInfo.pNetworkInterface              = pNetworkInterface;

	// Set callback for disconnect
	IotMqttCallbackInfo_t mqttCallbackInfo		= IOT_MQTT_CALLBACK_INFO_INITIALIZER;
	mqttCallbackInfo.function					= _mqttDisconnectCallback;
	networkInfo.disconnectCallback				= mqttCallbackInfo;

	/* Set the members of the Last Will and Testament (LWT) message info. The
	 * MQTT server will publish the LWT message if this client disconnects
	 * unexpectedly. */
	snprintf( willTopicName, 50, WILL_TOPIC_NAME_TEMPLATE, pIdentifier );
	willInfo.pTopicName      = willTopicName;
	willInfo.topicNameLength = strlen( willTopicName );
	willInfo.pPayload        = WILL_MESSAGE;
	willInfo.payloadLength   = WILL_MESSAGE_LENGTH;
	IotLogInfo( "Will topic = %s, message = %s", willInfo.pTopicName, willInfo.pPayload );

	/* Set the members of the connection info not set by the initializer. */
	connectInfo.awsIotMqttMode = true;
	connectInfo.cleanSession = true;
	connectInfo.keepAliveSeconds = MQTT_KEEPALIVE_SECONDS;
	connectInfo.pClientIdentifier = pIdentifier;
	connectInfo.clientIdentifierLength = ( uint16_t ) strlen( pIdentifier );
	connectInfo.pWillInfo = &willInfo;


	/* Connect to the broker. */
	connectStatus = IotMqtt_Connect( &networkInfo,
                                     &connectInfo,
                                     CONN_TIMEOUT_MS,
                                     pMqttConnection );

	if( connectStatus != IOT_MQTT_SUCCESS )
	{
		IotLogError( "ERROR: MQTT CONNECT returned error %s.", IotMqtt_strerror( connectStatus ) );
		/* Do we need to clean up a failed MQTT connection ? */
//		vMqttAppDeleteNetworkConnection( &xConnection );
		err = connectStatus;
	}
	else
	{
		_mqttConnected = true;
		_pCurrentMqttConnection = *pMqttConnection;
		IotLogInfo( "Mqtt connection established" );
		IotLogInfo( "MQTT demo client identifier is %.*s (length %hu).",
		                    connectInfo.clientIdentifierLength,
		                    connectInfo.pClientIdentifier,
		                    connectInfo.clientIdentifierLength );

		err = _updateShadowConnected(pIdentifier);

		_connectedCallback(_callbackParams);

	}

	return err;
}

/**
 * @brief	Deinitialization function for the MQTT library. Frees resources associated with the mqtt library
 */
void	mqtt_Cleanup(void)
{
	IotMqtt_Cleanup();
}

/**
 * @brief	Initialize the MQTT library and set a callback that triggers on a connection with the MQTT broker
 *
 * @param[in]   callback	Callback function forwhen an MQTT connection is successful
 * @param[in]	pParams		Parameters to pass to the callback function
 *
 * @return `EXIT_SUCCESS` if the mqtt library is successfully initialized; `EXIT_FAILURE`
 * otherwise.
 */
esp_err_t	mqtt_Init(_mqttConnectedCallback_t callback, const void * pParams)
{
	esp_err_t err = ESP_OK;

	if( IotMqtt_Init()  != IOT_MQTT_SUCCESS )
	{
		IotLogError( "Failed to initialize MQTT Library." );
		err = ESP_FAIL;
	}

	if(err == ESP_OK)
	{
		if(callback != NULL){
			_connectedCallback = callback;
		}
		if(pParams != NULL){
			_callbackParams = pParams;
		}
	}

	return err;

}
