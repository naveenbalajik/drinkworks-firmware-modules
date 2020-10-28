/**
 * @file	mqtt.c
 */

/* Standard includes. */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

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
#include "platform/iot_clock.h"
#include "platform/iot_network.h"

/* Debug Logging */
#include "mqtt_logging.h"

#include "mqtt.h"
#include "application.h"

/* Shadow include. */
#include "aws_iot_shadow.h"
#include "shadow.h"

#include "wifiFunction.h"

#define	MQTT_STACK_SIZE    ( 4096 + 1024 )

#define	MQTT_TASK_PRIORITY	( 15 )

#define	MQTT_TASK_NAME	( "MqttTask" )
/**
 * @brief The keep-alive interval
 *
 * An MQTT ping request will be sent periodically at this interval.
 */
#define KEEP_ALIVE_SECONDS                       ( 60 )

/**
 * @brief The Last Will and Testament topic name
 *
 * The MQTT server will publish a message to this topic name if this client is
 * unexpectedly disconnected.
 */
// Will Topic should be dw/things/<ThingName>/update
#define WILL_TOPIC_NAME_TEMPLATE		"dw/things/%s/update"

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
 * @brief The base interval in seconds for retrying network connection.
 */
#define MQTT_CONN_RETRY_BASE_INTERVAL_SECONDS    ( 2U )

/**
 * @brief The maximum interval in seconds for retrying network connection.
 */
#define MQTT_CONN_RETRY_MAX_INTERVAL_SECONDS     ( 360U )

/*
 * @brief	MQTT State Machine States
 */
typedef	enum
{
	eMqttInitialize,
	eMqttUnprovisioned,
	eMqttDisconnected,
	eMqttConnected
} mqttState_t;

/**
 * @brief	MQTT control structure
 */
typedef struct
{
	TaskHandle_t			taskHandle;												/**< handle for MQTT Task */
	mqttState_t				mqttState;
	int						retryInterval;
	void * 					pNetworkServerInfo;
	IotMqttConnection_t 	pMqttConnection;
	const IotNetworkInterface_t *	pNetworkInterface;
	void * 					pNetworkCredentialInfo;
	const char *			pIdentifier;
	bool					bConnectionParameters;
	bool					bMqttConnected;
	_mqttConnectedCallback_t connectedCallback;
	_mqttDisconnectedCallback_t disconnectedCallback;
	const char *			pLWAT;													/**< Last Will and Testament message */
} mqttData_t;

static mqttData_t mqttData =
{
	.mqttState = eMqttInitialize,
	.pMqttConnection = NULL,
	.bConnectionParameters = false,
	.bMqttConnected = false,
	.connectedCallback = NULL,
	.disconnectedCallback = NULL,
	.retryInterval = MQTT_CONN_RETRY_BASE_INTERVAL_SECONDS,
};

static IotMqttConnection_t _mqttConnection = IOT_MQTT_CONNECTION_INITIALIZER;


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
            IotLogInfo( "Mqtt disconnected due to invoking disconnect function.\r\n" );
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

    mqttData.bMqttConnected = false;

    mqttData.pMqttConnection = NULL;

    if( mqttData.disconnectedCallback != NULL )
    {
    	mqttData.disconnectedCallback();
    }

}


/**
 * @brief	Establish an MQTT Connection, using Client Identifier and setting Last Will and Testament message
 *
 * @return `EXIT_SUCCESS` if the connection is successfully established; `EXIT_FAILURE`
 * otherwise.
 */
static esp_err_t _establishMqttConnection( void )
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
	networkInfo.u.setup.pNetworkServerInfo     = mqttData.pNetworkServerInfo;
	networkInfo.u.setup.pNetworkCredentialInfo = mqttData.pNetworkCredentialInfo;
	networkInfo.pNetworkInterface              = mqttData.pNetworkInterface;

	// Set callback for disconnect
	IotMqttCallbackInfo_t mqttCallbackInfo		= IOT_MQTT_CALLBACK_INFO_INITIALIZER;
	mqttCallbackInfo.function					= _mqttDisconnectCallback;
	networkInfo.disconnectCallback				= mqttCallbackInfo;

	/* Set the members of the Last Will and Testament (LWT) message info. The
	 * MQTT server will publish the LWT message if this client disconnects
	 * unexpectedly. */
	snprintf( willTopicName, 50, WILL_TOPIC_NAME_TEMPLATE, mqttData.pIdentifier );
	willInfo.pTopicName      = willTopicName;
	willInfo.topicNameLength = strlen( willTopicName );
	willInfo.pPayload        = mqttData.pLWAT;
	willInfo.payloadLength   = strlen( mqttData.pLWAT );
	IotLogInfo( "Will topic = %s, message = %s", willInfo.pTopicName, willInfo.pPayload );

	/* Set the members of the connection info not set by the initializer. */
	connectInfo.awsIotMqttMode = true;
	connectInfo.cleanSession = true;
	connectInfo.keepAliveSeconds = MQTT_KEEPALIVE_SECONDS;
	connectInfo.pClientIdentifier = mqttData.pIdentifier;
	connectInfo.clientIdentifierLength = ( uint16_t ) strlen( mqttData.pIdentifier );
	connectInfo.pWillInfo = &willInfo;

	/* Connect to the broker. */
	connectStatus = IotMqtt_Connect( &networkInfo,
                                     &connectInfo,
                                     CONN_TIMEOUT_MS,
									 &_mqttConnection );

	IotLogInfo( "IotMqtt_Connect completed: %d", connectStatus );

    mqttData.pMqttConnection = _mqttConnection;

	if( connectStatus != IOT_MQTT_SUCCESS )
	{
		IotLogError( "ERROR: MQTT CONNECT returned error %s.", IotMqtt_strerror( connectStatus ) );
		/* Do we need to clean up a failed MQTT connection ? */
//		vMqttAppDeleteNetworkConnection( &xConnection );
		err = connectStatus;
	}
	else
	{
		mqttData.bMqttConnected = true;
		IotLogInfo( "Mqtt connection established" );
		IotLogInfo( "MQTT client identifier is %.*s (length %hu).",
		                    connectInfo.clientIdentifierLength,
		                    connectInfo.pClientIdentifier,
		                    connectInfo.clientIdentifierLength );

		/* If connected callback is registered, call it */
		if( mqttData.connectedCallback != NULL )
		{
			mqttData.connectedCallback( );
		}
	}

	return err;
}


/**
 * @brief Delay before retrying network connection up to a maximum interval.
 */
static void _connectionRetryDelay( void )
{
    unsigned int retryIntervalwithJitter = 0;

    if( ( mqttData.retryInterval * 2 ) >= MQTT_CONN_RETRY_MAX_INTERVAL_SECONDS )
    {
        /* Retry interval is already max.*/
    	 mqttData.retryInterval = MQTT_CONN_RETRY_MAX_INTERVAL_SECONDS;
    }
    else
    {
        /* Double the retry interval time.*/
    	 mqttData.retryInterval *= 2;
    }

    /* Add random jitter upto current retry interval .*/
    retryIntervalwithJitter =  mqttData.retryInterval + ( rand() %  mqttData.retryInterval );

    IotLogInfo( "Retrying network connection in %d Secs ", retryIntervalwithJitter );

    /* Delay for the calculated time interval .*/
    IotClock_SleepMs( retryIntervalwithJitter * 1000 );
}

/**
 * @brief	MQTT Task
 *
 * The MQTT state machine runs as a separate task.  It handles connections, disconnections,
 * and reconnections.
 */
static void mqtt_task( void *arg )
{

	for(;;)
	{
		switch( mqttData.mqttState )
		{
			case eMqttInitialize:
				mqttData.mqttState = eMqttUnprovisioned;
				IotLogDebug( "mqtt -> Unprovisioned" );
				break;

			case eMqttUnprovisioned:
				if( mqttData.bConnectionParameters == true )
				{
					mqttData.mqttState = eMqttDisconnected;
					IotLogDebug( "mqtt -> Disconnected" );
				}
				else
				{
					vTaskDelay( 500 / portTICK_PERIOD_MS );
				}
				break;

			/*
			 * If MQTT connection has not been established, periodically try to establish one.
			 * Do not try to establish connection if not connected to the (WiFi) network.
			 * The retry interval must be randomized to prevent all appliance from trying to connect
			 * simultaneously after a disconnect.
			 */
			case eMqttDisconnected:
				if( mqttData.bMqttConnected )
				{
					mqttData.mqttState = eMqttConnected;
					IotLogDebug( "mqtt -> Connected" );
				}
				else
				{
					/* Wait for a Wifi connection */
					if( wifi_GetStatus() != WiFi_Status_Connected )
					{
						vTaskDelay( 500 / portTICK_PERIOD_MS );
						mqttData.retryInterval = MQTT_CONN_RETRY_BASE_INTERVAL_SECONDS;
					}
					else
					{
						_establishMqttConnection();

						/* check if now connected, if not delay before retrying */
						if( mqttData.bMqttConnected == false )
						{
							_connectionRetryDelay();
						}
					}
				}
				break;

			case eMqttConnected:
				if (mqttData.bMqttConnected == false )
				{
					mqttData.mqttState = eMqttDisconnected;
					IotLogDebug( "mqtt -> Disconnected" );
				}
				else
				{
					/* TODO: wait on disconnect, until then ... sleep */
					vTaskDelay( 500 / portTICK_PERIOD_MS );
				}
				break;

			default:
				mqttData.mqttState = eMqttInitialize;
				break;
		}
	}
}

/* ************************************************************************* */
/* ************************************************************************* */
/* **********        I N T E R F A C E   F U N C T I O N S        ********** */
/* ************************************************************************* */
/* ************************************************************************* */

/**
 * @brief 	Getter for the MQTT connection status
 *
 * @return 	true is connected. false otherwise
 */
bool	mqtt_IsConnected( void )
{
	return mqttData.bMqttConnected;
}

/**
 * @brief 	Disconnect the current MQTT connection if there is one
 */
void mqtt_disconnectMqttConnection( void )
{

	IotLogInfo( "mqtt_disconnectMqttConnection");
	if( mqttData.pMqttConnection != NULL )
	{
		IotMqtt_Disconnect( mqttData.pMqttConnection, IOT_MQTT_FLAG_CLEANUP_ONLY );
		mqttData.pMqttConnection = NULL;
	}

	mqttData.bMqttConnected = false;

}

/**
 * @brief 	Get the handle of the current MQTT connection
 *
 * @return    Handle of the current MQTT connection
 */
IotMqttConnection_t	mqtt_getConnection( void )
{
	return mqttData.pMqttConnection;
}

/**
 * @brief 	Get the Client Identifier (ThingName)
 *
 * @return    Pointer to Client Identifier (NULL terminated)
 */
const char * mqtt_getIdentifier( void )
{
	return mqttData.pIdentifier;
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
esp_err_t	mqtt_SendMsgToTopic( const char* topic, uint32_t topicLen, const char* msgBuf, uint32_t msgLen, const IotMqttCallbackInfo_t * pCallbackInfo )
{

	esp_err_t err = ESP_OK;

	IotMqttPublishInfo_t msgInfo = IOT_MQTT_PUBLISH_INFO_INITIALIZER;

	msgInfo.qos = IOT_MQTT_QOS_1;
	msgInfo.pTopicName = topic;
	msgInfo.topicNameLength = topicLen;
	msgInfo.pPayload = msgBuf;
	msgInfo.payloadLength = msgLen;
	msgInfo.retryMs = PUBLISH_RETRY_MS;
	msgInfo.retryLimit = PUBLISH_RETRY_LIMIT;

	if( msgLen >= MAX_MQTT_PAYLOAD_LEN )
	{
		IotLogError( "Error: Message payload over max MQTT message size (128kB)" );
		err = ESP_FAIL;
	}

	if( mqttData.pMqttConnection == NULL )
	{
		IotLogError( "Error: No active MQTT connection. Cannot send msg to topic" );
		err = ESP_FAIL;
	}

	if( err == ESP_OK )
	{
		IotMqttError_t qos1Result = IotMqtt_Publish( mqttData.pMqttConnection, &msgInfo, 0, pCallbackInfo, NULL );
		if( qos1Result != IOT_MQTT_STATUS_PENDING )
		{
			IotLogError( "Error publishing MQTT message to topic. Err:%d", qos1Result );
			err = ESP_FAIL;
		}
	}

	if( err == ESP_OK )
	{
		IotLogInfo("Queued publish message to Topic %.*s", topicLen, topic);
	}

	return err;
}

/**
 * @brief	Set the MQTT Connection parameters
 *
 * Parameters are saved in static mqttData structure for establishing MQTT connection.
 *
 * @param[in]    pNetworkServerInfo      Passed to the MQTT connect function when establishing the MQTT connection.
 * @param[in]    pNetworkCredentialInfo  Passed to the MQTT connect function when establishing the MQTT connection.
 * @param[in]    pNetworkInterface       Network interface to use for the connection.
 * @param[in]    pIdentifier             Client Identifier (ThingName)
 */
void mqtt_setConnectionParameters(	void * pNetworkServerInfo,
									void * pNetworkCredentialInfo,
									const IotNetworkInterface_t * pNetworkInterface,
									const char * pIdentifier )
{
	mqttData.pNetworkServerInfo = pNetworkServerInfo;
	mqttData.pNetworkCredentialInfo = pNetworkCredentialInfo;
	mqttData.pNetworkInterface = pNetworkInterface;
	mqttData.pIdentifier = pIdentifier;
	mqttData.bConnectionParameters = true;
}


/**
 * @brief	Initialize the MQTT library and set a callback that triggers on a connection with the MQTT broker
 *
 * @param[in]   connectCallback		Callback function for when an MQTT connection is successful
 * @param[in]   disconnectCallback	Callback function for when an MQTT connection is terminated
 * @param[in]	pLWAT				Pointer to Last Will and Testament message
 *
 * @return `EXIT_SUCCESS` if the mqtt library is successfully initialized; `EXIT_FAILURE`
 * otherwise.
 */
esp_err_t	mqtt_Init( _mqttConnectedCallback_t connectCallback, _mqttDisconnectedCallback_t diconnectCallback, const char *pLWAT )
{
	esp_err_t err = ESP_OK;

	IotLogInfo( "mqtt_Init, LWAT = %s", pLWAT );

	if( IotMqtt_Init()  != IOT_MQTT_SUCCESS )
	{
		IotLogError( "Failed to initialize MQTT Library." );
		err = ESP_FAIL;
	}

	if( err == ESP_OK )
	{
		if( connectCallback != NULL )
		{
			mqttData.connectedCallback = connectCallback;
		}
		if( diconnectCallback != NULL )
		{
			mqttData.disconnectedCallback = diconnectCallback;
		}

		mqttData.pLWAT = pLWAT;

//		if( pParams != NULL )
//		{
//			mqttData.callbackParams = pParams;
//		}

		/* Create Task */
		IotLogDebug( "Create mqtt_task" );
	    xTaskCreate( mqtt_task, MQTT_TASK_NAME, MQTT_STACK_SIZE, NULL, MQTT_TASK_PRIORITY, &mqttData.taskHandle );
	    if( NULL == mqttData.taskHandle )
		{
	        return ESP_FAIL;
	    }

	    IotLogInfo( "%s created", MQTT_TASK_NAME );
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

