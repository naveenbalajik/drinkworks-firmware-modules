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
#include "platform/iot_threads.h"

/* NVS includes */
#include "nvs_utility.h"

/* Mqtt includes */
#include "mqtt.h"

/* JSON utilities include. */
#include "iot_json_utils.h"
#include "mjson.h"

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

typedef struct
{
    /* Allows the Shadow update function to wait for the delta callback to complete
     * a state change before continuing. */
    IotSemaphore_t deltaSemaphore;

    bool		deltaSemaphoreCreated;

    IotMqttConnection_t mqttConnection;

} shadowData_t;

static shadowData_t shadowData =
{
	.deltaSemaphoreCreated = false,
	.mqttConnection = NULL,
};

static _shadowItem_t *deltaCallbackList = NULL;

/**
 * @brief Flag to ensure shadow library is only initialized once
 */
static bool _shadowInitialized = false;

/**
 * @brief Parses a key in the "state" section of a Shadow delta document.
 *
 * @param[in] pDeltaDocument The Shadow delta document to parse.
 * @param[in] deltaDocumentLength The length of `pDeltaDocument`.
 * @param[in] pDeltaKey The key in the delta document to find. Must be NULL-terminated.
 * @param[out] pDelta Set to the first character in the delta key.
 * @param[out] pDeltaLength The length of the delta key.
 *
 * @return `true` if the given delta key is found; `false` otherwise.
 */
static bool _getDelta( const char * pDeltaDocument,
                       size_t deltaDocumentLength,
					   const char * pDeltaSection,
                       const char * pDeltaKey,
                       const char ** pDelta,
                       size_t * pDeltaLength )
{
    bool stateFound = false;
    bool deltaFound = false;
    bool sectionFound = false;
    const size_t deltaSectionLength = strlen( pDeltaSection );
    const size_t deltaKeyLength = strlen( pDeltaKey );
    const char * pState = NULL;
    size_t stateLength = 0;
    const char * pSection = NULL;
    size_t sectionLength = 0;

    /* Find the "state" key in the delta document. */
    stateFound = IotJsonUtils_FindJsonValue( pDeltaDocument,
                                             deltaDocumentLength,
                                             "state",
                                             5,
                                             &pState,
                                             &stateLength );

    if( stateFound == true )
    {
    	/* if a Delta Section is present */
    	if( pDeltaSection != NULL )
    	{
            /* Find the delta section within the "state" section. */
            sectionFound = IotJsonUtils_FindJsonValue( pState,
                                                     stateLength,
                                                     pDeltaSection,
                                                     deltaSectionLength,
                                                     &pSection,
                                                     &sectionLength );
            if( sectionFound == true )
            {
                /* Find the delta key within the delta section. */
                deltaFound = IotJsonUtils_FindJsonValue( pSection,
                                                         sectionLength,
                                                         pDeltaKey,
                                                         deltaKeyLength,
                                                         pDelta,
                                                         pDeltaLength );

            }

    	}
    	else
    	{
        /* Find the delta key within the "state" section. */
        deltaFound = IotJsonUtils_FindJsonValue( pState,
                                                 stateLength,
                                                 pDeltaKey,
                                                 deltaKeyLength,
                                                 pDelta,
                                                 pDeltaLength );
    	}
    }
    else
    {
        IotLogWarn( "Failed to find \"state\" in Shadow delta document." );
    }

    return deltaFound;
}

/**
 * @brief Update a Shadow Item
 *
 * @param[in] pItem			Pointer to Shadow Item
 */
static char * _formatJsonItem( _shadowItem_t * pItem )
{

	int32_t	itemLen = 0;
//	int32_t	sectionLen = 0;
//	int32_t	mergeLen = 0;
	char *itemJSON = NULL;
//	char *sectionJSON = NULL;
//	char *mergeOutput = NULL;

	switch( pItem->jType )
	{
		case JSON_STRING:
			itemLen = mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:%Q}}",
					pItem->section,
					pItem->key,
					pItem->jValue.string
					);
			break;

		case JSON_NUMBER:
			itemLen = mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:%f}}",
					pItem->section,
					pItem->key,
					*pItem->jValue.number
					);
			break;

		case JSON_INTEGER:
			itemLen = mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:%d}}",
					pItem->section,
					pItem->key,
					*pItem->jValue.integer
					);
			break;

		case JSON_BOOL:
			itemLen = mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:%B}}",
					pItem->section,
					pItem->key,
					*pItem->jValue.truefalse
					);
			break;

		case JSON_NONE:
		default:
			break;
	}

	/* Is a section specified for this item */
//	if( pItem->section != NULL )
//	{
//		sectionLen = mjson_printf( &mjson_print_dynamic_buf, &sectionJSON, "{%Q:}", pItem->section );
//		mergeLen = mjson_merge(sectionJSON, sectionLen, itemJSON, itemLen, mjson_print_dynamic_buf, &mergeOutput);
//
//		free( itemJSON );
//		free (sectionJSON );
//		itemJSON = mergeOutput;
//		itemLen = mergeLen;
//	}


	return itemJSON;
}

/**
 * @brief Format Shadow Update
 *
 * Build and send a Shadow reported document, based on the bUpdate flags in the table.
 */
static char * _formatShadowUpdate( void )
{
	int32_t	len = 2;
	char * temp = NULL;
//	int32_t tempLen = 0;
	char * mergeOutput = NULL;
	char * staticShadowJSON = (char *) malloc(sizeof("{}"));
	strcpy(staticShadowJSON, "{}");
    _shadowItem_t *pDeltaItem;

    /* Iterate through Shadow Item List */
    for( pDeltaItem = deltaCallbackList; pDeltaItem->key != NULL; ++pDeltaItem )
    {
    	/* For any item that needs updating */
    	if( pDeltaItem->bUpdate )
    	{
    		/* Create a new json document for just that item */
    		temp = _formatJsonItem( pDeltaItem );

      		/* Merge the new Item Document with the static shadow JSON. Output to mergeOutput */
    		len = mjson_merge(staticShadowJSON, len, temp, strlen( temp ), mjson_print_dynamic_buf, &mergeOutput);

    		/* Point the staticShadowJSON to the merged output and free the merged output and the temp key pair */
    		free( staticShadowJSON );
    		staticShadowJSON = mergeOutput;
    		mergeOutput = NULL;
    		free( temp );
    		temp = NULL;
    	}
    }
    return staticShadowJSON;
}

/**
 * @brief Shadow delta callback, invoked when the desired and updates Shadow
 * states differ.
 *
 * This function uses the registered DeltaCallback List to process the received Delta document.
 *
 * @param[in] pCallbackContext Not used.
 * @param[in] pCallbackParam The received Shadow delta document.
 */
static void _shadowDeltaCallback( void * pCallbackContext,
                                  AwsIotShadowCallbackParam_t * pCallbackParam )
{
    bool deltaFound = false;
//    const char * pDelta = NULL;
//    size_t deltaLength = 0;
//    IotSemaphore_t * pDeltaSemaphore = pCallbackContext;
//    int updateDocumentLength = 0;
//    AwsIotShadowError_t updateStatus = AWS_IOT_SHADOW_STATUS_PENDING;
//    AwsIotShadowDocumentInfo_t updateDocument = AWS_IOT_SHADOW_DOCUMENT_INFO_INITIALIZER;
    _shadowItem_t *pDeltaItem;

    char * updateDocument;
    /* Stored state. */
//    static int32_t currentState = 0;

    /* A buffer containing the update document. It has static duration to prevent
     * it from being placed on the call stack. */
//    static char pUpdateDocument[ EXPECTED_REPORTED_JSON_SIZE + 1 ] = { 0 };

    /* Iterate through deltaCallbacklist */
    for( pDeltaItem = deltaCallbackList; pDeltaItem->key != NULL; ++pDeltaItem )
    {
    	char matchstr[ 48 ];
    	char outbuf[ 30 ];
    	int result;
    	double value;
    	int ivalue;

    	/* assemble a match string */
    	if( pDeltaItem->section == NULL )
    	{
    		snprintf( matchstr, sizeof( matchstr ), "$.state.%s", pDeltaItem->key );
    	}
    	else
    	{
    		snprintf( matchstr, sizeof( matchstr ), "$.state.%s.%s", pDeltaItem->section, pDeltaItem->key );
    	}
    	IotLogInfo( "matchstr = %s", matchstr );

		switch( pDeltaItem->jType )
		{
			case JSON_STRING:
				result = mjson_get_string( pCallbackParam->u.callback.pDocument,
							pCallbackParam->u.callback.documentLength,
							matchstr,
							outbuf,
							sizeof( outbuf ));
				if( result == -1 )
				{
					IotLogInfo( "Did not find: %s", matchstr );
				}
				else
				{
					IotLogInfo( "Found %s = %s", matchstr, outbuf );
					pDeltaItem->bUpdate = true;
					deltaFound = true;
				}
				break;

			case JSON_NUMBER:
			case JSON_INTEGER:
				result = mjson_get_number( pCallbackParam->u.callback.pDocument,
							pCallbackParam->u.callback.documentLength,
							matchstr,
							&value );
				if( result == 0 )
				{
					IotLogInfo( "Did not find: %s", matchstr );
				}
				else
				{
					IotLogInfo( "Found %s = %f", matchstr, value );
					if( pDeltaItem->jType == JSON_NUMBER )
					{
						*pDeltaItem->jValue.number = value;
					}
					else
					{
						*pDeltaItem->jValue.integer = ( int16_t )value;
					}
					pDeltaItem->bUpdate = true;
					deltaFound = true;
				}
				break;

			case JSON_BOOL:
				result = mjson_get_bool( pCallbackParam->u.callback.pDocument,
							pCallbackParam->u.callback.documentLength,
							matchstr,
							&ivalue );
				if( result == 0 )
				{
					IotLogInfo( "Did not find: %s", matchstr );
				}
				else
				{
					IotLogInfo( "Found %s = %d", matchstr, ivalue );
					*pDeltaItem->jValue.truefalse = ivalue ? true : false;
					pDeltaItem->bUpdate = true;
					deltaFound = true;
				}
				break;

			case JSON_NONE:
			default:
				break;

		}

#ifdef	USE_CALLBACK
		/* Check if there is a different state in the Shadow for the current item. */
		deltaFound = _getDelta( pCallbackParam->u.callback.pDocument,
								pCallbackParam->u.callback.documentLength,
								pDeltaItem->section,
								pDeltaItem->key,
								&pDelta,
								&deltaLength );

		/* If difference found */
		if( deltaFound == true )
		{
			/* And a callback handler is present */
			if( pDeltaItem->handler != NULL )
			{
				/* call the handler */
				pDeltaItem->handler( pDeltaItem, ( const uint8_t * ) pDelta, deltaLength );
			}
#ifdef	DEPRECIATED
			/* Change the current state based on the value in the delta document. */
			if( *pDelta == '0' )
			{
				IotLogInfo( "%.*s changing state from %d to 0.",
							pCallbackParam->thingNameLength,
							pCallbackParam->pThingName,
							currentState );

				currentState = 0;
			}
			else if( *pDelta == '1' )
			{
				IotLogInfo( "%.*s changing state from %d to 1.",
							pCallbackParam->thingNameLength,
							pCallbackParam->pThingName,
							currentState );

				currentState = 1;
			}
			else
			{
				IotLogWarn( "Unknown powerOn state parsed from delta document." );
			}

			/* Set the common members to report the new state. */
			updateDocument.pThingName = pCallbackParam->pThingName;
			updateDocument.thingNameLength = pCallbackParam->thingNameLength;
			updateDocument.u.update.pUpdateDocument = pUpdateDocument;
			updateDocument.u.update.updateDocumentLength = EXPECTED_REPORTED_JSON_SIZE;

			/* Generate a Shadow document for the reported state. To keep the client
			 * token within 6 characters, it is modded by 1000000. */
			updateDocumentLength = snprintf( pUpdateDocument,
											 EXPECTED_REPORTED_JSON_SIZE + 1,
											 SHADOW_REPORTED_JSON,
											 ( int ) currentState,
											 ( long unsigned ) ( IotClock_GetTimeMs() % 1000000 ) );

			if( ( size_t ) updateDocumentLength != EXPECTED_REPORTED_JSON_SIZE )
			{
				IotLogError( "Failed to generate reported state document for Shadow update." );
			}
			else
			{
				/* Send the Shadow update. Its result is not checked, as the Shadow updated
				 * callback will report if the Shadow was successfully updated. Because the
				 * Shadow is constantly updated in this demo, the "Keep Subscriptions" flag
				 * is passed to this function. */
				updateStatus = AwsIotShadow_Update( pCallbackParam->mqttConnection,
													&updateDocument,
													AWS_IOT_SHADOW_FLAG_KEEP_SUBSCRIPTIONS,
													NULL,
													NULL );

				if( updateStatus != AWS_IOT_SHADOW_STATUS_PENDING )
				{
					IotLogWarn( "%.*s failed to report new state.",
								pCallbackParam->thingNameLength,
								pCallbackParam->pThingName );
				}
				else
				{
					IotLogInfo( "%.*s sent new state report.",
								pCallbackParam->thingNameLength,
								pCallbackParam->pThingName );
				}
			}

		}
		else
		{
			IotLogWarn( "Failed to parse powerOn state from delta document." );
#endif
		}
#endif
    }
    if( deltaFound )
    {
    	updateDocument = _formatShadowUpdate();
    	IotLogInfo( "Update Document = %s", updateDocument );
    	/* Update shadow */
    	updateReportedShadow( updateDocument, strlen( updateDocument), NULL );
    	free( updateDocument );
    }
    /* Post to the delta semaphore to unblock the thread sending Shadow updates. */
//    IotSemaphore_Post( pDeltaSemaphore );
}

/**
 * @brief Set the Shadow callback functions used in this demo.
 *
 * @param[in] pDeltaSemaphore Used to synchronize Shadow updates with the delta
 * callback.
 * @param[in] mqttConnection The MQTT connection used for Shadows.
 * @param[in] pThingName The Thing Name for Shadows in this demo.
 * @param[in] thingNameLength The length of `pThingName`.
 *
 * @return `EXIT_SUCCESS` if all Shadow callbacks were set; `EXIT_FAILURE`
 * otherwise.
 */
static int _setShadowCallbacks( IotSemaphore_t * pDeltaSemaphore,
                                IotMqttConnection_t mqttConnection,
                                const char * pThingName,
                                size_t thingNameLength )
{
    int status = EXIT_SUCCESS;
    AwsIotShadowError_t callbackStatus = AWS_IOT_SHADOW_STATUS_PENDING;
    AwsIotShadowCallbackInfo_t deltaCallback = AWS_IOT_SHADOW_CALLBACK_INFO_INITIALIZER;
//    AwsIotShadowCallbackInfo_t updatedCallback = AWS_IOT_SHADOW_CALLBACK_INFO_INITIALIZER;

    /* Set the functions for callbacks. */
    deltaCallback.pCallbackContext = pDeltaSemaphore;
    deltaCallback.function = _shadowDeltaCallback;
//    updatedCallback.function = _shadowUpdatedCallback;

    /* Set the delta callback, which notifies of different desired and reported
     * Shadow states. */
    callbackStatus = AwsIotShadow_SetDeltaCallback( mqttConnection,
                                                    pThingName,
                                                    thingNameLength,
                                                    0,
                                                    &deltaCallback );

//    if( callbackStatus == AWS_IOT_SHADOW_SUCCESS )
//    {
        /* Set the updated callback, which notifies when a Shadow document is
         * changed. */
//        callbackStatus = AwsIotShadow_SetUpdatedCallback( mqttConnection,
//                                                          pThingName,
//                                                          thingNameLength,
//                                                          0,
//                                                          &updatedCallback );
//    }

    if( callbackStatus != AWS_IOT_SHADOW_SUCCESS )
    {
        IotLogError( "Failed to set demo shadow callback, error %s.",
                     AwsIotShadow_strerror( callbackStatus ) );

        status = EXIT_FAILURE;
    }

    return status;
}

/**
 * @brief	Shadow Connect
 *
 * This function should be called upon establishing an MQTT connection. It will
 * 	+ Create a semaphore
 * 	+ Register callbacks
 *
 * @param[in] mqttConnection The MQTT connection used for Shadows.
 * @param[in] pThingName The Thing Name for Shadows in this demo.
 */
int shadow_connect( IotMqttConnection_t mqttConnection, const char * pThingName )
{
    /* Return value of this function and the exit status of this program. */
    int status = 0;

    size_t thingNameLength = EXIT_SUCCESS;    				/* Length of Shadow Thing Name. */

    /* Validate and determine the length of the Thing Name. */
    if( pThingName != NULL )
    {
        thingNameLength = strlen( pThingName );

        if( thingNameLength == 0 )
        {
            IotLogError( "The length of the Thing Name (identifier) must be nonzero." );
            status = EXIT_FAILURE;
        }
    }
    else
    {
        IotLogError( "A Thing Name (identifier) must be provided for the Shadow demo." );
        status = EXIT_FAILURE;
    }

    /* Save MQTT Connection for future use */
    shadowData.mqttConnection = mqttConnection;

    /* Create the semaphore that synchronizes with the delta callback. */
    if( status == EXIT_SUCCESS )

    shadowData.deltaSemaphoreCreated = IotSemaphore_Create( &shadowData.deltaSemaphore, 0, 1 );

    if( shadowData.deltaSemaphoreCreated == false )
    {
        status = EXIT_FAILURE;
    }

    /* Set the Shadow callbacks */
    if( status == EXIT_SUCCESS )
    {
        status = _setShadowCallbacks( &shadowData.deltaSemaphore, mqttConnection, pThingName, thingNameLength );
    }

    return status;
}

/*-----------------------------------------------------------*/


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

	/* Only proceed if an mqtt connection has been established */
	if( shadowData.mqttConnection != NULL )
	{
		/* A buffer containing the update document. It has static duration to prevent
		 * it from being placed on the call stack. */
		static char pUpdateDocument[ MAX_SHADOW_SIZE ] = { 0 };
		/* Generate shadow document using a timestamp for the client token. To keep the client token within 6 characters, it is modded by 1000000. */
		updateDocumentLength = snprintf(pUpdateDocument, MAX_SHADOW_SIZE, SHADOW_REPORTED_JSON, sizeJSON, updateJSON, ( long unsigned ) ( IotClock_GetTimeMs() % 1000000 ));

		// Get thing name
		/* Buffer to hold ThingName */
		char thingNameBuffer[ MAX_THINGNAME_LEN ] ={0};
		int thingLength = MAX_THINGNAME_LEN;
		if(EXIT_SUCCESS != NVS_Get(NVS_THING_NAME, thingNameBuffer, &thingLength) )
		{
			IotLogError( "ERROR, unable to fetch thing name" );
			status = EXIT_FAILURE;
		}

		/* If the null terminator is included in the thing length, remove it */
		if( thingNameBuffer[thingLength - 1] == 0 )
		{
			thingLength--;
		}

		/* Ensure that we are connected to the AWS server */
	//	IotMqttConnection_t mqttConnection;
	//	if( status == EXIT_SUCCESS )
	//	{
	//		status = mqtt_GetMqtt(&mqttConnection);
	//	}

		if( status == EXIT_SUCCESS )
		{
			/* Set the common members of the Shadow update document info. */
			updateDocument.pThingName = thingNameBuffer;
			updateDocument.thingNameLength = thingLength;
			updateDocument.u.update.pUpdateDocument = pUpdateDocument;
			updateDocument.u.update.updateDocumentLength = updateDocumentLength;

			updateStatus = AwsIotShadow_Update( shadowData.mqttConnection,
													 &updateDocument,
													 0,
													 pCallbackInfo,
													 NULL);
		}
		else
		{
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
	IotLogInfo( "shadow_init" );

	/* Initializing the shadow library using the default MQTT timeout. */
	if( !_shadowInitialized )
	{
		shadowInitStatus = AwsIotShadow_Init( 0 );

		if( shadowInitStatus == AWS_IOT_SHADOW_SUCCESS )
		{
			_shadowInitialized = true;
		}
		else
		{
			IotLogError( "ERROR: Shadow Initialization Failed" );
		}

	}

	return shadowInitStatus;

}

void shadow_InitDeltaCallbacks( _shadowItem_t *pShadowDeltaList )
{
	IotLogInfo( "Registering Delta Callback List" );
	deltaCallbackList = pShadowDeltaList;
}
