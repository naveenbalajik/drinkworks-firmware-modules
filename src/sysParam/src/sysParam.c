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
#include	"freertos/FreeRTOS.h"
#include	"freertos/task.h"
#include	"platform/iot_clock.h"
#include	"json.h"
#include	"mjson.h"
#include	"mqtt.h"
#include	"sysParam.h"
#include	"shadow_updates.h"

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
 * @brief Format System Parameter Update
 *
 * Build a System Parameter Update document, based on the bUpdate flags in the table.
 */
/* FIXME: don't look at bUpdate flasgs at the moment, output full list every period */
static char * _formatSysParmUpdate( void )
{
	const char * bufferA = NULL;
	const char * bufferB = NULL;
	const char * mergeOutput = NULL;
	_jsonItem_t *pItem;

	IotLogInfo( "_formatSysParmUpdate" );

	/* SerialNumber */
	bufferA = json_formatSerialNumber();

	/* Timestamp */
	bufferB = json_formatUTC( "createdAt" );

	/* merge header items */
	mjson_merge( bufferA, strlen( bufferA ), bufferB, strlen( bufferB ), mjson_print_dynamic_buf, &mergeOutput );
	free( bufferA );
	bufferA = mergeOutput;
	mergeOutput = NULL;
	free( bufferB );
	bufferB = NULL;

    /* Iterate through Shadow Item List */
    for( pItem = sysParamData.config->pList; pItem->key != NULL; ++pItem )
    {
/* For any item that needs updating */
//    	if( pItem->bUpdate )
//    	{
			/* Create a new json document for just that item */
    		bufferB = json_formatItem0Level( pItem );

      		/* Merge the new Item Document with the previous. Output to mergeOutput */
    		mjson_merge( bufferA, strlen( bufferA ), bufferB, strlen( bufferB ), mjson_print_dynamic_buf, &mergeOutput );
    		free( bufferA );
    		bufferA = mergeOutput;
    		mergeOutput = NULL;
    		free( bufferB );
    		bufferB = NULL;

    		/* clear the update flag */
    		/* TODO: clearing flags would be better after an MQTT acceptance */
//    		pItem->bUpdate = false;
//     	}
    }

	return bufferA;
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

			printf( "publishParams: %s\n--> %s\n\n", pJson, topic );
			mqtt_SendMsgToTopic( topic, strlen( topic ), pJson, strlen( pJson ), NULL );

//			mqtt_SendMsgToTopic( topic, strlen( topic ), pJson, strlen( pJson ), &publishCallback );

			free( pJson );					/* free buffer after if is processed */

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
				publishParams( sysParamData.config->topicProduction, pJson );		/* Prod */
			}
			else
			{
				publishParams( sysParamData.config->topicDevelop, pJson );		/* Dev */
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
	sysParamData.config = config;


	/* Create Task on new thread */
    xTaskCreate( _sysParamTask, "sys_param", SYS_PARAM_STACK_SIZE, NULL, SYS_PARAM_TASK_PRIORITY, &sysParamData.taskHandle );
    if( NULL == sysParamData.taskHandle )
	{
        return ESP_FAIL;
    }
    else
    {
    	IotLogInfo( "sys_param created" );
    }

    return	ESP_OK;
}
