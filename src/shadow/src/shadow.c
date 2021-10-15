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
#include "json.h"
#include "iot_json_utils.h"
#include "mjson.h"

#include	"TimeSync.h"

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
    bool					deltaSemaphoreCreated;				/**< Flag set when delta semaphore is created */
    IotMqttConnection_t		mqttConnection;						/**< MQTT connection handle */
    const char * 			pThingName;							/**< Thing Name pointer */
    size_t					thingNameLength;    				/**< Length of Shadow Thing Name. */
    _shadowItem_t *			itemList;							/**< Pointer to list of Shadow Items */
	time_t					contextTime;						/**< Time value used as context for Shadow callback */
	bool					bConnected;							/**< Shadow connected status */
	bool					bInitialized;						/**< Flag to ensure shadow library is only initialized once */
	int						numberOfItems;						/**< Number of Shadow Items */

} shadowData_t;

static shadowData_t shadowData =
{
	.deltaSemaphoreCreated = false,
	.mqttConnection = NULL,
	.itemList = NULL,
	.bConnected = false,
};

/**
 * @brief	Store Shadow Item value in associated NVS storage
 *
 * @param[in] pItem			Pointer to Shadow Item
 *
 */
void _storeInNvs( _shadowItem_t * pItem )
{
	uint8_t bValue;

	IotLogInfo( "_storeInNvs: %s", pItem->jItem.key );

	switch( pItem->jItem.jType )
	{
		case JSON_STRING:
			NVS_Set( pItem->nvsItem, pItem->jItem.jValue.string, NULL );
			break;

		case JSON_NUMBER:
			IotLogError( "Storing floating-point value in NVS is not supported" );
			break;

		case JSON_INTEGER:
			NVS_Set( pItem->nvsItem, pItem->jItem.jValue.integer, NULL );
			break;

		case JSON_INT32:
			NVS_Set( pItem->nvsItem, pItem->jItem.jValue.integer32, NULL );
			break;

		case JSON_UINT32:
			NVS_Set( pItem->nvsItem, pItem->jItem.jValue.integerU32, NULL );
			break;

		case JSON_BOOL:
			bValue = ( *pItem->jItem.jValue.truefalse ? 1 : 0 );
			NVS_Set( pItem->nvsItem, &bValue, NULL );
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
static void _fetchFromNvs( _shadowItem_t * pItem )
{
	uint8_t bValue;
	size_t size;
	esp_err_t err;
	char * currentVal;

	IotLogInfo( "_fetchFromNvs: %s", pItem->jItem.key );

	switch( pItem->jItem.jType )
	{
		case JSON_STRING:
			/* Get the size of item in NVS */
			err = NVS_Get_Size_Of( pItem->nvsItem, &size );

			/* If size can be read */
			if( err == ESP_OK )
			{
				currentVal = pvPortMalloc( size );							// Allocate buffer
				if( currentVal != NULL )
				{
					err = NVS_Get( pItem->nvsItem, currentVal, &size );		// Get current value
					if( err == ESP_OK )
					{
						pItem->jItem.jValue.string = currentVal;			// Set value to allocated buffer
					}
				}
				else
				{
					err = ESP_FAIL;											// Allocation failed
				}
			}

			/* If any error in reading current value */
			if( err != ESP_OK )
			{
				/* Set NVS with default value */
				NVS_Set( pItem->nvsItem, pItem->jItem.jValue.string, 0 );
			}
			break;

		case JSON_NUMBER:
			IotLogError( "Storing floating-point value in NVS is not supported" );
			break;

		case JSON_INTEGER:
			if( ESP_OK != NVS_Get( pItem->nvsItem, pItem->jItem.jValue.integer, 0 ) )
			{
				NVS_Set( pItem->nvsItem, pItem->jItem.jValue.integer, 0 );
			}
			break;

		case JSON_INT32:
			if( ESP_OK != NVS_Get( pItem->nvsItem, pItem->jItem.jValue.integer32, 0 ) )
			{
				NVS_Set( pItem->nvsItem, pItem->jItem.jValue.integer32, 0 );
			}
			break;

		case JSON_UINT32:
			if( ESP_OK != NVS_Get( pItem->nvsItem, pItem->jItem.jValue.integerU32, 0 ) )
			{
				NVS_Set( pItem->nvsItem, pItem->jItem.jValue.integerU32, 0 );
			}
			break;

		case JSON_BOOL:
			if( ESP_OK != NVS_Get( pItem->nvsItem, &bValue, 0 ) )
			{
				bValue = ( *pItem->jItem.jValue.truefalse ? 1 : 0 );
				NVS_Set( pItem->nvsItem, &bValue, 0 );
			}
			else
			{
				*pItem->jItem.jValue.truefalse = ( bValue == 0 ) ? false : true;
			}
			break;

		case JSON_NONE:
		default:
			break;
	}
}

/**
 * @brief	Update the local storage for the shadow item
 *
 * @param[in] pItem		Pointer to the Shadow Item
 * @param[in] pValue	Pointer to the Shadow Item Value
 *
 * @return true: item value changed; false: item value unchanged
 */
static bool _updateItem(  _shadowItem_t * pItem, const void * pValue )
{
	bool	bChanged = false;
	size_t	size;
	IotLogInfo( "_updateItem: %s", pItem->jItem.key );

	switch( pItem->jItem.jType )
	{
		case JSON_STRING:														// '\0' terminated string
			size = strlen( ( char * ) pValue ) + 1;

			IotLogInfo( "_updateItem, string: %s -> %s", pItem->jItem.jValue.string, pValue );

			/* compare new to current value */
			if( strcmp( pValue, pItem->jItem.jValue.string ) != 0 )
			{
				bChanged = true;
			}

			/* Free existing buffer storage */
			if( pItem->jItem.jValue.string != NULL )
			{
				vPortFree( pItem->jItem.jValue.string );
				pItem->jItem.jValue.string = NULL;
			}
			/* Allocate space from new string */
			pItem->jItem.jValue.string = pvPortMalloc( size );
			memcpy( pItem->jItem.jValue.string, pValue, size );
			break;

		case JSON_NUMBER:
			IotLogError( "Storing floating-point value in NVS is not supported" );
			break;

		case JSON_INTEGER:
			if( *pItem->jItem.jValue.integer != *( int16_t * )pValue )
			{
				bChanged = true;
			}
			*pItem->jItem.jValue.integer = *( int16_t * )pValue;
			break;

		case JSON_INT32:
			if( *pItem->jItem.jValue.integer32 != *( int32_t * )pValue )
			{
				bChanged = true;
			}
			*pItem->jItem.jValue.integer32 = *( int32_t * )pValue;
			break;

		case JSON_UINT32:
			if( *pItem->jItem.jValue.integerU32 != *( uint32_t * )pValue )
			{
				bChanged = true;
			}
			*pItem->jItem.jValue.integerU32 = *( uint32_t * )pValue;
			break;

		case JSON_BOOL:
			{
				bool bValue;
				bValue = ( ( *( uint8_t *)pValue ) == 0 ) ? false : true;
				if( *pItem->jItem.jValue.truefalse != bValue )
				{
					bChanged = true;
				}
				*pItem->jItem.jValue.truefalse = bValue;
			}
			break;

		case JSON_NONE:
		default:
			break;
	}
	return bChanged;
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
 * Format buffer is allocated from heap and must be released after processing
 *
 * @return	Pointer to JSON document.  NULL is no shadow items have bUpdate flag set.
 */
static char * _formatShadowUpdate( void )
{
	int32_t	len;
	char * temp = NULL;
	char * mergeOutput = NULL;
	char * staticShadowJSON = NULL;
    _shadowItem_t *pShadowItem;
    _jsonItem_t *pItem;
    bool	bUpdateNeeded = false;

    IotLogInfo( "_formatShadowUpdate" );
    /* Start by creating a client token */
	len = mjson_printf( &mjson_print_dynamic_buf, &staticShadowJSON, "{%Q:%Q}", "clientToken", _makeToken() );

    /* Iterate through Shadow Item List */
    for( pShadowItem = shadowData.itemList; pShadowItem->jItem.key != NULL; ++pShadowItem )
    {
    	pItem = &pShadowItem->jItem;

    	/* For any item that needs updating */
    	if( pItem->bUpdate )
    	{
			/* Create a new json document for just that item */
    		temp = json_formatItem2Level( pItem, "state", "reported" );

      		/* Merge the new Item Document with the static shadow JSON. Output to mergeOutput */
    		len = mjson_merge(staticShadowJSON, len, temp, strlen( temp ), mjson_print_dynamic_buf, &mergeOutput);

    		/* Point the staticShadowJSON to the merged output and free the merged output and the temp key pair */
    		free( staticShadowJSON );
    		staticShadowJSON = mergeOutput;
    		mergeOutput = NULL;
    		free( temp );
    		temp = NULL;

    		bUpdateNeeded = true;

    	}
    }

    IotLogInfo( "shadowJSON = %s", staticShadowJSON );

    /* If no update is needed, free format buffer and return NULL */
    if( bUpdateNeeded == false )
    {
    	free( staticShadowJSON );
    	staticShadowJSON = NULL;
    }

	return staticShadowJSON;
}

/**
 * @brief	Callback Handler for Shadow Update
 */
static void _shadowUpdateCallback( void * reference,  AwsIotShadowCallbackParam_t * param )
{
	time_t *context = reference;

	if( ( param->callbackType == AWS_IOT_SHADOW_UPDATE_COMPLETE ) && ( *context == shadowData.contextTime ) )
	{
		if( param->u.operation.result == AWS_IOT_SHADOW_SUCCESS )
		{
			IotLogInfo( "Shadow Update: Success" );
		}
		else if( param->u.operation.result == AWS_IOT_SHADOW_TIMEOUT )
		{
			IotLogError( "Shadow Update: Timeout Error" );
		}
		else
		{
			IotLogError(" Shadow Update: Error[%d]", param->u.operation.result );
		}
	}
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
static int _updateReportedShadow(const char * updateJSON,
							int	sizeJSON,
							const AwsIotShadowCallbackInfo_t * pCallbackInfo)
{
	int status = EXIT_SUCCESS;
	AwsIotShadowError_t updateStatus = AWS_IOT_SHADOW_STATUS_PENDING;
	AwsIotShadowDocumentInfo_t updateDocument = AWS_IOT_SHADOW_DOCUMENT_INFO_INITIALIZER;
	AwsIotShadowCallbackInfo_t updateCallback = AWS_IOT_SHADOW_CALLBACK_INFO_INITIALIZER;

	/* Only proceed if an mqtt connection has been established */
	if( shadowData.mqttConnection != NULL )
	{

		/* If caller has not request a callback, use a generic callback */
//		if( pCallbackInfo == NULL )
//		{
		shadowData.contextTime = getTimeValue();
		updateCallback.pCallbackContext = &shadowData.contextTime;
		updateCallback.function = _shadowUpdateCallback;
//		}

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
													 &updateCallback,
//													 pCallbackInfo,
													 NULL);
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
	else
	{
		IotLogError("Error updating shadow. No MQTT Connection");
		status = EXIT_FAILURE;
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
    _shadowItem_t *pShadowItem;
    _jsonItem_t *pItem;

    char * updateDocument;

    /* Iterate through Shadow Item list */
    for( pShadowItem = shadowData.itemList; pShadowItem->jItem.key != NULL; ++pShadowItem )
    {
    	char matchstr[ 48 ];
    	char outbuf[ 30 ];
    	int result;
    	double value;
    	int ivalue;

    	pItem = &pShadowItem->jItem;

    	/* assemble a match string */
    	if( pItem->section == NULL )
    	{
    		snprintf( matchstr, sizeof( matchstr ), "$.state.%s", pItem->key );
    	}
    	else
    	{
    		snprintf( matchstr, sizeof( matchstr ), "$.state.%s.%s", pItem->section, pItem->key );
    	}
    	IotLogInfo( "_shadowDeltaCallback: matchstr = %s", matchstr );

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
					IotLogInfo( "Found %s = %s", matchstr, outbuf );
					pItem->bUpdate = true;
					deltaFound = true;
				}
				break;

			case JSON_NUMBER:
			case JSON_INTEGER:
			case JSON_INT32:
			case JSON_UINT32:
				result = mjson_get_number( pCallbackParam->u.callback.pDocument,
							pCallbackParam->u.callback.documentLength,
							matchstr,
							&value );
				if( result != 0 )
				{
					IotLogInfo( "Found %s = %f", matchstr, value );
					if( pItem->jType == JSON_NUMBER )
					{
						*pItem->jValue.number = value;
					}
					else if( pItem->jType == JSON_INTEGER )
					{
						*pItem->jValue.integer = ( int16_t )value;
					}
					else if( pItem->jType == JSON_INT32 )
					{
						*pItem->jValue.integer32 = ( int32_t )value;
					}
					else
					{
						*pItem->jValue.integerU32 = ( uint32_t )value;
					}
					pItem->bUpdate = true;
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
					*pItem->jValue.truefalse = ivalue ? true : false;
					pItem->bUpdate = true;
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
    	if( updateDocument != NULL )
    	{
    	IotLogInfo( "Update Document = %s", updateDocument );
			/* Update shadow */
			_updateReportedShadow( updateDocument, strlen( updateDocument), NULL );
			free( updateDocument );
    	}
    }
    else
    {
    	IotLogInfo( "Not found: %.*s", pCallbackParam->u.callback.documentLength, pCallbackParam->u.callback.pDocument );
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

    _shadowItem_t *pShadowItem;
    _jsonItem_t	*pItem;

    /* Debug - print the update document */
//	printf( "_shadowUpdatedCallback: doc[%d] = %.*s\n",
//			pCallbackParam->u.callback.documentLength,
//			pCallbackParam->u.callback.documentLength,
//			pCallbackParam->u.callback.pDocument );

	/* Don't try to process the document if it is very small */
	if( pCallbackParam->u.callback.documentLength > MIN_UPDATE_LEN )
	{
		/* Iterate through Shadow Item List */
		for( pShadowItem = shadowData.itemList; pShadowItem->jItem.key != NULL; ++pShadowItem )
		{
			bUpdateComplete = false;

			pItem = &pShadowItem->jItem;

			/* assemble a match string */
	    	if( pItem->section == NULL )
	    	{
	    		snprintf( matchstr, sizeof( matchstr ), "$.current.state.reported.%s", pItem->key );
	    	}
	    	else
	    	{
	    		snprintf( matchstr, sizeof( matchstr ), "$.current.state.reported.%s.%s", pItem->section, pItem->key );
	    	}
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
				case JSON_INT32:
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
						else if( pItem->jType == JSON_INT32 )
						{
							/* If values match, cancel the update flag */
							if( ( *pItem->jValue.integer32 == ( int32_t )value ) && pItem->bUpdate )
							{
								IotLogInfo( "Found %s = %d", matchstr, ( int32_t )value );
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
				if( pShadowItem->nvsItem != -1 )
				{
					_storeInNvs( pShadowItem );
				}
				/* Call UpdateCompleteCallback handler, if present */
				if( pShadowItem->handler != NULL )
				{
					IotLogInfo( "shadow update[%s]:handler", pItem->key );
					pShadowItem->handler( pShadowItem );
				}
			}

			/* Allow other tasks to run - prevent task_wdt events when processing large delta documents */
			vTaskDelay( 10 / portTICK_PERIOD_MS );
		}
	}
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

/* ************************************************************************* */
/* ************************************************************************* */
/* **********        I N T E R F A C E   F U N C T I O N S        ********** */
/* ************************************************************************* */
/* ************************************************************************* */

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

    /* Set the Shadow connected flag */
    if( status == EXIT_SUCCESS )
    {
        shadowData.bConnected = true;
    }

    return status;
}

/**
 * @brief	Shadow Disconnect
 *
 * This function should called upon an MQTT disconnect.
 * By clearing the callbacks here, they can be re-subscribed to
 * upon reconnection.
 */
void shadow_disconnect( void )
{

	IotLogInfo( "shadow_disconnect" );

	/* Clear the Shadow connected flag */
    shadowData.bConnected = false;

    /* Remove the Delta callback */
    AwsIotShadow_SetDeltaCallback( shadowData.mqttConnection,
    		shadowData.pThingName,
			shadowData.thingNameLength,
			0,
            NULL );

    /* Remove the Updated callback */
    AwsIotShadow_SetUpdatedCallback( shadowData.mqttConnection,
    		shadowData.pThingName,
			shadowData.thingNameLength,
            0,
            NULL );
}

/**
 * @brief	Update the Report Shadow State
 *
 * A JSON shadow document is formatted, using the bUpdate flags of the shadow items.
 * If one or more items have their bUpdate flag set, the JSON document will be valid
 * and will be published, updating the reported state of the device shadow.
 */
void shadow_updateReported( void )
{
	char *updateDocument;

	/*
	 * Only proceed if the Shadow connected flag is set AND
	 * an mqtt connection has been established, otherwise the bUpdate flags will get cleared
	 */
	if( ( shadowData.bConnected == true ) && ( shadowData.mqttConnection != NULL ) )
	{
		updateDocument = _formatShadowUpdate();

		if( updateDocument != NULL )
		{
			IotLogInfo( "Update Document = %s", updateDocument );

			/* Update shadow */
			_updateReportedShadow( updateDocument, strlen( updateDocument), NULL );
			free( updateDocument );
		}
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
	if( !shadowData.bInitialized )
	{
		shadowInitStatus = AwsIotShadow_Init( 0 );

		if( shadowInitStatus == AWS_IOT_SHADOW_SUCCESS )
		{
			shadowData.bInitialized = true;
		}
		else
		{
			IotLogError( "ERROR: Shadow Initialization Failed" );
		}

	}

	return shadowInitStatus;

}

/**
 * @brief	Initialize a Shadow String Item to "uninitialized"
 *
 * Allocate a dynamic buffer and copy constant string into it.
 */
static void _initString( char **pString )
{
	const char uninit[] = "uninitialized";

	/* Allocate space from new string */
	*pString = pvPortMalloc( sizeof( uninit ) );
	if( pString == NULL )
	{
		IotLogError( "Cannot allocate shadow item buffer" );
	}
	else
	{
		strcpy( *pString, uninit );				// copy "uninitialized" string
		printf( "_initString: %s\n", *pString );
	}
}

/**
 * @brief	Initialize Shadow Item List
 *
 * @param[in]	pShadowItemList		Pointer to list of Shadow Items
 */
void shadow_initItemList( _shadowItem_t *pShadowItemList )
{
	_shadowItem_t *pShadowItem;

	IotLogInfo( "Initializing Shadow Item List" );
	shadowData.itemList = pShadowItemList;
	shadowData.numberOfItems = 0;

	/* Iterate through Shadow Item List fetching values from NVS */
	for( pShadowItem = shadowData.itemList; pShadowItem->jItem.key != NULL; ++pShadowItem )
	{
		++shadowData.numberOfItems;
		if( pShadowItem->nvsItem != -1 )
		{
			/* For strings with associated NVS set default to a dynamically allocated "Uninitialized" string */
			if( pShadowItem->jItem.jType == JSON_STRING )
			{
				_initString( &pShadowItem->jItem.jValue.string );
			}
			_fetchFromNvs( pShadowItem );
			pShadowItem->jItem.bUpdate = true;
			vTaskDelay( 10 / portTICK_PERIOD_MS );
		}
		else if( pShadowItem->jItem.jType == JSON_STRING )
		{
			/* For strings without associated NVS storage, create string, but don't set update flag */
			_initString( &pShadowItem->jItem.jValue.string );
			printf( "shadow_initItemList, key = %s, value = %s\n", pShadowItem->jItem.key, pShadowItem->jItem.jValue.string );
		}
	}
	IotLogInfo( "%d items initialized", shadowData.numberOfItems );
}

/**
 * @brief	Update a Shadow Item
 *
 * The local storage is updated
 * If the Item has associated NVS storage, that is updated
 * The Item's update flag is set
 * Attempt to update the AWS shadow
 *
 * @param[in]	item	Shadow Item index
 * @param[in]	pData	Pointer to the item's new data
 *
 * @return true: item value changed; false: item value unchanged
 */
bool shadow_updateItem( int item, const void * pData )
{
	bool bChanged = false;
	_shadowItem_t *pShadowItem;

	IotLogInfo( "shadow_updateItem: %d", item );

	if( item < shadowData.numberOfItems )
	{
		pShadowItem = &shadowData.itemList[ item ];
		bChanged = _updateItem( pShadowItem, pData );			// update the local storage
		if( pShadowItem->nvsItem != -1 )						// for items with NVS storage
		{
			_storeInNvs( pShadowItem );							// Update NVS store
		}
		pShadowItem->jItem.bUpdate = true;						// set the update flag
		shadow_updateReported();								// Update the Shadow
	}
	else
	{
		IotLogError( "Invalid Shadow Item (%d), number of items = %d", item, shadowData.numberOfItems );
	}
	return bChanged;
}
