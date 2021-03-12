/*
 * FreeRTOS V202002.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/**
 * @file ota_update.c
 * @brief A simple OTA update example.
 *
 * This example initializes the OTA agent to enable OTA updates via the
 * MQTT broker. It simply connects to the MQTT broker with the users
 * credentials and spins in an indefinite loop to allow MQTT messages to be
 * forwarded to the OTA agent for possible processing. The OTA agent does all
 * of the real work; checking to see if the message topic is one destined for
 * the OTA agent. If not, it is simply ignored.
 */
/* The config header is always included first. */
#include "iot_config.h"

/* MQTT include. */
#include "iot_mqtt.h"
/* Standard includes. */
#include <stdio.h>
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* FreeRTOS OTA agent includes. */
#include "aws_iot_ota_agent.h"

/* FreeRTOS OTA PAL includes. */
#include "aws_iot_ota_pal.h"

#include "host_ota_pal.h"

#include "iot_network_manager_private.h"

/* Required for task stack and priority */
#include "aws_app_config.h"

/* Platform layer includes. */
#include "platform/iot_clock.h"
#include "platform/iot_threads.h"

/* Set up logging for this module */
#include "ota_logging.h"

//#include "application.h"
#include "mqtt.h"

#include "ota_update.h"

#include "host_ota.h"

#include "wifiFunction.h"

static void App_OTACompleteCallback( OTA_JobEvent_t eEvent );

#define	OTA_UPDATE_STACK_SIZE    ( 3076 )

#define	OTA_UPDATE_TASK_PRIORITY	( 7 )

typedef	enum
{
	eOtaTaskInit,
	eOtaTaskStart,
	eOtaTaskRun,
	eOtaTaskComplete,
	eOtaTaskSuspend,
	eOtaTaskResume
} _otaTaskState_t;


typedef	struct
{
	TaskHandle_t				taskHandle;									/**< handle for OTA Update Task */
	_otaTaskState_t				taskState;
	bool						bConnected;
	OTA_State_t					eState;
	OTA_ConnectionContext_t		connectionCtx;
	const char * 				pIdentifier;
	IotSemaphore_t	*			pHostUpdateComplete;
	hostOtaPendUpdateCallback_t	pendingHostOtaUpdate;						/**< callback function, returns true if HostOta task is pending on an image update; false indicates task is otherwise busy */
	const AltProcessor_Functions_t * hostPal;									/**< pointer to Host OTA PAL functions */

} otaData_t;

static otaData_t otaData =
{
	.taskState = eOtaTaskInit,
	.pHostUpdateComplete = NULL,
	.pendingHostOtaUpdate = NULL,
};

/**
 * @brief Network types allowed for OTA. Only support OTA over Wifi
 */
#define otaNETWORK_TYPES                  ( AWSIOT_NETWORK_TYPE_WIFI )

/**
 * @brief The maximum time interval that is permitted to elapse between the point at
 * which the MQTT client finishes transmitting one control Packet and the point it starts
 * sending the next.In the absence of control packet a PINGREQ  is sent. The broker must
 * disconnect a client that does not send a message or a PINGREQ packet in one and a
 * half times the keep alive interval.
 */
#define OTA_KEEP_ALIVE_SECONDS                  ( 120UL )

/**
 * @brief Timeout for MQTT connection, if the MQTT connection is not established within
 * this time, the connect function returns #IOT_MQTT_TIMEOUT
 */
#define OTA_CONNECTION_TIMEOUT_MS               ( 2000UL )

/**
 * @brief The base interval in seconds for retrying network connection.
 */
//#define OTA_CONN_RETRY_BASE_INTERVAL_SECONDS    ( 2U )

/**
 * @brief The delay used in the main OTA Demo task loop to periodically output the OTA
 * statistics like number of packets received, dropped, processed and queued per connection.
 */
#define OTA_TASK_DELAY_SECONDS                  ( 1UL )

/**
 * @brief The maximum interval in seconds for retrying network connection.
 */
//#define OTA_CONN_RETRY_MAX_INTERVAL_SECONDS     ( 360U )

/**
 * @brief Connection retry interval in seconds.
 */
//static int _retryInterval = OTA_CONN_RETRY_BASE_INTERVAL_SECONDS;

/**
 * @brief OTA agent state
 */
static const char * _pStateStr[ eOTA_AgentState_All ] =
{
    "Init",
    "Ready",
    "RequestingJob",
    "WaitingForJob",
    "CreatingFile",
    "RequestingFileBlock",
    "WaitingForFileBlock",
    "ClosingFile",
    "Suspended",
    "ShuttingDown",
    "Stopped"
};

static OTA_PAL_ImageState_t CurrentImageState = eOTA_PAL_ImageState_Valid;

/*--------------------------- OTA PAL OVERRIDES ----------------------------*/

/*
 * OTA PAL Overrides
 *
 * These functions are used by the OTA Initialization function to override the default PAL layer
 * functions.
 *
 * Their purpose is to re-direct execution to alternate processor functions, if the ServerFileId is
 * non-zero, or use the default PAL layer function when the ServerFileId is zero.
 *
 * A table of alternate processor functions is passed to the OTAUpdate initialization function.
 *
 * For PIC32MZ updates set the file ID to '1' in the OTA Job
 *
 */

#ifdef NEEDED
OTA_JobParseErr_t otaDemoCustomJobCallback( const char * pcJSON, uint32_t ulMsgLen )
{
    DEFINE_OTA_METHOD_NAME( "prvDefaultCustomJobCallback" );
    configPRINTF(("Job Found:\r\n"));
    if ( pcJSON != NULL )
    {
        //Process Custom job
    }

    OTA_LOG_L1( "[%s] Received Custom Job inside OTA Demo.\r\n", OTA_METHOD_NAME );

    return eOTA_JobParseErr_None;
}

#endif	/* NEEDED */

OTA_Err_t prvPAL_Abort_override( OTA_FileContext_t * const C )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_Abort_override" );

    if ( C == NULL )
    {
        OTA_LOG_L1( "[%s] File context null\r\n", OTA_METHOD_NAME );
        return kOTA_Err_AbortFailed;
    }

    if ( C->ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_Abort( C );
    }
    else if( ( otaData.hostPal == NULL ) || (otaData.hostPal->xAbort == NULL ) )
    {
        OTA_LOG_L1( "[%s] Alternate function null\r\n", OTA_METHOD_NAME );
        return kOTA_Err_AbortFailed;
    }
    else
    {
        OTA_LOG_L1( "[%s] OTA for alternate processor\r\n", OTA_METHOD_NAME );
        return otaData.hostPal->xAbort( C );
    }
}

/**
 * @brief	Override function for prvPAL_ActivateNewImage()
 *
 * This override function calls the default PAL function if the ServerFileID is zero (i.e. ESP32 Updates)
 * For other ServerFileID values a modified PAL function is called, with partition type and subtype values,
 * to process updates for secondary processors.
 *
 * @param[in]	C	OTA File context pointer
 *
 * @return kOTA_Err_None if successful, otherwise an error code prefixed with 'kOTA_Err_'.
 *
 * TODO: hard wiring to partition 0x44/0x57 ("DW") violates modularity intent.
 */
OTA_Err_t prvPAL_ActivateNewImage_override( uint32_t ulServerFileID )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_ActivateNewImage_override" );

    if ( ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_ActivateNewImage();
    }
    else if( ( otaData.hostPal == NULL ) || (otaData.hostPal->xActivateNewImage == NULL ) )
    {
        OTA_LOG_L1( "[%s] Alternate function null\r\n", OTA_METHOD_NAME );
        return kOTA_Err_ActivateFailed;
    }
    else
    {
        OTA_LOG_L1( "[%s] OTA for alternate processor.\r\n", OTA_METHOD_NAME );
        // Reset self after doing cleanup
        return otaData.hostPal->xActivateNewImage();
    }
}

OTA_Err_t prvPAL_CloseFile_override( OTA_FileContext_t * const C )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_CloseFile_override" );

    if ( C->ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_CloseFile( C );
    }
    else if( ( otaData.hostPal == NULL ) || (otaData.hostPal->xCloseFile == NULL ) )
    {
        OTA_LOG_L1( "[%s] Alternate function null\r\n", OTA_METHOD_NAME );
        return -1;
    }
    else
    {
        OTA_LOG_L1( "[%s] Received prvPAL_CloseFile_customer inside OTA for alternate processor.\r\n", OTA_METHOD_NAME );
        return otaData.hostPal->xCloseFile( C );
    }
}

/**
 * @brief	Override function for prvPAL_CreateFileForRx()
 *
 * This override function calls the default PAL function if the ServerFileID is zero (i.e. ESP32 Updates)
 * For other ServerFileID values a modified PAL function is called, with partition type and subtype values,
 * to process updates for secondary processors.
 *
 * @param[in]	C	OTA File context pointer
 *
 * @return kOTA_Err_None if successful, otherwise an error code prefixed with 'kOTA_Err_'.
 */
static OTA_Err_t prvPAL_CreateFileForRx_override( OTA_FileContext_t * const C )
{
	esp_partition_type_descriptor_t partitionDescriptor = { .type = 0x44, .subtype = 0x57 };

    DEFINE_OTA_METHOD_NAME( "prvPAL_CreateFileForRx_override" );

    if ( C == NULL )
    {
        OTA_LOG_L1( "[%s] File context null\r\n", OTA_METHOD_NAME );
        return kOTA_Err_RxFileCreateFailed;
    }

    if ( C->ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_CreateFileForRx( C );
    }
    else if( ( otaData.hostPal == NULL ) || (otaData.hostPal->xCreateFileForRx == NULL ) )
    {
        OTA_LOG_L1( "[%s] Alternate function null\r\n", OTA_METHOD_NAME );
        return kOTA_Err_RxFileCreateFailed;
    }
    else
    {
        OTA_LOG_L1( "[%s] OTA for alternate processor.\r\n", OTA_METHOD_NAME );
        return otaData.hostPal->xCreateFileForRx( C, &partitionDescriptor );
    }
}

/**
 * @brief	Override function for prvPAL_GetPlatformImageState()
 *
 * This override function calls the default PAL function if the ServerFileID is zero (i.e. ESP32 Updates).
 * For other ServerFileID values hostOta_getImageState() is called, to process updates for secondary processors.
 *
 * @param[in]	ulServerFileID	Server File ID (0 = ESP32)
 *
 * @return	Enumerated platform image state
 */
static OTA_PAL_ImageState_t prvPAL_GetPlatformImageState_override( uint32_t ulServerFileID )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_GetPlatformImageState_override" );

    if ( ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_GetPlatformImageState();
    }
    else if( ( otaData.hostPal == NULL ) || (otaData.hostPal->xGetImageState == NULL ) )
    {
        OTA_LOG_L1( "[%s] Alternate function null\r\n", OTA_METHOD_NAME );
        return -1; /* TODO Is this the appropriate return value or error? */
    }
    else
    {
    	CurrentImageState = otaData.hostPal->xGetImageState();
        OTA_LOG_L1( "[%s](%d) OTA for alternate processor: %d.\r\n", OTA_METHOD_NAME, ulServerFileID, CurrentImageState );
        return CurrentImageState;
    }
}


OTA_Err_t prvPAL_ResetDevice_override( uint32_t ulServerFileID )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_ResetDevice_override" );

    if ( ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_ResetDevice();
    }
    else if( ( otaData.hostPal == NULL ) || (otaData.hostPal->xResetDevice == NULL ) )
    {
        OTA_LOG_L1( "[%s] Alternate function null\r\n", OTA_METHOD_NAME );
        return kOTA_Err_ResetNotSupported;
    }
    else
    {
        OTA_LOG_L1( "[%s] OTA for alternate processor.\r\n", OTA_METHOD_NAME );
        return otaData.hostPal->xResetDevice();
    }
}


/**
 * @brief	Override function for prvPAL_SetPlatformImageState()
 *
 * This override function calls the default PAL function if the ServerFileID is zero (i.e. ESP32 Updates).
 * For other ServerFileID values hostOta_setImageState() is called, to process updates for secondary processors.
 *
 * @param[in]	ulServerFileID	Server File ID (0 = ESP32)
 * @param[in]	eState Enumerated Platform Image state
 *
 * @return kOTA_Err_None if successful, otherwise an error code prefixed with 'kOTA_Err_'.
 */
static OTA_Err_t prvPAL_SetPlatformImageState_override( uint32_t ulServerFileID, OTA_ImageState_t eState )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_SetPlatformImageState_override" );

    if ( ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_SetPlatformImageState(eState);
    }
    else if( ( otaData.hostPal == NULL ) || (otaData.hostPal->xSetImageState == NULL ) )
    {
        OTA_LOG_L1( "[%s] Alternate function null\r\n", OTA_METHOD_NAME );
        return -1;
    }
    else
    {
        OTA_LOG_L1( "[%s](%d) OTA for alternate processor: %d\r\n", OTA_METHOD_NAME, ulServerFileID, eState  );
        otaData.hostPal->xSetImageState( eState );

//        if ( eState==eOTA_ImageState_Testing )
//        {
//            CurrentImageState = eOTA_PAL_ImageState_PendingCommit;
//        }

        return kOTA_Err_None;
    }
}

int16_t prvPAL_WriteBlock_override( OTA_FileContext_t * const C,
                           uint32_t iOffset,
                           uint8_t * const pacData,
                           uint32_t iBlockSize )
 {
    DEFINE_OTA_METHOD_NAME( "prvPAL_WriteBlock_override" );

    if ( C == NULL )
    {
        OTA_LOG_L1( "[%s] File context null\r\n", OTA_METHOD_NAME );
        return -1;
    }

    if ( C->ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_WriteBlock( C, iOffset, pacData, iBlockSize );
    }
    else if( ( otaData.hostPal == NULL ) || (otaData.hostPal->xWriteBlock == NULL ) )
    {
        OTA_LOG_L1( "[%s] Alternate function null\r\n", OTA_METHOD_NAME );
        return -1;
    }
    else
    {
        OTA_LOG_L1( "[%s] OTA for alternate processor.\r\n", OTA_METHOD_NAME );
        return otaData.hostPal->xWriteBlock( C, iOffset, pacData, iBlockSize );
    }
}

/* end of secondaryota_patch.txt */

/**
 * @brief	OTA Update Task
 *
 * The OTA Update state machine runs in a separate task.
 * It is responsible for initializing the OTA Agent,
 * suspending the agent when MQTT connection is lost,
 * resuming the Agent when MQTT connection is restored
 *
 * param[in]	arg		Not used
 */

static void _OTAUpdateTask( void *arg )
{

	/* Only need to override the default callbacks that are needed, other than xCompleteCallback */
    OTA_PAL_Callbacks_t otaCallbacks = {
        .xAbort                    = prvPAL_Abort_override,
        .xActivateNewImage         = prvPAL_ActivateNewImage_override,
        .xCloseFile                = prvPAL_CloseFile_override,
        .xCreateFileForRx          = prvPAL_CreateFileForRx_override,
        .xGetPlatformImageState    = prvPAL_GetPlatformImageState_override,
        .xResetDevice              = prvPAL_ResetDevice_override,
        .xSetPlatformImageState    = prvPAL_SetPlatformImageState_override,
        .xWriteBlock               = prvPAL_WriteBlock_override,
        .xCompleteCallback         = App_OTACompleteCallback,
        .xCustomJobCallback        = NULL	// otaDemoCustomJobCallback
    };

	IotLogInfo( "_OTAUpdateTask" );

	/* Continually loop until OTA process is completed */
	for( ; ; )
	{
		otaData.bConnected = mqtt_IsConnected();
		otaData.eState = OTA_GetAgentState();

		switch( otaData.taskState )
		{

			case eOtaTaskInit:
				/* If a callback function for pendingHostOtaUpdate has been registered */
				if( NULL != otaData.pendingHostOtaUpdate )
				{
					/* Delay starting OTA Update task if Host Ota Update task is busy (e.g. transfer is in process) */
					if( otaData.pendingHostOtaUpdate() == false )
					{
						vTaskDelay( 1000 / portTICK_PERIOD_MS );
						break;
					}
				}
				IotLogInfo( "ota -> Start" );
				otaData.taskState = eOtaTaskStart;
				break;

			case eOtaTaskStart:
				if( otaData.bConnected )
				{
					/* Short delay after MQTT connection is established */
					vTaskDelay( 5000 / portTICK_PERIOD_MS );

					/* Once connected, get the MQTT connection reference */
					otaData.connectionCtx.pvControlClient = mqtt_getConnection();

					IotLogInfo( "_OTAUpdateTask: MQTT Connected (%p)", otaData.connectionCtx.pvControlClient );

					/*
					 * If Agent is not already running, initialize the OTA Agent, using internal init function,
					 * so PAL callbacks can be overridden
					 */
					OTA_AgentInit_internal( ( void * ) ( &otaData.connectionCtx ),
											( const uint8_t * ) ( otaData.pIdentifier ),
											&otaCallbacks,
											( TickType_t ) ~0 );
					IotLogInfo( "ota -> Run" );
					otaData.taskState = eOtaTaskRun;
				}
				else
				{
					/* wait for MQTT connection */
					vTaskDelay( 100 / portTICK_PERIOD_MS );
				}
				break;

			case eOtaTaskRun:
				if( otaData.eState == eOTA_AgentState_Stopped )
				{
					IotLogInfo( "OTA Agent Stopped. Disconnecting" );
					IotLogInfo( "ota -> Complete" );
					otaData.taskState = eOtaTaskComplete;
				}
				else if( otaData.bConnected == false )
				{
					/* If MQTT connection is broken */
					IotLogInfo( "OTA Agent Disconnected. Suspending" );
					vTaskDelay( 100 / portTICK_PERIOD_MS );

					/* Send Suspend event to OTA agent.*/
					if( OTA_Suspend() == kOTA_Err_None )
					{
						/* Command accepted - transition to Suspend state */
						IotLogInfo( "ota -> Suspend" );
						otaData.taskState = eOtaTaskSuspend;
					}
					else
					{
						/* Command not accepted - where to now? */
					}
				}
				else
				{
					/* Periodically output statistics */
					IotLogInfo( "State: %s  Received: %u   Queued: %u   Processed: %u   Dropped: %u\r\n", _pStateStr[ otaData.eState ],
								OTA_GetPacketsReceived(), OTA_GetPacketsQueued(), OTA_GetPacketsProcessed(), OTA_GetPacketsDropped() );

					/* Wait - one second. */
					IotClock_SleepMs( OTA_TASK_DELAY_SECONDS * 1000 );
				}
				break;


			case eOtaTaskSuspend:
				/* When agent transitions to Suspended state, wait for reconnection */
				if( otaData.eState == eOTA_AgentState_Suspended )
				{
					IotLogInfo( "ota -> Resume" );
					otaData.taskState = eOtaTaskResume;
				}
				else
				{
					/* Wait for OTA Agent to process the suspend event. */
					IotClock_SleepMs( OTA_TASK_DELAY_SECONDS * 1000 );
				}
				break;

			case eOtaTaskResume:
				if( otaData.bConnected )
				{
					IotLogInfo( "OTA Agent Suspended. Resuming" );
					OTA_Resume( &otaData.connectionCtx );

					/* Get new MQTT Connection handle */

					otaData.connectionCtx.pvControlClient =	mqtt_getConnection();

					/*
					 * Agent is already running, use standard OTA Agent initialization function.
					 * This will clear OTA statistics for new connection.
					 */
					OTA_AgentInit( ( void * ) ( &otaData.connectionCtx ),
								   ( const uint8_t * ) ( otaData.pIdentifier ),
								   App_OTACompleteCallback,
								   ( TickType_t ) ~0 );

					/* transition to Run State */
					IotLogInfo( "ota -> Run" );
					otaData.taskState = eOtaTaskRun;
				}
				else
				{
					vTaskDelay( 1000 / portTICK_PERIOD_MS );
				}
				break;

			case eOtaTaskComplete:
				IotLogInfo( "OTA Agent Stopped. Disconnecting" );
				vTaskDelay( 100 / portTICK_PERIOD_MS );
				/* Try to close the MQTT connection. */
//				if( *pMqttConnection != NULL )
//				{
//					IotMqtt_Disconnect( *pMqttConnection, 0 );
//				}
				/* FIXME - Why would we disconnect the MQTT connection? What should we do or completion */
				break;
		}
	}
}

/*-----------------------------------------------------------*/
/* The OTA agent has completed the update job or determined that we're in
 * self test mode. If it was accepted, we want to activate the new image.
 * This typically means we should reset the device to run the new firmware.
 * If now is not a good time to reset the device, it may be activated later
 * by your user code. If the update was rejected, just return without doing
 * anything and we'll wait for another job. If it reported that we should
 * start test mode, normally we would perform some kind of system checks to
 * make sure our new firmware does the basic things we think it should do
 * but we'll just go ahead and set the image as accepted for demo purposes.
 * The accept function varies depending on your platform. Refer to the OTA
 * PAL implementation for your platform in aws_ota_pal.c to see what it
 * does for you.
 *
 * @param[in] eEvent Specify if this demo is running with the AWS IoT
 * MQTT server. Set this to `false` if using another MQTT server.
 * @return None.
 */

static void App_OTACompleteCallback( OTA_JobEvent_t eEvent )
{
    OTA_Err_t xErr = kOTA_Err_Uninitialized;


    /* OTA job is completed. so delete the MQTT and network connection. */
    if( eEvent == eOTA_JobEvent_Activate )
    {
        IotLogInfo( "Received eOTA_JobEvent_Activate callback from OTA Agent." );

        /* OTA job is completed. so delete the network connection. */
//        mqtt_disconnectMqttConnection();

        /* Activate New image, this function will only return if processing an update for a secondary processor */
    	OTA_ActivateNewImage();

     	IotLogInfo( "Secondary Processor Update, activated ... post semaphore" );

    	if( otaData.pHostUpdateComplete != NULL )
    	{
    		IotSemaphore_Post( otaData.pHostUpdateComplete );
    	}
    	else
    	{
    		IotLogError( "pHostUpdateComplete semaphore is NULL" );
    	}

        /* We should never get here as new image activation must reset the device.*/
//        IotLogError( "New image activation failed.\r\n" );
//
//        for( ; ; )
//        {
//        }
    }
    else if( eEvent == eOTA_JobEvent_Fail )
    {
    	IotLogError( "Received eOTA_JobEvent_Fail callback from OTA Agent." );
        /* Nothing special to do. The OTA agent handles it. */
    }
    else if( eEvent == eOTA_JobEvent_StartTest )
    {
        /* This demo just accepts the image since it was a good OTA update and networking
         * and services are all working (or we wouldn't have made it this far). If this
         * were some custom device that wants to test other things before calling it OK,
         * this would be the place to kick off those tests before calling OTA_SetImageState()
         * with the final result of either accepted or rejected. */
    	IotLogInfo( "Received eOTA_JobEvent_StartTest callback from OTA Agent." );
        xErr = OTA_SetImageState( eOTA_ImageState_Accepted );

        if( xErr != kOTA_Err_None )
        {
        	IotLogError( " Error! Failed to set image state as accepted.\r\n" );
        }
    }
}

/* ************************************************************************* */
/* ************************************************************************* */
/* **********        I N T E R F A C E   F U N C T I O N S        ********** */
/* ************************************************************************* */
/* ************************************************************************* */

/**
 * @brief	Initialize the OTA Update Task
 *
 * @param[in] pMqttConnection			Pointer to the MQTT connection
 * @param[in] pIdentifier				NULL-terminated MQTT client identifier.
 * @param[in] pNetworkCredentialInfo	Passed to the OTA Agent
 * @param[in] pNetworkInterface			The network interface used by the OTA Agent
 * @param[in] pSemaphore				Update Complete Semaphore
 *
 * @return `EXIT_SUCCESS` if the ota task is successfully created; `EXIT_FAILURE` otherwise.
 */

int OTAUpdate_init( 	const char * pIdentifier,
                            void * pNetworkCredentialInfo,
                            const IotNetworkInterface_t * pNetworkInterface,
							IotSemaphore_t *pSemaphore,
							hostOtaPendUpdateCallback_t function,
							const AltProcessor_Functions_t * altProcessorFunctions )
{
    int xRet = EXIT_SUCCESS;

    if( otaNETWORK_TYPES == AWSIOT_NETWORK_TYPE_NONE )
    {
    	IotLogError( "There are no networks configured for the OTA Update Task." );
        xRet = EXIT_FAILURE;
    }

    /* Save the Semaphore */
    otaData.pHostUpdateComplete = pSemaphore;
	IotLogInfo( "OTAUpdate_startTask: pHostUpdateComplete = %p", otaData.pHostUpdateComplete );

	/* Update the connection context shared with OTA Agent.*/
	otaData.connectionCtx.pxNetworkInterface = ( void * ) pNetworkInterface;
	otaData.connectionCtx.pvNetworkCredentials = pNetworkCredentialInfo;
	otaData.pIdentifier = pIdentifier;

	/* Save the callback function for pendingHostOtaUpdate */
	otaData.pendingHostOtaUpdate = function;

	/* Save the Alternate Processor PAL function table */
	otaData.hostPal = altProcessorFunctions;

	/* Create Task on new thread */
    xTaskCreate( _OTAUpdateTask, "ota_update", OTA_UPDATE_STACK_SIZE, NULL, OTA_UPDATE_TASK_PRIORITY, &otaData.taskHandle );

    if( NULL == otaData.taskHandle )
	{
    	xRet = EXIT_FAILURE;
    }
    else
    {
    	IotLogInfo( "ota_update created" );
    }

    return xRet;
}

