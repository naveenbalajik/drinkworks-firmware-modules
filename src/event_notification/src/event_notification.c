/**
 *  @file event_notification.c
 */

#include <stdint.h>
#include <string.h>
#include "event_notification.h"
#include "mqtt.h"

/**
 * @brief Max topic name length
 */
#define	MAX_TOPIC_LEN	128


/**
 * @brief Event Subject strings
 *
 * All subject strings should be defined here and accesses via function:
 * eventNotification_getSubject()
 */
static const char * _subjectString[] =
{
	[ eEventSubject_PowerOn ] = "PowerOnEvent",
	[ eEventSubject_SystemAlive ] = "SystemAlive",
	[ eEventSubject_DispenseReady ] = "DispenseReady",
	[ eEventSubject_DispenseStart ] = "DispenseStart",
	[ eEventSubject_DispenseComplete ] = "DispenseComplete",
	[ eEventSubject_DispenseError ] = "DispenseError",
	[ eEventSubject_HandleRaised ] = "HandleRaised",
	[ eEventSubject_ImageCaptureComplete ] = "CaptureComplete",
	[ eEventSubject_FillStart ] = "FillStart",
	[ eEventSubject_CarbonationStart] = "CabonationStart",
	[ eEventSubject_PourStart ] = "PourStart",
	[ eEventSubject_PicUpdate ] = "PICupdate",
	[ eEventSubject_NoWater ] = "NoWater",
	[ eEventSubject_NoCO2Available ] = "NoCO2Available",
	[ eEventSubject_OverPressure ] = "OverPressure",
	[ eEventSubject_CarbonationTimeout ] = "CarbonationTimeout",
	[ eEventSubject_PmError ] = "PunctureMechError",
	[ eEventSubject_RecoveryStart ] = "RecoveryStart"
};

/**
 * @brief	Event Notification local data structure
 */
typedef struct
{
	char 	topicName[ MAX_TOPIC_LEN ];					/**< Event Notification topic name */
	size_t	topicSize;										/**< Event Notification topic name length */
} _eventNofify_t;

/**
 * @brief	Suffix, added to the ThingName, to form the Event Notification MQTT topic
 */
static const char _topicSuffix[] = "/Events";

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
		if( eventNotify.topicSize )
		{
			mqtt_SendMsgToTopic( eventNotify.topicName, eventNotify.topicSize, pJSON, strlen( pJSON ), NULL );
		}
	}
}


/**
 * @brief Initialize Event Notification module
 *
 * @param[in] thingName		Pointer to ThingName string
 * @param[in] initExtend	Extended Initialization function, optional
 */
void eventNotification_Init( const char *thingName, _initializeCallback_t initExtend )
{
	if( NULL != thingName )
	{
		/* Make sure topic name will fit in buffer */
		if( MAX_TOPIC_LEN > ( strlen( thingName ) + strlen( _topicSuffix ) ) )
		{
			strcpy( eventNotify.topicName, thingName );									/* Copy ThingName */
			strcat( eventNotify.topicName, _topicSuffix );								/* Append suffix */
			eventNotify.topicSize = strlen( eventNotify.topicName );					/* save topic name length */

			/* Call any extended initialization function */
			if( NULL != initExtend )
			{
				initExtend();
			}
		}
	}
}

/**
 * @brief	Send a pre-formatted JSON message to the Thing's Event Topic
 *
 * JSON message must be in the appropriate format, namely:
 *	"{ event: {	param1: value, param2: value, ... }	}"
 *
 * @param[in] eventOutputJSON	pointer to JSON message buffer
 */
void eventNotification_SendEvent( char *pJson )
{
	/* TODO - valid JSON format validation */
	_sendToEventTopic( pJson );
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
