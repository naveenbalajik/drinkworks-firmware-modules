/**
 * @file	event_records.h
 *
 * Created on: September 3, 2020
 * Author: Ian Whitehead
 */

#ifndef	EVENT_RECORDS_H
#define	EVENT_RECORDS_H

#include	"event_fifo.h"
#include	"nvs_utility.h"

/**
 * @brief	Enumerated Dispense Record Status
 */
enum RecordStatus
{
	eNoError =								0x00,
	eUnknown_Error =						0x01,
	eTop_of_Tank_Error =					0x02,
	eCarbonator_Fill_Timeout_Error =		0x03,
	eOver_Pressure_Error =					0x04,
	eCarbonation_Timeout_Error =			0x05,
	eError_Recovery_Brew =					0x06,
	eHandle_Lift_Error =					0x07,
	ePuncture_Mechanism_Error =				0x08,
	eCarbonation_Mechanism_Error =			0x09,
	eWaitForWater_Timeout_Error =			0x0A,
	eCleaning_Cycle_Completed =				0x80,
	eRinsing_Cycle_Completed =				0x81,
	eCO2_Module_Attached =					0x82,
	eFirmware_PIC_Update_Passed =			0x83,		/**< PIC firmware update successful */
	eFirmware_PIC_Update_Failed =			0x84,		/**< PIC firmware update failed */
	eDrain_Cycle_Complete =					0x85,
	eFreezeEventUpdate =					0x86,
	eCritical_Error_OverTemp =				0x87,
	eCritical_Error_PuncMechFail =			0x88,
	eCritical_Error_TrickleFillTmout =		0x89,
	eCritical_Error_ClnRinCWTFillTmout =	0x8A,
	eCritical_Error_ExtendedOPError = 		0x8B,
	eCritical_Error_BadMemClear = 			0x8C,
	eCritical_Error_OPRecoveryError =		0x8D,		/**< Critical Error: Over-pressure Recovery Error */
	eFirmware_ESP_Update_Passed =			0x90,		/**< ESP32 firmware update successful */
	eFirmware_ESP_Update_Failed =			0x91,		/**< ESP32 firmware update failed */
	eBLE_ModuleReset = 						0xE0,		/**< Module reset detected by comparing Firmware Version Characteristic */
	eBLE_IdleStatus =						0xE1,		/**< Module reporting Idle Status, unexpectedly */
	eBLE_StandbyStatus =					0xE2,		/**< Module reporting Standby Status, unexpectedly */
	eBLE_ConnectedStatus =					0xE3,		/**< Module reporting Connected Status, unexpectedly */
	eBLE_HealthTimeout =					0xE4,		/**< Module did not respond to a health check within timeout period */
	eBLE_ErrorState =						0xE5,		/**< Resetting from Error State, entered from CONFIG or SETUP */
	eBLE_MultiConnectStat =					0xE6,		/**< Multiple Connected Status messages received when in WaitForConnection state */
	eBLE_MaxCriticalTimeout =				0xE7,		/**< Consecutive timeouts of critical response commands exceeds threshold */
	eStatusUnknown =						0xFF
};

int32_t eventRecords_init( fifo_handle_t fifo, NVS_Items_t eventRecordKey );

void eventRecords_onChangedTopic( int32_t lastRecordedEvent );

const char * eventRecords_statusText( uint8_t status);

void eventRecords_saveRecord( char * pInput );

#endif		/*	EVENT_RECORDS_H	*/
