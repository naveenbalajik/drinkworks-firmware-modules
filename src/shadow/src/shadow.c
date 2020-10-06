/*
 * shadow.c
 *
 *  Created on: Sep 9, 2020
 *      Author: Nicholas.Weber
 */

/* Shadow include. */
#include "aws_iot_shadow.h"
#include "shadow.h"
#include "shadow_logging.h"

/* Standard includes. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Platform includes. */
#include "platform/iot_clock.h"

/* NVS includes */
#include "nvs_utility.h"

/* Mqtt includes */
#include "mqtt.h"


/* Max thing name length */
#define	MAX_THINGNAME_LEN	128

/**
 * @brief The timeout for Shadow and MQTT operations in this demo.
 */
#define TIMEOUT_MS            ( 5000 )

/**
 * @brief Format string representing a Shadow document with a "reported" state.
 *
 * For the freertos shadow service, a client token is required for all shadow updates.
 * The client token must be unique at any given time, but may be reused once the update
 * is completed. A timestamp is used for the client token in this case.
 */
#define SHADOW_REPORTED_JSON     \
    "{"                          \
    "\"state\":{"                \
    "\"reported\":"              \
    "%.*s"          			 \
    "},"                         \
	"\"clientToken\":\"%06lu\""  \
    "}"

#define MAX_SHADOW_SIZE			4096

/**
 * @brief Flag to ensure shadow library is only initialized once
 */
static bool _shadowInitialized = false;

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
							const AwsIotShadowCallbackInfo_t * pCallbackInfo)
{
	int status = EXIT_SUCCESS;
	int updateDocumentLength = 0;
	AwsIotShadowError_t updateStatus = AWS_IOT_SHADOW_STATUS_PENDING;
	AwsIotShadowDocumentInfo_t updateDocument = AWS_IOT_SHADOW_DOCUMENT_INFO_INITIALIZER;

    /* A buffer containing the update document. It has static duration to prevent
     * it from being placed on the call stack. */
    static char pUpdateDocument[ MAX_SHADOW_SIZE ] = { 0 };
    /* Generate shadow document using a timestamp for the client token. To keep the client token within 6 characters, it is modded by 1000000. */
    updateDocumentLength = snprintf(pUpdateDocument, MAX_SHADOW_SIZE, SHADOW_REPORTED_JSON, sizeJSON, updateJSON, ( long unsigned ) ( IotClock_GetTimeMs() % 1000000 ));

	// Get thing name
	/* Buffer to hold ThingName */
	char thingNameBuffer[ MAX_THINGNAME_LEN ] ={0};
	int thingLength = MAX_THINGNAME_LEN;
	if(EXIT_SUCCESS != NVS_Get(NVS_THING_NAME, thingNameBuffer, &thingLength)){
		IotLogError( "ERROR, unable to fetch thing name" );
		status = EXIT_FAILURE;
	}

	/* If the null terminator is included in the thing length, remove it */
	if(thingNameBuffer[thingLength - 1] == 0){
		thingLength--;
	}

	/* Ensure that we are connected to the AWS server */
	IotMqttConnection_t mqttConnection;
	if(status == EXIT_SUCCESS){
		status = mqtt_GetMqtt(&mqttConnection);
	}

	if(status == EXIT_SUCCESS){
		/* Set the common members of the Shadow update document info. */
		updateDocument.pThingName = thingNameBuffer;
		updateDocument.thingNameLength = thingLength;
	    updateDocument.u.update.pUpdateDocument = pUpdateDocument;
	    updateDocument.u.update.updateDocumentLength = updateDocumentLength;

		updateStatus = AwsIotShadow_Update( mqttConnection,
												 &updateDocument,
												 0,
												 pCallbackInfo,
												 NULL);
	}
	else{
		IotLogError("Error updating shadow. No MQTT Connection");
	}

	if( updateStatus != AWS_IOT_SHADOW_STATUS_PENDING )
	{
		IotLogError( "Failed to send Shadow update. error %s.", AwsIotShadow_strerror( updateStatus ) );
		status = EXIT_FAILURE;
	}
	else
	{
		IotLogInfo( "Sent Shadow update" );
	}

	return status;
}

/**
 * @brief Initialize the Shadow library.
 *
 * @return `EXIT_SUCCESS` if all libraries were successfully initialized;
 * `EXIT_FAILURE` otherwise.
 */
int shadow_init(void)
{
	AwsIotShadowError_t shadowInitStatus = AWS_IOT_SHADOW_SUCCESS;
	/* Initializing the shadow library using the default MQTT timeout. */
	if(!_shadowInitialized){
		shadowInitStatus = AwsIotShadow_Init( 0 );

		if( shadowInitStatus == AWS_IOT_SHADOW_SUCCESS )
		{
			_shadowInitialized = true;
		}
		else{
			IotLogError( "ERROR: Shadow Initialization Failed" );
		}

	}

	return shadowInitStatus;

}
