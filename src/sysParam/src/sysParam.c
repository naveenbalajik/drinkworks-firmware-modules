/**
 * @file	sysParam.c
 *
 *	Push System Parameter to MQTT Topic.
 *
 * Created on: October 21, 2020
 * Author: Ian Whitehead
 */
#include	<stdlib.h>
#include	<stdio.h>
#include	<stdint.h>
#include	<stdbool.h>
#include	<string.h>
#include	"freertos/task.h"
#include	"mjson.h"
#include	"mqtt.h"
#include	"sysParam.h"

/* Debug Logging */
#include "sysParam_logging.h"

#define	SYS_PARAM_STACK_SIZE    ( 3076 )

#define	SYS_PARAM_TASK_PRIORITY	( 2 )

typedef	struct
{
	TaskHandle_t		taskHandle;												/**< handle for System Parameter Task */
	_sysParamConfig_t * config;
} sysParam_t;

static sysParam_t	sysParamData =
{
	.config = NULL,
};

/**
 * @brief Update a SysParam Item
 *
 *	Assumes that Item has a Section
 *
 * @param[in] pItem			Pointer to SysParam Item
 */
static char * _formatJsonItem( _sysParamItem_t * pItem )
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

		case JSON_UINT16:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:{%Q:{%Q:%d}}}}",
					"state",
					"reported",
					pItem->section,
					pItem->key,
					*pItem->jValue.integerU16
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
 * @brief Format System Parameter Update
 *
 * Build a System Parameter Update document, based on the bUpdate flags in the table.
 */
static char * _formatSysParmUpdate( void )
{
	int32_t	len;
	char * temp = NULL;
	char * mergeOutput = NULL;
	char * outputJSON = NULL;
	_sysParamItem_t *pItem;

    /* Start by creating a client token */
	len = mjson_printf( &mjson_print_dynamic_buf, &outputJSON, "{%Q:%Q}", "clientToken", _makeToken() );

    /* Iterate through Shadow Item List */
    for( pItem = sysParamData.list; pItem->key != NULL; ++pItem )
    {
    	/* For any item that needs updating */
    	if( pItem->bUpdate )
    	{
			/* Create a new json document for just that item */
    		temp = _formatJsonItem( pItem );

      		/* Merge the new Item Document with the static shadow JSON. Output to mergeOutput */
    		len = mjson_merge(outputJSON, len, temp, strlen( temp ), mjson_print_dynamic_buf, &mergeOutput);

    		/* Point the outputJSON to the merged output and free the merged output and the temp key pair */
    		free( outputJSON );
    		outputJSON = mergeOutput;
    		mergeOutput = NULL;
    		free( temp );
    		temp = NULL;

    	}
    }

	return outputJSON;
}

/**
 * @brief	Publish System Parameters to AWS
 *
 */
/* TODO: Add callback, look for rejection */
static void publishParams( const char *topic, const char *pJson )
{
//	IotMqttCallbackInfo_t publishCallback = IOT_MQTT_CALLBACK_INFO_INITIALIZER;

	if( mqtt_IsConnected() )
	{
		if( NULL != pJson )
		{
//			publishCallback.function = vSysParamPublishComplete;

			/* Clear flags */
//			_evtrec.publishComplete = false;
//			_evtrec.publishSuccess = false;

			/* Use Time Value as context */
//			_evtrec.contextTime =  getTimeValue();
//			publishCallback.pCallbackContext = &_evtrec.contextTime;

			mqtt_SendMsgToTopic( topic, strlen( topic ), pJson, strlen( pJson ), NULL );
//			mqtt_SendMsgToTopic( topic, strlen( topic ), pJson, strlen( pJson ), &publishCallback );

			vPortFree( pJson );					/* free buffer after if is processed */

		}
	}
}

/**
 * @brief	System Parameter Task
 *
 * Executes on separate thread:
 *  - Periodically format updates to System Parameters, as JSON document
 *  - Publish JSON document to MQTT Topic
 *
 * @param[in] arg 	Not used
 *
 */
static void _sysParamTask(void *arg)
{
	esp_err_t err;

	const char * pJson;

	vTaskDelay( 10000 / portTICK_PERIOD_MS );

	IotLogInfo( "_sysParamTask" );

    while( 1 )
	{
    	/* Only process records if user has opted in to Data Sharing */
    	if( shadowUpdates_getDataShare() )
    	{
    		/* Format updates to system parameters */
        	pJson = _formatSysParmUpdate( );

			/* get Event Records from FIFO, push to AWS */
			if( shadowUpdates_getProductionRecordTopic() )
			{
				publishRecords( sysParamData.config->topicPoduction, pJson );		/* Prod */
			}
			else
			{
				publishRecords( sysParamData.config->topicDevelop, pJson );		/* Dev */
			}

    	}

    	/* Wait: update interval */
		vTaskDelay( sysParamData.config->updateInterval / portTICK_PERIOD_MS );
    }
}



/* ************************************************************************* */
/* ************************************************************************* */
/* **********        I N T E R F A C E   F U N C T I O N S        ********** */
/* ************************************************************************* */
/* ************************************************************************* */

/**
 * @brief Initialize System Parameter submodule - register commands
 */
int32_t sysParam_init( _sysParamConfig_t * config )
{
	esp_err_t	err = ESP_OK;

	/* save the system parameter configuration */
	_sysParamData.config = config;


	/* Create Task on new thread */
    xTaskCreate( _sysParamTask, "sys_param", SYS_PARAM_STACK_SIZE, NULL, SYS_PARAM_TASK_PRIORITY, &sysParamData.taskHandle );
    if( NULL == sysParamData.taskHandle )
	{
        return ESP_FAIL;
    }
    {
    	IotLogInfo( "sys_param created" );
    }

    return	ESP_OK;
}
