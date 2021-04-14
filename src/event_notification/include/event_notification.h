/**
 *  @file event_notification.h
 */
 
#ifndef	_EVENT_NOTIFICATION_H_
#define	_EVENT_NOTIFICATION_H_

/**
 * @brief Generic Initialize Callback type
 *
 * Can be used to extend a module's initialization function
 */
typedef void (* _initializeCallback_t)( void );


/**
 * @brief Enumerated Event Subjects
 *
 * These enumerations are used to lookup the Event Subject string in
 * eventNotification_getSubject().
 *
 * The first 6 entries map to DW Events on Model-A.
 */
typedef enum
{
	eEventSubject_PowerOn = 0,
	eEventSubject_SystemAlive,
	eEventSubject_DispenseReady,
	eEventSubject_DispenseStart,
	eEventSubject_DispenseComplete,
	eEventSubject_DispenseError,
	eEventSubject_PicUpdate,
	eEventSubject_HandleRaised,
	eEventSubject_ImageCaptureComplete,
	eEventSubject_FillStart,
	eEventSubject_CarbonationStart,
	eEventSubject_PourStart,
	eEventSubject_NoWater,
	eEventSubject_NoCO2Available,
	eEventSubject_OverPressure,
	eEventSubject_CarbonationTimeout,
	eEventSubject_PmError,
	eEventSubject_RecoveryStart,
	eEventSubject_EndOfList,
} _eventSubject_t;

/**
 * @brief Initialize Event Notification module
 *
 * @param[in] thingName		Pointer to ThingName string
 * @param[in] initExtend	Extended Initialization function, optional
 */
void eventNotification_Init( const char *thingName, _initializeCallback_t initExtend );

/**
 * @brief	Send a pre-formatted JSON message to the Thing's Event Topic
 */
void eventNotification_SendEvent( char *pJson );

/**
 * @brief	Get Event Subject string
 *
 * @param[in]	subject		Enumerated Event Subject
 * @return		Event Subject string; NULL if subject is invalid
 */
const char * eventNotification_getSubject( _eventSubject_t subject );

#endif /* _EVENT_NOTIFICATION_H_ */
