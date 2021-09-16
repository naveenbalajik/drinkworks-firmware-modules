/**
 *  @file event_notification.c
 */

#include <stdint.h>
#include <string.h>
#include "event_notification.h"
#include "mqtt.h"
#include "mjson.h"
#include "app_logging.h"			// FIXME

/**
 * @brief Max topic name length
 */
#define	MAX_TOPIC_LEN	128

/**
 * @brief Serial Number Buffer Length
 */
#define	SER_NUM_BUF_LEN	13


/**
 * @brief Event Subject strings
 *
 * All subject strings should be defined here and accesses via function:
 * eventNotification_getSubject()
 */
static const char * _subjectString[] =
{
	[ eEventSubject_PowerOn ]					= "PowerOnEvent",
	[ eEventSubject_SystemAlive ]				= "SystemAlive",
	[ eEventSubject_DispenseReady ]				= "DispenseReady",
	[ eEventSubject_DispenseStart ]				= "DispenseStart",
	[ eEventSubject_DispenseComplete ]			= "DispenseComplete",
	[ eEventSubject_DispenseError ]				= "DispenseError",
	[ eEventSubject_HandleRaised ]				= "HandleRaised",
	[ eEventSubject_ImageCaptureComplete ]		= "CaptureComplete",
	[ eEventSubject_FillStart ]					= "FillStart",
	[ eEventSubject_CarbonationStart]			= "CarbonationStart",
	[ eEventSubject_PourStart ]					= "PourStart",
	[ eEventSubject_OtaUpdate ]					= "OTAupdate",
	[ eEventSubject_PicUpdate ]					= "PICupdate",
	[ eEventSubject_NoWater ]					= "NoWater",
	[ eEventSubject_NoCO2Available ]			= "NoCO2Available",
	[ eEventSubject_OverPressure ]				= "OverPressure",
	[ eEventSubject_CarbonationTimeout ]		= "CarbonationTimeout",
	[ eEventSubject_PmError ]					= "PunctureMechError",
	[ eEventSubject_RecoveryStart ]				= "RecoveryStart",
	[ eEventSubject_OobeStart ]					= "OobeStart",
	[ eEventSubject_OobeFirmwareUpdate ]		= "OobeFirmwareUpdate",
	[ eEventSubject_OobeReset ]					= "OobeReset",
	[ eEventSubject_OobeRinseStart ]			= "OobeRinseStart",
	[ eEventSubject_OobeFillCarbonator ]		= "OobeFillCarbonator",
	[ eEventSubject_OobeNoWater ]				= "OobeNoWater",
	[ eEventSubject_OobeSoak ]					= "OobeSoak",
	[ eEventSubject_OobePress2Empty ]			= "OobePress2Empty",
	[ eEventSubject_OobeEmptyCarbonator]		= "OobeEmptyCarbonator",
	[ eEventSubject_OobeEmptyAWT]				= "OobeEmptyAWT",
	[ eEventSubject_OobeFinalPurge ]			= "OobeFinalPurge",
	[ eEventSubject_OobeStage2Complete ]		= "OobeStage2Complete",
	[ eEventSubject_Clean_CleanFill ]			= "Clean_CleanFill",
	[ eEventSubject_Clean_CleanNoWater ]		= "Clean_CleanNoWater",
	[ eEventSubject_Clean_CleanRefill ]			= "Clean_CleanRefill",
	[ eEventSubject_Clean_CleanSoak ]			= "Clean_CleanSoak",
	[ eEventSubject_Clean_Press2Empty ]			= "Clean_Press2Empty",
	[ eEventSubject_Clean_CleanEmptyCarb ]		= "Clean_CleanEmptyCarb",
	[ eEventSubject_Clean_RinseFill ]			= "Clean_RinseFill",
	[ eEventSubject_Clean_RinseNoWater ]		= "Clean_RinseNoWater",
	[ eEventSubject_Clean_RinseRefill ]			= "Clean_RinseRefill",
	[ eEventSubject_Clean_RinseSoak ]			= "Clean_RinseSoak",
	[ eEventSubject_Clean_RinseEmptyAWT ]		= "Clean_RinseEmptyAWT",
	[ eEventSubject_Clean_RinseEmptyCarb ]		= "Clean_RinseEmptyCarb",
	[ eEventSubject_Clean_Complete ]			= "Clean_Complete",
	[ eEventSubject_Critical_PuncMechError ]	= "CriticalError_PM",
	[ eEventSubject_Critical_ExtendedOPError ]	= "CriticalError_ExtendedOP",
	[ eEventSubject_Critical_ClearMemError ]	= "CriticalError_ClearMem",
	[ eEventSubject_Critical_OPRecoveryError ]	= "CriticalError_OPRecovery",
	[ eEventSubject_Idle ] 						= "Idle",
	[ eEventSubject_Sleep ] 					= "Sleep"

};

/**
 * @brief	Event Notification local data structure
 */
typedef struct
{
	char 	topicNameEvent[ MAX_TOPIC_LEN ];				/**< Event Notification topic name */
	size_t	topicSizeEvent;									/**< Event Notification topic name length */
	char 	topicNameFeedback[ MAX_TOPIC_LEN ];				/**< Feedback topic name */
	size_t	topicSizeFeedback;								/**< Feedback topic name length */
	char	serialNumber[ SER_NUM_BUF_LEN ];				/**< Serial Number */
	const	_feedbackSubject_t	* pFeedbackSubject;			/**< Feedback Subject Table */
	uint32_t				subjectCount;			/**< Number of Subjects in Subject Table */
} _eventNofify_t;

/**
 * @brief	Suffix, added to the ThingName, to form the Event Notification MQTT topic
 */
static const char _topicSuffixEvent[] = "/Events";

/**
 * @brief	Suffix, added to the ThingName, to form the Feedback MQTT topic
 */
static const char _topicSuffixFeedback[] = "/Feedback";

static _eventNofify_t eventNotify;



/**
 * @brief	Send a pre-formatted JSON message to the Thing's Event Topic
 *
 * @param[in] pJSON	pointer to JSON message buffer
 */
static void _sendToEventTopic( char *pJSON )
{

	if( mqtt_IsConnected() )
	{
		if( eventNotify.topicSizeEvent )
		{
			mqtt_SendMsgToTopic( eventNotify.topicNameEvent, eventNotify.topicSizeEvent, pJSON, strlen( pJSON ), NULL );
		}
	}
}

/**
 * @brief Callback from feedback subscription
 *
 * @param[in] param1	Semaphore for provisioning request
 * @param[in] pPublish	Information about the completed operation passed by the MQTT library.
 */
static void _feedbackSubscriptionCallback( void* param1, IotMqttCallbackParam_t* const pPublish )
{
//	IotSemaphore_t* pPublishesReceived = ( IotSemaphore_t * )param1;
	const char* pPayload = pPublish->u.message.info.pPayload;
	int payloadLength = pPublish->u.message.info.payloadLength;
	char buffer[ 64 ];

	IotLogInfo( "Message received from topic:%.*s", pPublish->u.message.topicFilterLength, pPublish->u.message.pTopicFilter );

	// Ensure that the message came in on the expected topic
	if( memcmp( pPublish->u.message.pTopicFilter, eventNotify.topicNameFeedback, eventNotify.topicSizeFeedback ) == 0 )
	{
		const char *val;
		int len = 0;

		// Check for Subject table
		if( eventNotify.pFeedbackSubject != NULL )
		{
			uint32_t	i;
			const _feedbackSubject_t *pFeedback;

			// Look for the "subject" in the message, this will be a string token
			len = mjson_get_string( pPayload, payloadLength, "$.subject", buffer, sizeof( buffer ) );
			if( len != -1 )
			{
//				printf("found subject: %.*s\n", len, buffer );
				// Iterate through subject table
				for (i = 0; i < eventNotify.subjectCount; ++i )
				{
					pFeedback = &eventNotify.pFeedbackSubject[ i ];
					printf("Looking for: %s\n", pFeedback->subject );
					// Look for matching subject
					if( strncmp( buffer, pFeedback->subject, len) == 0 )
					{
						IotLogInfo( "Found registered subject: %s", pFeedback->subject );

						// Look for the "data" in the message, this will be a JSON Object token
						if( MJSON_TOK_OBJECT == mjson_find( pPayload, payloadLength, "$.data", &val, &len ) )
						{
							IotLogInfo( "Found data: %.*s", len, val );

							// invoke callback, if valid
							if( pFeedback->callback != NULL )
							{
								pFeedback->callback( val, len );
							}
							else
							{
								IotLogError( "No callback function" );
							}
						}
						else
						{
							IotLogError( "Data key not present" );
						}
						break;							// terminate on subject match
					}
				}

			}
			else
			{
				IotLogError( "Subject key not present" );
			}
		}
		else
		{
			IotLogError( "No Feedback Subject table" );
		}
	}
	else
	{
		IotLogError( "Message Received on unexpected topic" );
	}
}


/**
 * @brief Initialize Event Notification module
 *
 * @param[in] thingName		Pointer to ThingName string
 * @param[in] serialNumber	Pointer to SerialNumber string, optional
 * @param[in] initExtend	Extended Initialization function, optional
 */
void eventNotification_Init( const char *thingName, const char *serialNumber, _initializeCallback_t initExtend )
{
	printf( "\n\n*****  eventNotification_Init  *****\n\n" );
	IotLogInfo( "eventNotification_Init" );
	if( NULL != thingName )
	{
		/* Make sure Event topic name will fit in buffer */
		if( MAX_TOPIC_LEN > ( strlen( thingName ) + strlen( _topicSuffixEvent ) ) )
		{
			strcpy( eventNotify.topicNameEvent, thingName );									/* Copy ThingName */
			strcat( eventNotify.topicNameEvent, _topicSuffixEvent );							/* Append suffix */
			eventNotify.topicSizeEvent = strlen( eventNotify.topicNameEvent );					/* save topic name length */

			/* Call any extended initialization function */
			if( NULL != initExtend )
			{
				initExtend();
			}
		}

		/* Make sure Feedback topic name will fit in buffer */
		if( MAX_TOPIC_LEN > ( strlen( thingName ) + strlen( _topicSuffixFeedback ) ) )
		{
			strcpy( eventNotify.topicNameFeedback, thingName );									/* Copy ThingName */
			strcat( eventNotify.topicNameFeedback, _topicSuffixFeedback );						/* Append suffix */
			eventNotify.topicSizeFeedback = strlen( eventNotify.topicNameFeedback );			/* save topic name length */

			printf( "eventNotification_Init, feedback topic: %s\n", eventNotify.topicNameFeedback );
			IotLogInfo( "eventNotification_Init, feedback topic: %s", eventNotify.topicNameFeedback );
			/* Subscribe to Feedback topic */
			if( mqtt_subscribeTopic( eventNotify.topicNameFeedback, _feedbackSubscriptionCallback, NULL ) == IOT_MQTT_SUCCESS )
			{
				printf( "Feedback topic subscription: success\n" );
				IotLogInfo( "Feedback topic subscription: success" );
			}

		}
	}

	if( NULL != serialNumber )
	{
		strncpy( eventNotify.serialNumber, serialNumber, SER_NUM_BUF_LEN );
	}
}

/**
 * @brief	Send a pre-formatted JSON message to the Thing's Event Topic
 *
 * JSON message must be in the appropriate format, namely:
 *	"{ event: {	param1: value, param2: value, ... }	}"
 *
 *	If a serial number was provided at module initialization, notification includes the serial number.
 *
 *	Caller must free buffer after function returns
 *
 * @param[in] eventOutputJSON	pointer to JSON message buffer
 */
void eventNotification_SendEvent( char *pJson )
{
	if( NULL != pJson )
	{
		/* Prepend serial number to message */
		if( NULL != eventNotify.serialNumber )
		{
			char * pHeader = NULL;
			char * pCombined = NULL;
			int32_t	lenHeader= 0;

			/* Format header */
			lenHeader = mjson_printf( &mjson_print_dynamic_buf, &pHeader,
					"{%Q:%Q}",
					"serialNumber", eventNotify.serialNumber );

			/* Merge the head and input buffers */
			mjson_merge( pHeader, lenHeader, pJson, strlen( pJson), mjson_print_dynamic_buf, &pCombined );

			vPortFree( pHeader );								// Free Header buffer

			_sendToEventTopic( pCombined );						// Send notification

			vPortFree( pCombined );								// Free combined buffer
		}
		else
		{
			/* TODO - valid JSON format validation */
			_sendToEventTopic( pJson );
		}
	}
}

/**
 * @brief	Get Event Subject string
 *
 * @param[in]	subject		Enumerated Event Subject
 * @return		Event Subject string; NULL if subject is invalid
 */
const char * eventNotification_getSubject( _eventSubject_t subject )
{
	if( eEventSubject_EndOfList > subject )
	{
		return _subjectString[ subject ];
	}
	else
	{
		return NULL;
	}
}

/**
 * @brief	Register Feedback Subject Table
 *
 * Feedback Subject Table contains the Subject string and callback function for each subject being registered
 *
 * @param[in] pSubjectTable		Pointer to Feedback subject table.
 * @param[in] subjectCount		Number of Subjects in table
 */
void eventNotification_RegisterFeedbackSubjects( const _feedbackSubject_t * pSubjectTable, const uint32_t subjectCount  )
{
	eventNotify.pFeedbackSubject = pSubjectTable;				// Save pointer to feedback subject table
	eventNotify.subjectCount = subjectCount;					// Save Number of subjects
}
