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
	[ eEventSubject_PowerOn ]					= "PowerOnEvent",
	[ eEventSubject_SystemAlive ]				= "SystemAlive",
	[ eEventSubject_DispenseReady ]				= "DispenseReady",
	[ eEventSubject_DispenseStart ]				= "DispenseStart",
	[ eEventSubject_DispenseComplete ]			= "DispenseComplete",
	[ eEventSubject_DispenseError ]				= "DispenseError",
	[ eEventSubject_HandleRaised ]				= "HandleRaised",
	[ eEventSubject_ImageCaptureComplete ]		= "CaptureComplete",
	[ eEventSubject_FillStart ]					= "FillStart",
	[ eEventSubject_CarbonationStart]			= "CabonationStart",
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
	[ eEventSubject_Critical_OPRecoveryError ]	= "CriticalError_OPRecovery"

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
