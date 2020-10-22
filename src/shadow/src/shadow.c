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
 * @brief	Minimum Shadow Document size to process
 */
#define MIN_UPDATE_LEN 5

/**
 * @brief The timeout for Shadow and MQTT operations.
 */
#define TIMEOUT_MS            ( 5000 )

#define MAX_SHADOW_SIZE			4096

typedef struct
{
    /* Allows the Shadow update function to wait for the delta callback to complete
     * a state change before continuing. */
    IotSemaphore_t 			deltaSemaphore;

    bool					deltaSemaphoreCreated;

    IotMqttConnection_t		mqttConnection;
    const char * 			pThingName;							/**< Thing Name pointer */
    size_t					thingNameLength;    				/**< Length of Shadow Thing Name. */

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
 * @brief	Store Shadow Item value in associated NVS storage
 *
 * @param[in] pItem			Pointer to Shadow Item
 *
 */
void _storeInNvs( _shadowItem_t * pItem )
{
	uint8_t bValue;

	IotLogInfo( "_storeInNvs: %s", pItem->key );

	switch( pItem->jType )
	{
		case JSON_STRING:
			NVS_Set( pItem->nvsItem, pItem->jValue.string, 0 );
			break;

		case JSON_NUMBER:
			IotLogError( "Storing floating-point value in NVS is not supported" );
			break;

		case JSON_INTEGER:
			NVS_Set( pItem->nvsItem, pItem->jValue.integer, 0 );
			break;

		case JSON_UINT32:
			NVS_Set( pItem->nvsItem, pItem->jValue.integerU32, 0 );
			break;

		case JSON_BOOL:
			bValue = ( *pItem->jValue.truefalse ? 1 : 0 );
			NVS_Set( pItem->nvsItem, &bValue, 0 );
			break;

		case JSON_NONE:
		default:
			break;
	}
}

/**
 * @brief	Fetch Shadow Item value from associated NVS storage
 *
 * If Shadow Item does not exist, save the current (default) item value in NVS.
 *
 * @param[in] pItem			Pointer to Shadow Item
 */
void _fetchFromNvs( _shadowItem_t * pItem )
{
	uint8_t bValue;

	IotLogInfo( "_fetchFromNvs: %s", pItem->key );

	switch( pItem->jType )
	{
		case JSON_STRING:
			if( ESP_OK != NVS_Get( pItem->nvsItem, pItem->jValue.string, 0 ) )
			{
				NVS_Set( pItem->nvsItem, pItem->jValue.string, 0 );
			}
			break;

		case JSON_NUMBER:
			IotLogError( "Storing floating-point value in NVS is not supported" );
			break;

		case JSON_INTEGER:
			if( ESP_OK != NVS_Get( pItem->nvsItem, pItem->jValue.integer, 0 ) )
			{
				NVS_Set( pItem->nvsItem, pItem->jValue.integer, 0 );
			}
			break;

		case JSON_UINT32:
			if( ESP_OK != NVS_Get( pItem->nvsItem, pItem->jValue.integerU32, 0 ) )
			{
				NVS_Set( pItem->nvsItem, pItem->jValue.integerU32, 0 );
			}
			break;

		case JSON_BOOL:
			if( ESP_OK != NVS_Get( pItem->nvsItem, &bValue, 0 ) )
			{
				bValue = ( *pItem->jValue.truefalse ? 1 : 0 );
				NVS_Set( pItem->nvsItem, &bValue, 0 );
			}
			else
			{
				*pItem->jValue.truefalse = ( bValue == 0 ) ? false : true;
			}
			break;

		case JSON_NONE:
		default:
			break;
	}
}

/**
 * @brief Update a Shadow Item
 *
 *	Assumes that Item has a Section
 *
 * @param[in] pItem			Pointer to Shadow Item
 */
static char * _formatJsonItem( _shadowItem_t * pItem )
{

	char *itemJSON = NULL;

	switch( pItem->jType )
	{
		case JSON_STRING:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:{%Q:{%Q:%Q}}}}",
					"state",
					"reported",
					pItem->section,
					pItem->key,
					pItem->jValue.string
					);
			break;

		case JSON_NUMBER:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:{%Q:{%Q:%f}}}}",
					"state",
					"reported",
					pItem->section,
					pItem->key,
					*pItem->jValue.number
					);
			break;

		case JSON_INTEGER:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:{%Q:{%Q:%d}}}}",
					"state",
					"reported",
					pItem->section,
					pItem->key,
					*pItem->jValue.integer
					);
			break;

		case JSON_UINT32:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:{%Q:{%Q:%d}}}}",
					"state",
					"reported",
					pItem->section,
					pItem->key,
					*pItem->jValue.integerU32
					);
			break;

		case JSON_BOOL:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:{%Q:{%Q:%B}}}}",
					"state",
					"reported",
					pItem->section,
					pItem->key,
					*pItem->jValue.truefalse
					);
			break;

		case JSON_NONE:
		default:
			break;
	}

	return itemJSON;
}

/**
 * @brief	Create a client token using a timestamp
 *
 * For the freertos shadow service, a client token is required for all shadow updates.
 * The client token must be unique at any given time, but may be reused once the update
 * is completed. A timestamp is used for the client token in this case.
 *
 *  To keep the client token within 6 characters, it is modded by 1000000.
 */
static char * _makeToken( void )
{
	static char tokenBuffer[ 7 ];

	snprintf( tokenBuffer, sizeof( tokenBuffer ), "%06lu", ( long unsigned ) ( IotClock_GetTimeMs() % 1000000 ));
	return tokenBuffer;
}

/**
 * @brief Format Shadow Update
 *
 * Build and send a Shadow reported document, based on the bUpdate flags in the table.
 */
static char * _formatShadowUpdate( void )
{
	int32_t	len;
	char * temp = NULL;
	char * mergeOutput = NULL;
	char * staticShadowJSON = NULL;
    _shadowItem_t *pItem;

    /* Start by creating a client token */
	len = mjson_printf( &mjson_print_dynamic_buf, &staticShadowJSON, "{%Q:%Q}", "clientToken", _makeToken() );

    /* Iterate through Shadow Item List */
    for( pItem = deltaCallbackList; pItem->key != NULL; ++pItem )
    {
    	/* For any item that needs updating */
    	if( pItem->bUpdate )
    	{
			/* Create a new json document for just that item */
    		temp = _formatJsonItem( pItem );

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
 * @brief 	Send Shadow Update Document
 *
 * Input must be a fully formatted Shadow Update JSON document including:
 *
 * { state:
 * 		{ reported:
 * 			{ section1:
 * 				{ characteristic1:Value1,
 * 				  characteristic2:Value2
 * 				},
 * 			{ section2:
 * 				{ characteristic3:Value3,
 * 				  characteristic4:Value4
 * 				},
 * 			  ...
 * 			}
 * 		}
 * 	}
 *
 * @param[in] updateJSON		The shadow update in JSON format. Should not include the "reported" portion
 * @param[in] sizeJSON			Size of JSON string
 * @param[in] pCallbackInfo		Callback info after Shadow Update Complete ACK'd by AWS
 *
 * @return `EXIT_SUCCESS` if all Shadow updates were sent; `EXIT_FAILURE`
 * otherwise.
 */
static int updateReportedShadow(const char * updateJSON,
							int	sizeJSON,
							const AwsIotShadowCallbackInfo_t * pCallbackInfo)
{
	int status = EXIT_SUCCESS;
	AwsIotShadowError_t updateStatus = AWS_IOT_SHADOW_STATUS_PENDING;
	AwsIotShadowDocumentInfo_t updateDocument = AWS_IOT_SHADOW_DOCUMENT_INFO_INITIALIZER;
	/* Only proceed if an mqtt connection has been established */
	if( shadowData.mqttConnection != NULL )
	{

		if( status == EXIT_SUCCESS )
		{
			/* Set the common members of the Shadow update document info. */
			updateDocument.pThingName = shadowData.pThingName;
			updateDocument.thingNameLength = shadowData.thingNameLength;
			updateDocument.u.update.pUpdateDocument = updateJSON;
			updateDocument.u.update.updateDocumentLength = sizeJSON;

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
    _shadowItem_t *pDeltaItem;

    char * updateDocument;

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
    	IotLogInfo( "_shadowDeltaCallback: matchstr = %s", matchstr );

		switch( pDeltaItem->jType )
		{
			case JSON_STRING:
				result = mjson_get_string( pCallbackParam->u.callback.pDocument,
							pCallbackParam->u.callback.documentLength,
							matchstr,
							outbuf,
							sizeof( outbuf ));
				if( result != -1 )
				{
					IotLogInfo( "Found %s = %s", matchstr, outbuf );
					pDeltaItem->bUpdate = true;
					deltaFound = true;
				}
				break;

			case JSON_NUMBER:
			case JSON_INTEGER:
			case JSON_UINT32:
				result = mjson_get_number( pCallbackParam->u.callback.pDocument,
							pCallbackParam->u.callback.documentLength,
							matchstr,
							&value );
				if( result != 0 )
				{
					IotLogInfo( "Found %s = %f", matchstr, value );
					if( pDeltaItem->jType == JSON_NUMBER )
					{
						*pDeltaItem->jValue.number = value;
					}
					else if( pDeltaItem->jType == JSON_INTEGER )
					{
						*pDeltaItem->jValue.integer = ( int16_t )value;
					}
					else
					{
						*pDeltaItem->jValue.integerU32 = ( uint32_t )value;
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
				if( result != 0 )
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
 * @brief Shadow updated callback, invoked when the Shadow document changes.
 *
 * This function reports when a Shadow has been updated. It is used to verify that
 * reported state updates were accepted.
 *
 * The received document is parsed and <i>current:state:reported</i> contents compared with local values.
 * <i>bUpdate</i> flag is cleared for matching values.
 *
 * @param[in] pCallbackContext Not used.
 * @param[in] pCallbackParam The received Shadow updated document.
 */
static void _shadowUpdatedCallback( void * pCallbackContext,
                                    AwsIotShadowCallbackParam_t * pCallbackParam )
{
	char matchstr[ 64 ];
	char outbuf[ 30 ];
	int result;
	double value;
	int ivalue;
	bool	bUpdateComplete;

    /* Silence warnings about unused parameters. */
    ( void ) pCallbackContext;

    _shadowItem_t *pItem;

    /* Debug - print the update document */
//	printf( "_shadowUpdatedCallback: doc[%d] = %.*s\n",
//			pCallbackParam->u.callback.documentLength,
//			pCallbackParam->u.callback.documentLength,
//			pCallbackParam->u.callback.pDocument );

	/* Don't try to process the document if it is very small */
	if( pCallbackParam->u.callback.documentLength > MIN_UPDATE_LEN )
	{
		/* Iterate through deltaCallbacklist */
		for( pItem = deltaCallbackList; pItem->key != NULL; ++pItem )
		{
			bUpdateComplete = false;

			/* assemble a match string */
			snprintf( matchstr, sizeof( matchstr ), "$.current.state.reported.%s.%s", pItem->section, pItem->key );
			IotLogDebug( "_shadowUpdatedCallback: matchstr = %s", matchstr );

			switch( pItem->jType )
			{
				case JSON_STRING:
					result = mjson_get_string( pCallbackParam->u.callback.pDocument,
								pCallbackParam->u.callback.documentLength,
								matchstr,
								outbuf,
								sizeof( outbuf ));
					if( result != -1 )
					{
						/* If values match, cancel the update flag */
//						if( ( 0 == strcmp( pItem->jValue.string, outbuf ) ) && pItem->bUpdate )
						if( pItem->bUpdate  && (pItem->jValue.string != NULL ) && ( 0 == strcmp( pItem->jValue.string, outbuf ) ) )
						{
							IotLogInfo( "Found %s = %s", matchstr, outbuf );
							bUpdateComplete = true;
						}
					}
					break;

				case JSON_NUMBER:
				case JSON_INTEGER:
				case JSON_UINT32:
					result = mjson_get_number( pCallbackParam->u.callback.pDocument,
								pCallbackParam->u.callback.documentLength,
								matchstr,
								&value );
					if( result != 0 )
					{
						if( pItem->jType == JSON_NUMBER )
						{
							/* If values match, cancel the update flag */
							if( ( *pItem->jValue.number == value ) && pItem->bUpdate )
							{
								IotLogInfo( "Found %s = %f", matchstr, value );
								bUpdateComplete = true;
							}
						}
						else if( pItem->jType == JSON_INTEGER )
						{
							/* If values match, cancel the update flag */
							if( ( *pItem->jValue.integer == ( int16_t )value ) && pItem->bUpdate )
							{
								IotLogInfo( "Found %s = %d", matchstr, ( int16_t )value );
								bUpdateComplete = true;
							}
						}
						else
						{
							/* If values match, cancel the update flag */
							if( ( *pItem->jValue.integerU32 == ( uint32_t )value ) && pItem->bUpdate )
							{
								IotLogInfo( "Found %s = %d", matchstr, ( uint32_t )value );
								bUpdateComplete = true;
							}
						}
					}
					break;

				case JSON_BOOL:
					result = mjson_get_bool( pCallbackParam->u.callback.pDocument,
								pCallbackParam->u.callback.documentLength,
								matchstr,
								&ivalue );
					if( result != 0 )
					{
						/* If values match, cancel the update flag */
						if( ( *pItem->jValue.truefalse == ( ivalue ? true : false ) ) && pItem->bUpdate )
						{
							IotLogInfo( "Found %s = %d", matchstr, ivalue );
							bUpdateComplete = true;
						}
					}
					break;

				case JSON_NONE:
				default:
					break;

			}

			/* If update for this Item completed */
			if( bUpdateComplete )
			{
				/* Clear update flag */
				pItem->bUpdate = false;

				/* Store value in NVS */
				if( pItem->nvsItem != -1 )
				{
					_storeInNvs( pItem );
				}
				/* Call UpdateCompleteCallback handler, if present */
				if( pItem->handler != NULL )
				{
					IotLogInfo( "shadow update[%s]:handler", pItem->key );
					pItem->handler( pItem );
				}
			}
		}
	}
}

/* ************************************************************************* */
/* ************************************************************************* */
/* **********        I N T E R F A C E   F U N C T I O N S        ********** */
/* ************************************************************************* */
/* ************************************************************************* */

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
    AwsIotShadowCallbackInfo_t updatedCallback = AWS_IOT_SHADOW_CALLBACK_INFO_INITIALIZER;

    /* Set the functions for callbacks. */
    deltaCallback.pCallbackContext = pDeltaSemaphore;
    deltaCallback.function = _shadowDeltaCallback;
    updatedCallback.function = _shadowUpdatedCallback;

    /* Set the delta callback, which notifies of different desired and reported
     * Shadow states. */
    callbackStatus = AwsIotShadow_SetDeltaCallback( mqttConnection,
                                                    pThingName,
                                                    thingNameLength,
                                                    0,
                                                    &deltaCallback );

    if( callbackStatus == AWS_IOT_SHADOW_SUCCESS )
    {
        /* Set the updated callback, which notifies when a Shadow document is
         * changed. */
        callbackStatus = AwsIotShadow_SetUpdatedCallback( mqttConnection,
                                                          pThingName,
                                                          thingNameLength,
                                                          0,
                                                          &updatedCallback );
    }

    if( callbackStatus != AWS_IOT_SHADOW_SUCCESS )
    {
        IotLogError( "Failed to set shadow module callback, error %s.",
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
    int status = EXIT_SUCCESS;

    /* Validate and determine the length of the Thing Name. */
    if( pThingName != NULL )
    {
    	shadowData.pThingName = pThingName;
    	shadowData.thingNameLength = strlen( shadowData.pThingName );

        if( shadowData.thingNameLength == 0 )
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
    {
    	shadowData.deltaSemaphoreCreated = IotSemaphore_Create( &shadowData.deltaSemaphore, 0, 1 );
    }

    if( shadowData.deltaSemaphoreCreated == false )
    {
        status = EXIT_FAILURE;
    }

    /* Set the Shadow callbacks */
    if( status == EXIT_SUCCESS )
    {
        status = _setShadowCallbacks( &shadowData.deltaSemaphore, mqttConnection, shadowData.pThingName, shadowData.thingNameLength );
    }

    return status;
}


void shadow_updateReported( void )
{
	char *updateDocument;

	/* Only proceed if an mqtt connection has been established, otherwise the bUpdate flags will get cleared */
	if( shadowData.mqttConnection != NULL )
	{
		updateDocument = _formatShadowUpdate();
		IotLogInfo( "Update Document = %s", updateDocument );

		/* Update shadow */
		updateReportedShadow( updateDocument, strlen( updateDocument), NULL );
		free( updateDocument );
	}
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
	_shadowItem_t *pItem;

	IotLogInfo( "Registering Delta Callback List" );
	deltaCallbackList = pShadowDeltaList;

	/* Iterate through deltaCallbacklist fetching values from NVS */
	for( pItem = deltaCallbackList; pItem->key != NULL; ++pItem )
	{
		if( pItem->nvsItem != -1 )
		{
			_fetchFromNvs( pItem );
			pItem->bUpdate = true;
		}
	}
}
