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
	eEventSubject_OtaUpdate,
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
	eEventSubject_OobeStart,
	eEventSubject_OobeFirmwareUpdate,
	eEventSubject_OobeReset,
	eEventSubject_OobeRinseStart,
	eEventSubject_OobeFillCarbonator,
	eEventSubject_OobeNoWater,
	eEventSubject_OobeSoak,
	eEventSubject_OobePress2Empty,
	eEventSubject_OobeEmptyCarbonator,
	eEventSubject_OobeEmptyAWT,
	eEventSubject_OobeFinalPurge,
	eEventSubject_OobeStage2Complete,
	eEventSubject_Clean_CleanFill,
	eEventSubject_Clean_CleanNoWater,
	eEventSubject_Clean_CleanRefill,
	eEventSubject_Clean_CleanSoak,
	eEventSubject_Clean_Press2Empty,
	eEventSubject_Clean_CleanEmptyCarb,
	eEventSubject_Clean_RinseFill,
	eEventSubject_Clean_RinseNoWater,
	eEventSubject_Clean_RinseRefill,
	eEventSubject_Clean_RinseSoak,
	eEventSubject_Clean_RinseEmptyAWT,
	eEventSubject_Clean_RinseEmptyCarb,
	eEventSubject_Clean_Complete,
	eEventSubject_Critical_PuncMechError,
	eEventSubject_Critical_ExtendedOPError,
	eEventSubject_Critical_ClearMemError,
	eEventSubject_Critical_OPRecoveryError,
	eEventSubject_EndOfList,
	eEventSubject_None
} _eventSubject_t;

/**
 * @brief Initialize Event Notification module
 *
 * @param[in] thingName		Pointer to ThingName string
 * @param[in] serialNumber	Pointer to SerialNumber string, optional
 * @param[in] initExtend	Extended Initialization function, optional
 */
void eventNotification_Init( const char *thingName, const char *serialNumber, _initializeCallback_t initExtend );

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
