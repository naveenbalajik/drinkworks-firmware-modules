/**
 * @file fleetProvisioning.c
 *
 * Created on: May 4, 2020
 * 		Author: nick.weber
 */

/* Standard includes. */
#include <string.h>
#include <stddef.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"

/* PKCS#11 includes. */
#include "iot_pkcs11_config.h"
#include "iot_pkcs11.h"
#include "iot_pkcs11_pal.h"

/* Platform layer includes. */
#include "platform/iot_clock.h"
#include "platform/iot_threads.h"

/* DW Provisioning*/
#include "fleetProvisioning.h"

/* Flash include*/
#include "nvs_flash.h"
#include "nvs_utility.h"

/* Certificate and Key Provisioning*/
#include "pkcs11_cred_storage_utility.h"

/* ESP Wifi */
#include "esp_wifi.h"

/* MQTT include. */
#include "iot_mqtt.h"

/* IoT Network */
#include "platform/iot_network.h"

/* DW Ble Gap for SN lookup */
#include "bleGap.h"

/* mjson include*/
#include "mjson.h"

/* tcpIP connection parameters */
#include "aws_clientcredential.h"
#include "platform/iot_network_freertos.h"
#include "iot_network_manager_private.h"

/* credential decryption */
#include "credential_decryption_utility.h"

/* Debug Logging */
#include "fleetProvisioning_logging.h"

/**
* @brief The longest client identifier that an MQTT server must accept (as defined
* by the MQTT 3.1.1 spec) is 23 characters. Add 1 to include the length of the NULL
* terminator.
*/
#define CLIENT_IDENTIFIER_MAX_LENGTH				( 24 )

/**
* @brief The keep-alive interval used for this demo.
*
* An MQTT ping request will be sent periodically at this interval.
*/
#define KEEP_ALIVE_SECONDS							( 60 )

/**
 * @brief The timeout for MQTT operations.
 */
#define MQTT_TIMEOUT_MS								( 15000 )

 /**
 * @brief A PUBLISH message is retried if no response is received within this
 * time.
 */
#define PUBLISH_RETRY_MS							( 1000 )

 /**
 * @brief Maximum number of times each PUBLISH in this demo will be retried.
 */
#define PUBLISH_RETRY_LIMIT							( 10 )

 /**
 * @brief Number of topics to subscribe to relating to certificate creation
 */
#define CERT_CREATED_SUBSCRIBE_TOPIC_CNT			(2)

 /**
 * @brief Number of topics to subscribe to relating to fleet provisioning
 */
#define FLEET_PROVIS_SUBSCRIBE_TOPIC_CNT			(2)

 /**
 * @brief Max value for the fleet provisioning semaphore
 */
#define FLEET_PROV_SEMAPHORE_MAX_VAL				(1024)

 /**
 * @brief Length of final certificate array
 */
#define FINAL_CERT_MAX_LENGTH						(2048)

 /**
 * @brief Length of final key array
 */
#define	FINAL_KEY_MAX_LENGTH						(2048)


 /**
 * @brief Topic to request certificate from AWS
 */
#define CERT_CREATE_REQUEST_AWS_TOPIC_NAME			"$aws/certificates/create/json"

 /**
 * @brief Return topic for an accepted certificate creation
 */
#define CERT_CREATE_RETURN_TOPIC_ACCEPTED			"$aws/certificates/create/json/accepted"

 /**
 * @brief Return topic for a rejected certificate creation
 */
#define CERT_CREATE_RETURN_TOPIC_REJECTED			"$aws/certificates/create/json/rejected"

 /**
 * @brief Topic to register and fleet provision thing with AWS. Wildcard is replaced by template name by init function
 */
#define PROVISION_TOPIC_STUCTURE					"$aws/provisioning-templates/*/provision/json"

/**
 * @brief Fleet Provisioning stack size
 */
#define FLEET_PROV_STACK_SIZE	( 8192 )

/**
 * @brief Fleet Provisioning task priority
 */
#define	FLEET_PROV_TASK_PRIORITY	( 17 )

/**
 * @brief Fleet Provisioning task handle
 */
static TaskHandle_t _xFleetProvTaskHandle = NULL;

/**
 * @brief Static instance of the fleet provisioning parameters. Set during initialization
 */
static fleetProv_InitParams_t	_fleetProvParams = {0};

/**
 * @brief Current Status of the fleet provisioning
 */
static eFleetProv_Status_t _fleetProvStatus = eFLEETPROV_NOT_INITIALIZED;

/**
 * @brief Fleet provisioning template name. This will dictate what topics the fleet provisioning request is sent to
 */
static char * pTemplateName = NULL;

/**
 * @brief Fleet provisioning template request topic. Set by init
 */
static char * pProvisionRequestTopic = NULL;

/**
 * @brief Fleet provisioning accepted topic. Set by init
 */
static char * pProvisionRequestAcceptedTopic = NULL;

/**
 * @brief Fleet provisioning rejected topic. Set by init
 */
static char * pProvisionRequestRejectedTopic = NULL;

 /**
 * @brief Static array for final certificate. Temporary storage for the cert until AWS responds 'Accepted' to the register thing request
 */
static unsigned char 	*finalCert = NULL;
static unsigned int		finalCertLength = 0;

/**
* @brief Static array for final key. Temporary storage for the cert until AWS responds 'Accepted' to the register thing request
*/
static unsigned char 	*finalKey = NULL;
static unsigned int		finalKeyLength = 0;

/**
* @brief Replaces characters in string with specified characters
*
* Note that the new word length must be equal to or less than old word length. Otherwise this function will return -1.
*
* @param[in] str: pointer to the string to be modified
* @param[in] strSize: size of string
* @param[in] oldWord: word/character to be changed. Must be null terminated
* @param[in] newWord: new word/character to replace old word. Must be null terminated. Must also be <= length of old word
*
* @return The new string size after replacement completed. -1 if error
*/
static int _replaceCharsInString(char* str, int strSize, const char* oldWord, const char* newWord) {

	int i = 0;
	int wordLengthDifference;

	wordLengthDifference = strlen(oldWord) - strlen(newWord);

	if (wordLengthDifference < 0) {
		return -1;
	}

	for (i = 0; i < strSize; i++) {
		if (memcmp(str + i, oldWord, strlen(oldWord)) == 0) {
			memcpy((str + i), newWord, strlen(newWord));
			if (wordLengthDifference > 0) {
				strSize -= wordLengthDifference;
				memcpy(str + i + 1, str + i + 1 + wordLengthDifference, strSize - i);
			}
		}
	}

	*(str + strSize - 1) = 0;

	return strSize;

}

/**
 * @brief Replaces wildcard in a string and appends another string to the end of the string
 * Note: All inputs to the function must be strings (null terminated)
 * Note: Output string will be dynamically allocated
 *
 * @param[in] strWithWildcard		String with wildcard
 *
 * @return Pointer to new string allocation if successful. NULL if failed
 */
static char * _replaceWildcardAppend(const char * strWithWildcard, const char * replacement, const char * additions)
{
	int err = ESP_OK;
	int index = 0;
	char * newString;
	char * wildcardPosition = NULL;

	// Allocate memory for the new string
	if(additions == NULL)
	{
		newString = (char *) calloc(strlen(strWithWildcard) + strlen(replacement), sizeof(char));
	}
	else
	{
		newString = (char *) calloc(strlen(strWithWildcard) + strlen(replacement) + strlen(additions), sizeof(char));
	}
	if(newString == NULL)
	{
		err = ESP_FAIL;
		IotLogError("Error: Could not allocate memory for new string with replaced wildcard");
	}

	if(err == ESP_OK){
		// Set the newly allocated array to the string with wildcard
		memcpy(newString, strWithWildcard, strlen(strWithWildcard));
		// Find the wildcard
		wildcardPosition = strchr(strWithWildcard, 42);
		index = wildcardPosition - strWithWildcard;
		if(wildcardPosition == NULL){
			err = ESP_FAIL;
			free(newString);
			newString = NULL;
			IotLogError("Error: Wildcard not found in string");
		}
	}

	if(err ==ESP_OK){
		// Replace the wildcard with the replacement string
		strcpy(newString + index, replacement);
		index += strlen(replacement);
		// Refill the end of the string
		strcpy(newString + index, wildcardPosition + 1);
		index += strlen(wildcardPosition + 1);
		// Place any additions on the end of the string
		if(additions != NULL){
			strcpy(newString + index, additions);
		}
	}

	return newString;
}


/**
 * @brief Establish a new connection to the MQTT server.
 * The MQTT connection will use the serial number as the client identifier
 *
 * @param[in] pNetworkServerInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkCredentialInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkInterface The network interface to use for this demo.
 * @param[out] pMqttConnection Set to the handle to the new MQTT connection.
 *
 * @return `EXIT_SUCCESS` if the connection is successfully established; `EXIT_FAILURE`
 * otherwise.
 */
static int32_t _establishMqttConnection(void* pNetworkServerInfo,
									void* pNetworkCredentialInfo,
									const IotNetworkInterface_t* pNetworkInterface,
									IotMqttConnection_t* pMqttConnection)
{
	esp_err_t err = 0;
	IotMqttError_t connectStatus = IOT_MQTT_STATUS_PENDING;
	IotMqttNetworkInfo_t networkInfo = IOT_MQTT_NETWORK_INFO_INITIALIZER;
	IotMqttConnectInfo_t connectInfo = IOT_MQTT_CONNECT_INFO_INITIALIZER;

	/* Set the members of the network info not set by the initializer. This
	 * struct provided information on the transport layer to the MQTT connection. */
	networkInfo.createNetworkConnection = true;
	networkInfo.u.setup.pNetworkServerInfo = pNetworkServerInfo;
	networkInfo.u.setup.pNetworkCredentialInfo = pNetworkCredentialInfo;
	networkInfo.pNetworkInterface = pNetworkInterface;

	/* Set the members of the connection info not set by the initializer. */
	connectInfo.awsIotMqttMode = true;
	connectInfo.cleanSession = true;
	connectInfo.keepAliveSeconds = KEEP_ALIVE_SECONDS;

	/* Use the serial number for the thing name while requesting final certificates */
	char pClientIdentifierBuffer[CLIENT_IDENTIFIER_MAX_LENGTH] = { 0 };
	/* Length is used as an in/out parameter to the nvs api used in the fetch serial number function. On input, the length should be set to buffer length */
	size_t length = CLIENT_IDENTIFIER_MAX_LENGTH;
	bleGap_fetchSerialNumber(pClientIdentifierBuffer, &length);

	connectInfo.pClientIdentifier = pClientIdentifierBuffer;
	connectInfo.clientIdentifierLength = (uint16_t)strlen(pClientIdentifierBuffer);

	connectStatus = IotMqtt_Connect(&networkInfo,
		&connectInfo,
		MQTT_TIMEOUT_MS,
		pMqttConnection);

	if (connectStatus != IOT_MQTT_SUCCESS)
	{
		IotLogError( "ERROR: MQTT CONNECT returned error %s.", IotMqtt_strerror( connectStatus ) );
		err = connectStatus;
		return err;
	}

	return err;

}

/**
 * @brief Cleanup malloc'd memory
 *
 */
static void _fleetProvCleanup(void){
	if (NULL != finalCert)
	{
		vPortFree(finalCert);
		finalCert = NULL;
	}

	if (NULL != finalKey)
	{
		vPortFree(finalKey);
		finalKey = NULL;
	}

	if(NULL != pProvisionRequestTopic)
	{
		vPortFree(pProvisionRequestTopic);
		pProvisionRequestTopic = NULL;
	}

	if(NULL != pProvisionRequestAcceptedTopic)
	{
		vPortFree(pProvisionRequestAcceptedTopic);
		pProvisionRequestAcceptedTopic = NULL;
	}

	if(NULL != pProvisionRequestRejectedTopic)
	{
		vPortFree(pProvisionRequestRejectedTopic);
		pProvisionRequestRejectedTopic = NULL;
	}

}


/**
 * @brief Send the fleet provisioning request to AWS. The following items are sent to AWS to perform
 * the fleet provisioning: certificate token, MAC address, and serial number. The certificate token
 * is received after requesting a final certificate from AWS.
 *
 * @param[in] mqttConnection	Current Mqtt connection
 * @param[in] pCertOwnershipToken	Certificate ownership token for the final certificate
 * @param[in] sizeCertOwnershipToken	Size of the certificate ownership token
 *
 * @return 	ESP_OK
 */
static uint32_t _fleetProvisionRequest(IotMqttConnection_t mqttConnection, const char* pCertOwnershipToken, int sizeCertOwnershipToken)
{
	IotMqttPublishInfo_t publishInfo = IOT_MQTT_PUBLISH_INFO_INITIALIZER;
	publishInfo.qos = IOT_MQTT_QOS_1;
	publishInfo.retryMs = PUBLISH_RETRY_MS;
	publishInfo.retryLimit = PUBLISH_RETRY_LIMIT;
	publishInfo.pTopicName = pProvisionRequestTopic;
	publishInfo.topicNameLength = strlen(pProvisionRequestTopic);

	// Get the parameter information for the fleet provisioning response
	char sernum[16] = {0};
	size_t length = sizeof( sernum );
	// Get Serial Number from NV storage
	bleGap_fetchSerialNumber(sernum, &length);
	/* Get WiFi MAC Address (byte reversed) to attach to end of message*/
	uint8_t wifi_addr[6];
	esp_wifi_get_mac(WIFI_IF_STA, wifi_addr);
	char macAddress[18] = {0};
	sprintf(macAddress, "%02x:%02x:%02x:%02x:%02x:%02x", wifi_addr[5], wifi_addr[4], wifi_addr[3], wifi_addr[2], wifi_addr[1], wifi_addr[0]);

	// Create an mjson document with the certificate ownership token
	char * provisionPayload = NULL;
	mjson_printf(&mjson_print_dynamic_buf, &provisionPayload,"{%Q:%.*s, %Q: {%Q:%Q, %Q:%Q}}", \
															"certificateOwnershipToken", \
															sizeCertOwnershipToken, pCertOwnershipToken, \
															"parameters", \
															"SerialNumber", \
															sernum, \
															"MACaddress", \
															macAddress
															);

	// Set the publish payload to the
	publishInfo.pPayload = provisionPayload;
	publishInfo.payloadLength = strlen(provisionPayload);
	IotMqtt_Publish(mqttConnection,
									&publishInfo,
									0,
									NULL,
									NULL);

	if(NULL != provisionPayload)
	{
		free(provisionPayload);
	}

	return ESP_OK;
}

/**
 * @brief Store Thing name in NV memory
 *
 * Thing name, returned by AWS, is stored in NV memory, Namespace = WiFi, Key= ThingName
 *
 * @param[in] thingName		pointer to null-terminated string
 */
static int32_t _storeThingName(const char* thingName)
{
	esp_err_t xRet;
	xRet = NVS_Set(NVS_THING_NAME, (void*)thingName, NULL);

	if (xRet == ESP_OK)
	{
		IotLogInfo( "storeThingName(%s) - set OK", thingName );
	}

	return xRet;

}

/**
 * @brief Take final certificate and private key and set PKCS11 object to final certs
 *
 * @return 	ESP_OK if successful, error otherwise
 */
static int32_t _setFinalCredsToPKCS11Object(void)
{

	esp_err_t err;
	ProvisioningParams_t xParams;

	xParams.pucClientPrivateKey = finalKey;
	xParams.ulClientPrivateKeyLength = finalKeyLength;

	xParams.pucClientCertificate = finalCert;
	xParams.ulClientCertificateLength = finalCertLength;

	err = setPKCS11CredObjectParams(&xParams);
	if (err != ESP_OK) {
		NVS_EraseKey(NVS_CLAIM_CERT);
		NVS_EraseKey(NVS_CLAIM_PRIVATE_KEY);
		IotLogError( "FAILED to set final creds to PKCS11 object" );
	}

	if(err == ESP_OK){
		AwsIotNetworkManager_UpdateTCPIPCreds(&xParams);
	}

	return err;
}

/**
 * @brief Callback from fleet provisioning request
 *
 * @param[in] param1	Semaphore for provisioning request
 * @param[in] pOperation	Information about the completed operation passed by the MQTT library.
 */
static void _fleetProvSubscriptionCallback(void* param1, IotMqttCallbackParam_t* const pPublish) 
{	
	esp_err_t err = ESP_OK;
	IotSemaphore_t* pPublishesReceived = (IotSemaphore_t*)param1;
	const char* pPayload = pPublish->u.message.info.pPayload;

	IotLogInfo( "Message received from topic:%.*s", pPublish->u.message.topicFilterLength, pPublish->u.message.pTopicFilter );

	// Ensure that the message came in on the accepted topic
	if(memcmp(pPublish->u.message.pTopicFilter, pProvisionRequestAcceptedTopic, strlen(pProvisionRequestAcceptedTopic)) == 0)
	{
		const char *val;
		int len = 0;
		// Look for the thing name in the message
		if(MJSON_TOK_STRING == mjson_find(pPayload, pPublish->u.message.info.payloadLength, "$.thingName", &val, &len))
		{
			// Extract thing name (max thing name length is 23)
			char thingName[24] = {0};
			memcpy(thingName, val+1, len-2);

			// Store thing name in NVS memory
			err = _storeThingName(thingName);

			if(err == ESP_OK){
				err = _setFinalCredsToPKCS11Object();
			}
			/* The cert/key should be set to nvs memory after they PKCS11 objects are set.
			 This is because the fleet provisioning call checks for the existence of credentials in nvs to confirm they are set.
			 We want to avoid the scenario where are the credentials are set in nvs but fail to get saved as a PKCS11 object */
			if(err == ESP_OK){
				err = NVS_Set(NVS_FINAL_PRIVATE_KEY, finalKey, &finalKeyLength);
			}
			if(err == ESP_OK){
				err = NVS_Set(NVS_FINAL_CERT, finalCert, &finalCertLength);
			}

			if(err == ESP_OK){
				/* Increment the number of PUBLISH messages received. */
				IotSemaphore_Post(pPublishesReceived);
			}
		}
		else
		{
			IotLogError( "Failed. Unable to find thing name in message" );
		}
	}
	else
	{
//		Verbose messaging to help diagnose why a proviosning request is rejected
//		printf( "Rejection message: %.*s\n", pPublish->u.message.info.payloadLength, ( char * )pPublish->u.message.info.pPayload );
		IotLogError( "Failed. Message Received from Fleet Provisioning Rejected Topic" );
	}

}

/**
 * @brief Take the final credentials from NVS and update the TCPIP credentials used by the network manager
 *
 * @return 	ESP_OK if successful, error otherwise
 */
static int32_t _updateTCPIPCredsWithFinalParamsFromNVS(void)
{
	esp_err_t err = ESP_OK;
	uint32_t certBlobSize, keyBlobSize;

	err = NVS_Get_Size_Of(NVS_FINAL_CERT, &certBlobSize);

	if(err == ESP_OK){
		finalCert = pvPortMalloc(certBlobSize);
		err = NVS_Get(NVS_FINAL_CERT, finalCert, &certBlobSize);
	}

	if(err == ESP_OK){
		err = NVS_Get_Size_Of(NVS_FINAL_PRIVATE_KEY, &keyBlobSize);
	}

	if(err == ESP_OK){
		finalKey = pvPortMalloc(keyBlobSize);
		err = NVS_Get(NVS_FINAL_PRIVATE_KEY, finalKey, &keyBlobSize);
	}

	ProvisioningParams_t xParams;

	xParams.pucClientCertificate = finalCert;
	xParams.ulClientCertificateLength = certBlobSize;

	xParams.pucClientPrivateKey = finalKey;
	xParams.ulClientPrivateKeyLength = keyBlobSize;

	if(err == ESP_OK){
		AwsIotNetworkManager_UpdateTCPIPCreds(&xParams);
	}

	return err;
}

/**
 * @brief Callback from certificate create request
 *
 * @param[in] param1	Not used
 * @param[in] pOperation	Information about the completed operation passed by the MQTT library.
 */
static void _certificateCreateSubscriptionCallback(void* param1, IotMqttCallbackParam_t* const pPublish)
{
	IotLogInfo( "Message received from topic:%.*s", pPublish->u.message.topicFilterLength, pPublish->u.message.pTopicFilter );
	const char* pPayload = pPublish->u.message.info.pPayload;
	/* If the message was received on the accepted topic of certificate creation then parse message */
	if (strncmp(pPublish->u.message.pTopicFilter, CERT_CREATE_RETURN_TOPIC_ACCEPTED, sizeof(CERT_CREATE_RETURN_TOPIC_ACCEPTED) - 1) == 0)
	{
		int parsedParametersFound = 0;
		char *val;
		int len = 0;
		// Find the certificate ownership token
		if(MJSON_TOK_STRING == mjson_find(pPayload, pPublish->u.message.info.payloadLength, "$.certificateOwnershipToken", (const char **)&val, &len))
		{
			// Send the ownership token along with the device parameters
			_fleetProvisionRequest(pPublish->mqttConnection, val, len);
			parsedParametersFound++;
		}
		// Find the certificate file
		if(MJSON_TOK_STRING == mjson_find(pPayload, pPublish->u.message.info.payloadLength, "$.certificatePem", (const char **)&val, &len))
		{
			// Copy the certificate into a new buffer before storing in the final cert
			char * cert = (char *) calloc(len, sizeof(char));
			memcpy(cert, val + 1, len - 2);
			if(cert != NULL){
				finalCertLength = _replaceCharsInString(cert, len - 2, "\\n", "\n");
				finalCert = pvPortMalloc(finalCertLength);
				memcpy(finalCert, cert, finalCertLength);
				parsedParametersFound++;
				free(cert);
			}
		}
		// Find the private key
		if(MJSON_TOK_STRING == mjson_find(pPayload, pPublish->u.message.info.payloadLength, "$.privateKey", (const char **)&val, &len))
		{
			finalKeyLength = _replaceCharsInString(val + 1, len - 2, "\\n", "\n");
			finalKey = pvPortMalloc(finalKeyLength);
			memcpy(finalKey, val + 1, finalKeyLength);
			parsedParametersFound++;
		}

		if( parsedParametersFound < 3 )
		{
			IotLogError( "Failed to find ownershipToken, cert, or private key in message" );
		}
	}
	else {
		IotLogError( "AWS rejected request for certificate creation. Response topic: %.*s", pPublish->u.message.topicFilterLength, pPublish->u.message.pTopicFilter );
	}
}

/**
 * @brief Unsubscribe to topics
 *
 * @param[in] mqttConnection	Current Mqtt connection
 * @param[in] pTopicsList	List of topics to unsubscribe from. Must be null terminated strings
 * @param[in] numTopics		Number of topics to unsubscribe from
 *
 * @return 	subscriptionStatus of type IotMqttError_t
 */
static int32_t _unsubscribeTopics(IotMqttConnection_t mqttConnection, const char** pTopicsList, uint8_t numTopics)
{
	IotMqttError_t subscriptionStatus = IOT_MQTT_STATUS_PENDING;
	IotMqttSubscription_t* pSubscriptions = (IotMqttSubscription_t*)pvPortMalloc(numTopics * sizeof(IotMqttSubscription_t));
	int i;

	/* Set up the subscription parameters */
	for (i = 0; i < numTopics; i++)
	{
		pSubscriptions[i].qos = IOT_MQTT_QOS_1;
		pSubscriptions[i].pTopicFilter = pTopicsList[i];
		pSubscriptions[i].topicFilterLength = ((uint16_t)strlen(pTopicsList[i]));
		pSubscriptions[i].callback.function = NULL;
		pSubscriptions[i].callback.pCallbackContext = NULL;
	}

	subscriptionStatus = IotMqtt_TimedUnsubscribe(mqttConnection, pSubscriptions, numTopics, 0, MQTT_TIMEOUT_MS);
	if (IOT_MQTT_SUCCESS != IOT_MQTT_SUCCESS)
	{
		IotLogError( "Failure unsubscribing from topics. Failure code:%d", subscriptionStatus );
	}

	vPortFree(pSubscriptions);

	return subscriptionStatus;
}

/**
 * @brief Subscribe to topics
 *
 * @param[in] mqttConnection	Current Mqtt connection
 * @param[in] pTopicsList	List of topics to unsubscribe from. Must be null terminated string.
 * @param[in] numTopics		Number of topics to unsubscribe from
 * @param[in] callbackFunc	Callback funtion when message received from topic
 * @param[in] pCallbackParameter	parameter to pass to callback function
 *
 * @return 	subscriptionStatus of type IotMqttError_t
 */
static int32_t _subscribeTopics(IotMqttConnection_t mqttConnection,
	const char** pTopicsList,
	uint8_t numTopics,
	void* callbackFunc,
	void* pCallbackParameter)
{
	IotMqttError_t subscriptionStatus = IOT_MQTT_STATUS_PENDING;
	IotMqttSubscription_t* pSubscriptions = (IotMqttSubscription_t*)pvPortMalloc(numTopics * sizeof(IotMqttSubscription_t));
	int i;

	/* Set up the subscription parameters */
	for (i = 0; i < numTopics; i++)
	{
		pSubscriptions[i].qos = IOT_MQTT_QOS_1;
		pSubscriptions[i].pTopicFilter = pTopicsList[i];
		pSubscriptions[i].topicFilterLength = ((uint16_t)strlen(pTopicsList[i]));
		pSubscriptions[i].callback.function = callbackFunc;
		pSubscriptions[i].callback.pCallbackContext = pCallbackParameter;
	}

	/* Subscribe to topics */
	subscriptionStatus = IotMqtt_TimedSubscribe(mqttConnection, pSubscriptions, numTopics, 0, MQTT_TIMEOUT_MS);

	/* Check the status of SUBSCRIBE. */
	switch (subscriptionStatus)
	{
		case IOT_MQTT_SUCCESS:
			IotLogInfo( "All topic subscriptions accepted" );
			break;

		case IOT_MQTT_SERVER_REFUSED:

			/* Check which subscriptions were rejected before exiting the demo. */
			for( i = 0; i < numTopics; i++ )
			{
				if( IotMqtt_IsSubscribed(mqttConnection,
					pSubscriptions[ i ].pTopicFilter,
					pSubscriptions[ i ].topicFilterLength,
					NULL ) == true )
				{
					IotLogError( "Topic filter %.*s was accepted",
						pSubscriptions[ i ].topicFilterLength,
						pSubscriptions[ i ].pTopicFilter );
				}
				else
				{
					IotLogError( "Fail subscribe. Topic filter %.*s was rejected",
						pSubscriptions[ i ].topicFilterLength,
						pSubscriptions[ i ].pTopicFilter );
				}
			}
			break;

		default:
			IotLogError( "Topic Subscribed Failure:%d", subscriptionStatus );
			break;
	}

	vPortFree(pSubscriptions);

	return subscriptionStatus;
}

/**
 * @brief Send an empty message to the certificate request topic to request a final certificate
 *
 * @param[in] mqttConnection	Current Mqtt connection
 * @param[in] pSemaphore	Semaphore to wait for provisioning process to complete
 *
 * @return 	ESP_OK if successful, -1 if failed
 */
static int32_t _requestFinalCertFromAWS(IotMqttConnection_t mqttConnection, IotSemaphore_t* pSemaphore) {
	IotMqttPublishInfo_t publishInfo = IOT_MQTT_PUBLISH_INFO_INITIALIZER;
	/* Set the common members of the publish info. */
	publishInfo.qos = IOT_MQTT_QOS_1;
	publishInfo.topicNameLength = sizeof(CERT_CREATE_REQUEST_AWS_TOPIC_NAME) - 1;
	publishInfo.pPayload = "";
	publishInfo.retryMs = PUBLISH_RETRY_MS;
	publishInfo.retryLimit = PUBLISH_RETRY_LIMIT;
	publishInfo.pTopicName = CERT_CREATE_REQUEST_AWS_TOPIC_NAME;
	publishInfo.payloadLength = 0;

	IotLogInfo( "Sending request to AWS for final credentials" );
	IotMqtt_Publish( mqttConnection,
		&publishInfo,
		0,
		NULL,
		NULL );

	if( IotSemaphore_TimedWait( pSemaphore, MQTT_TIMEOUT_MS ) == false )
	{
		IotLogError( "Failed. (Semaphore Timed Out Waiting for Final Cert)" );
		return -1;
	}

	return ESP_OK;
}

/**
 * @brief Set the claim credentials to the PKCS11 object
 *
 * @return ESP_OK upon successful credential setup.
 * Otherwise, a positive PKCS #11 error code.
 */
int32_t _setClaimCredsToPKCS11Object(void)
{
	esp_err_t err = ESP_OK;
	uint32_t size;
	err = NVS_Get_Size_Of(NVS_CLAIM_CERT, &size);

	// Determine if an object is already set to the PKCS11 certificate. If object is already set, then the claim credentials are already in place and no need to rewrite them
	if(err != ESP_OK){

		err = credentialUtility_decryptCredentials();

		if(err != ESP_OK){
			IotLogError( "Failed Credential decryption" );
			return -1;
		}

		ProvisioningParams_t xParams;

		xParams.pucClientCertificate = plaintextClaimCert;
		xParams.ulClientCertificateLength = claimCertLength;

		xParams.pucClientPrivateKey = plaintextClaimPrivKey;
		xParams.ulClientPrivateKeyLength = claimPrivKeyLength;

		err = setPKCS11CredObjectParams(&xParams);

		/* The cert/key should be set to nvs memory after they PKCS11 objects are set.
		 This is because the fleet provisioning call checks for the existence of credentials in nvs to confirm they are set.
		 We want to avoid the scenario where are the credentials are set in nvs but fail to get saved as a PKCS11 object */
		if(err == ESP_OK){
			err = NVS_Set(NVS_CLAIM_PRIVATE_KEY, plaintextClaimPrivKey, &claimPrivKeyLength);
		}
		if(err == ESP_OK){
			err = NVS_Set(NVS_CLAIM_CERT, plaintextClaimCert, &claimCertLength);
		}
	}

	return err;
}

/**
 * @brief Request final certificate and private key from aws
 *
* @param[in] pNetworkServerInfo Passed to the MQTT connect function when
* establishing the MQTT connection.
* @param[in] pNetworkCredentialInfo Passed to the MQTT connect function when
* establishing the MQTT connection.
* @param[in] pNetworkInterface The network interface to use for this demo.
 *
 * @return 	ESP_OK if successful, error otherwise
 */
static int32_t _getFinalCertsFromAWS(void* pNetworkServerInfo,
										void* pNetworkCredentialInfo,
										const IotNetworkInterface_t* pNetworkInterface)
{
	esp_err_t err;

	/* Semaphore used while waiting for fleet provisioning received messages*/
	IotSemaphore_t fleetProvisioningReceived;

	// Subscribe to fleet provisioning accepted/rejected topics
	const char* pFleetProvReturnTopics[ FLEET_PROVIS_SUBSCRIBE_TOPIC_CNT ] =
	{
		pProvisionRequestAcceptedTopic,
		pProvisionRequestRejectedTopic,
	};


	if( pProvisionRequestTopic == NULL )
	{
		IotLogError( "Error, provisioning topic not set. Need to call init to set." );
		return ESP_FAIL;
	}

	/*MQTT connection for final certificate request*/
	IotMqttConnection_t mqttConnection = IOT_MQTT_CONNECTION_INITIALIZER;

	/* Establish a new MQTT connection. */
	err = _establishMqttConnection(pNetworkServerInfo,
		pNetworkCredentialInfo,
		pNetworkInterface,
		&mqttConnection);

	if( err != ESP_OK )
	{
		IotLogError( "FAILED to establish MQTT connection" );
		return err;
	}

	// Subscribe to certificate created accepted/rejected topics
	const char* pCertCreatedReturnTopics[CERT_CREATED_SUBSCRIBE_TOPIC_CNT] =
	{
		CERT_CREATE_RETURN_TOPIC_ACCEPTED,
		CERT_CREATE_RETURN_TOPIC_REJECTED,
	};

	err = _subscribeTopics(mqttConnection, pCertCreatedReturnTopics, CERT_CREATED_SUBSCRIBE_TOPIC_CNT, _certificateCreateSubscriptionCallback, NULL);
	if( ESP_OK == err )
	{
		err = _subscribeTopics( mqttConnection, pFleetProvReturnTopics, FLEET_PROVIS_SUBSCRIBE_TOPIC_CNT, _fleetProvSubscriptionCallback, &fleetProvisioningReceived );
	}

	if( ESP_OK == err )
	{
		if( IotSemaphore_Create( &fleetProvisioningReceived, 0, 1024 ) == true )
		{
			err = _requestFinalCertFromAWS(mqttConnection, &fleetProvisioningReceived);
			IotSemaphore_Destroy( &fleetProvisioningReceived );
		}
		else {
			IotLogError( "Failed to create semaphore" );
			err = ESP_FAIL;
		}
	}

	if( ESP_OK == err )
	{
		err = _unsubscribeTopics(mqttConnection, pCertCreatedReturnTopics, CERT_CREATED_SUBSCRIBE_TOPIC_CNT);
	}

	if( ESP_OK == err )
	{
		err = _unsubscribeTopics(mqttConnection, pFleetProvReturnTopics, FLEET_PROVIS_SUBSCRIBE_TOPIC_CNT);
	}
	
	// For debug
	if( err != ESP_OK )
	{
		IotLogError("_getFinalCertsFromAWS: err = %d, previously no MQTT Disconnect request", err );
	}

	IotLogInfo( "MQTT Disconnected" );
	IotMqtt_Disconnect(mqttConnection, 0);

	return err;
}

/**
 * @brief Initialize the PKCS11 objects with the final credentials. If the final credentials
 * are not yet stored in nvm then they will be requested from AWS using the claim credentials
 *
 * @param[in] pNetworkServerInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkInterface The network interface to use for this demo.
 * @param[in] pNetworkInterface The network interface to use for this demo.
 *
 * @return ESP_OK if successful, error otherwise
 */
int32_t _getSetCredentials(void * pNetworkServerInfo, void* pCredentials, const IotNetworkInterface_t * pNetworkInterface)
{
	esp_err_t err;
	uint32_t size;

	//Check for Final Certificate
	IotLogInfo( "Checking for Final Credentials" );
	err = NVS_Get_Size_Of( NVS_FINAL_CERT, &size );
	if (err == ESP_OK)
	{
		IotLogInfo( "Found Final Credentials" );
		err = _updateTCPIPCredsWithFinalParamsFromNVS();
	}
	else {
		IotLogInfo( "Final credentials not set" );
		IotLogInfo( "Setting claim credentials to PKCS11 Object" );
		err = _setClaimCredsToPKCS11Object();  // Check if any PKCS object is set. Only write to PKCS object if certificate is empty
		if (err != ESP_OK) {
			IotLogError( "CRITICAL ERROR: Could not write claim credentials from program flash to nvs" );
		}

		if(err == ESP_OK){
			IotLogInfo( "Requesting final credentials from AWS" );
			err = _getFinalCertsFromAWS(pNetworkServerInfo, pCredentials, pNetworkInterface);
			if (err != ESP_OK) {
				IotLogError( "CRITICAL ERROR: Failed to get final credentials from AWS" );
			}
		}
	}

	_fleetProvCleanup();

	return err;
}

/**
 * @brief Fleet provisioning task. This task will determing if the claim/final credentials are set and
 * request them from AWS if they are not set. This task should maintain a high priority. The task deletes
 * itself once it has completed setting the credentials.
 *
 * @param[in] pArgs		Semaphore that will be posted to once the provisioning completes.
 *
 */
void _fleetProvTask( void * pArgs)
{
	esp_err_t err = ESP_OK;
	IotSemaphore_t * completeSemaphore = (IotSemaphore_t *) pArgs;

	_fleetProvStatus = eFLEETPROV_IN_PROCESS;

	if(_fleetProvParams.pConnectionParams != NULL && _fleetProvParams.pCredentials != NULL && _fleetProvParams.pNetworkInterface != NULL)
	{
		err = _getSetCredentials(_fleetProvParams.pConnectionParams, _fleetProvParams.pCredentials, _fleetProvParams.pNetworkInterface);
	}
	else{
		IotLogError(" Error: A fleet provisioning parameter is set to NULL ");
		err = ESP_FAIL;
	}

	if(err == ESP_OK){
		_fleetProvStatus = eFLEETPROV_COMPLETED_SUCCESS;
		// Post to the semaphore indicating the fleet provisioning has been completed
		IotSemaphore_Post(completeSemaphore);
		IotLogInfo( "Fleet Prov Completed Successfully " );
	}
	else{
		_fleetProvStatus = eFLEETPROV_COMPLETED_FAILED;
		IotLogError("Fleet Prov Failed ");
	}

	// Delete the task once credentials have been proisionined
	IotLogInfo( " Deleting Fleet Provisioning Task " );
	vTaskDelete(NULL);

	for(;;)
	{
		// Delay task for 10s.
		vTaskDelay( 10000 / portTICK_PERIOD_MS );
	}

}

/**
 * @brief Clear out the final credentials from nvs. On subsequent boot the fleet provisioning
 * will request new credentials from AWS
 */
void fleetProv_ClearFinalCredentials(void)
{
	NVS_EraseKey(NVS_CLAIM_CERT);
	NVS_EraseKey(NVS_CLAIM_PRIVATE_KEY);
	NVS_EraseKey(NVS_FINAL_CERT);
	NVS_EraseKey(NVS_FINAL_PRIVATE_KEY);
}

/**
 * @brief Return the status of the fleet provisioning.
 *
 * @return eFleetProv_Status_t type indicating the status
 */
int32_t fleetProv_GetStatus( void )
{
	return _fleetProvStatus;
}

/**
 * @brief Initialize the fleet provisioning task. This function creates a new fleet provisioning task which
 * set the PKCS11 credentials of the ESP module. The task will request credentials from AWS if no final
 * credentials exist. If final credentials are not set, have a Wifi connection before calling this task.
 *
 * @param[in] pfleetProvInitParams		Initialization parameters for fleet provisioning
 * @param[in] pSemaphore				Semaphore that will be posted when the fleet provisioning completes
 *
 * @return ESP_OK if successful, error otherwise
 */
int32_t fleetProv_Init(fleetProv_InitParams_t * pfleetProvInitParams, IotSemaphore_t* pSemaphore)
{
	esp_err_t err = ESP_OK;

	// Store the initialization parameters so the external context does not need to exist when the task executes
	_fleetProvParams.pConnectionParams = pfleetProvInitParams->pConnectionParams;
	_fleetProvParams.pCredentials = pfleetProvInitParams->pCredentials;
	_fleetProvParams.pNetworkInterface = pfleetProvInitParams->pNetworkInterface;

	// Set the template name
	if(pfleetProvInitParams->pProvTemplateName != NULL){
		pTemplateName = (char*) pfleetProvInitParams->pProvTemplateName;
	}
	else{
		err = ESP_FAIL;
	}
	// Set the request, accepted, and rejected topics based on the fleet provisioning input template
	if(err == ESP_OK){
		pProvisionRequestTopic = _replaceWildcardAppend(PROVISION_TOPIC_STUCTURE, pTemplateName, NULL);
		pProvisionRequestAcceptedTopic = _replaceWildcardAppend(PROVISION_TOPIC_STUCTURE, pTemplateName, "/accepted");
		pProvisionRequestRejectedTopic = _replaceWildcardAppend(PROVISION_TOPIC_STUCTURE, pTemplateName, "/rejected");
	}

	// Create the fleet provisioning task
	if(err == ESP_OK){
		xTaskCreate(_fleetProvTask, "fleetProv", FLEET_PROV_STACK_SIZE, (void *) pSemaphore, FLEET_PROV_TASK_PRIORITY, &_xFleetProvTaskHandle);
	}

	if(NULL == _xFleetProvTaskHandle){
		err = ESP_FAIL;
		IotLogError( "Error Creating Fleet Provisioning Task" );
	}
	else{
		IotLogInfo("Fleet Prov task created");
	}

	return err;
}
