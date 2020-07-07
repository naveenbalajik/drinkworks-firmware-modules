/**
 * @file dw_test_ble_gatt_server.c
 */
 
#include "iot_ble.h"
#include "../DwBleInterface_internal.h"
#include "../DwBleInterface.h"

#include "dw_test_ble_gatt_server.h"
 
static const uint8_t    SernNum[] = { '0', '1', 'A', 'B', '2', '3', 'C', 'B', '4', '5', '6', '7' }; 
static const uint8_t    FwIdStr[] = "ESP32 Demo firmware";
static const uint16_t   PowerUpEvts = 17;
static const uint16_t	RunHours = 18;
static const uint16_t	StateSubstate = 19;
static const uint8_t	TFSubstate = 20;
static const uint16_t	CWTTemp = 21;
static const uint8_t	AllowCarb = 1;
static const uint8_t	DwEvent[] = "DwEvent [Test Data]";
static const uint16_t	StillBev = 22;
static const uint16_t	CarbBev = 23;
static const uint16_t	CleanCycles = 24;
static const uint32_t	UVLEDminutes = 25;
static const uint16_t	MinutesToReady = 26;
static const uint16_t	ImagerErrCnt = 27;
static const uint16_t	RecogErrCnt = 28;
static const uint16_t	FillTOErrCnt = 29;
static const uint16_t	OverPressureErrCnt = 30;
static const uint16_t	TopOfTankErrCnt = 31;
static const uint16_t	CarbTOErrCnt = 32;
static const uint16_t	HandleLiftErrCnt = 33;
static const uint16_t	PunctMechErrCnt = 34;
static const uint16_t	CO2MechErrCnt = 35;
static const uint32_t	RecordIndex = 129;
static const uint8_t	RecordData[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
static const uint32_t	RecordCount = 513;

static const uint16_t	CO2Status = 0x77;
static const uint8_t	HygieneStatus[] = { 0x13, 0x0c, 0x0c, 0x04, 0x10, 0x1b, 0x00, 0x01 };

static const uint8_t	FirmwareVersion[] = "2.30";

static const uint8_t	OtaCommand[] = { 0x04, 0x02, 0xec, 0x86 };
static const uint8_t	OtaStatus[] = { 0x05, 0x42, 0x00, 0x80, 0x5e };

static const uint8_t	CurrentTime[] = { 0xe4, 0x07, 0x02, 0x19, 0x12, 0x35, 0x2e, 0x02, 0x00, 0x00 };		/* 2019 Feb 25 18:53:46 Tuesday */
static const uint8_t	LocalTimeInfo[] = { 0x80, 0xFF };
static const uint8_t	ReferenceTimeInfo[] = { 0x00, 0xFF, 0x00, 0x00 };

//#define	TEST_BUFFER_SIZE	40
//static char TestBuffer[TEST_BUFFER_SIZE];

 void DwTestBleGattServer_Init( void )
 {
	 /* Initiailize characteristics with known constant values */
/*
	DwBleInterface_SetCharacteristicValueRemote(  SERIAL_NUMBER_HANDLE, ( uint8_t * ) SernNum, sizeof( SernNum ) );
	 
	DwBleInterface_SetCharacteristicValueRemote(  FIRMWARE_ID_HANDLE, ( uint8_t * ) FwIdStr, sizeof( FwIdStr ) );
	DwBleInterface_SetCharacteristicValueRemote(  POWER_UP_EVENTS_HANDLE, ( uint8_t * ) &PowerUpEvts, sizeof( PowerUpEvts ) );
	DwBleInterface_SetCharacteristicValueRemote(  RUN_HOURS_HANDLE, ( uint8_t * ) &RunHours, sizeof( RunHours ) );
	DwBleInterface_SetCharacteristicValueRemote(  STATE_SUBSTATE_HANDLE, ( uint8_t * ) &StateSubstate, sizeof( StateSubstate ) );
	DwBleInterface_SetCharacteristicValueRemote(  TF_SUBSTATE_HANDLE, ( uint8_t * ) &TFSubstate, sizeof( TFSubstate ) );
	DwBleInterface_SetCharacteristicValueRemote(  CURRENT_CWT_HANDLE, ( uint8_t * ) &CWTTemp, sizeof( CWTTemp ) );
	DwBleInterface_SetCharacteristicValueRemote(  ALLOW_CARB_HANDLE, ( uint8_t * ) &AllowCarb, sizeof( AllowCarb ) );
	DwBleInterface_SetCharacteristicValueRemote(  DW_EVENT_HANDLE, ( uint8_t * ) DwEvent, sizeof( DwEvent ) );
	DwBleInterface_SetCharacteristicValueRemote(  STILL_HANDLE, ( uint8_t * ) &StillBev, sizeof( StillBev ) );
	DwBleInterface_SetCharacteristicValueRemote(  CARB_HANDLE, ( uint8_t * ) &CarbBev, sizeof( CarbBev ) );
	DwBleInterface_SetCharacteristicValueRemote(  CLN_CYC_COUNT_HANDLE, ( uint8_t * )&CleanCycles, sizeof( CleanCycles ) );
	DwBleInterface_SetCharacteristicValueRemote(  UV_ON_MINS_HANDLE, ( uint8_t * ) &UVLEDminutes, sizeof( UVLEDminutes ) );
	DwBleInterface_SetCharacteristicValueRemote(  MINS_TO_READY_HANDLE, ( uint8_t * ) &MinutesToReady, sizeof( MinutesToReady ) );
	DwBleInterface_SetCharacteristicValueRemote(  IMAGE_ERR_HANDLE, ( uint8_t * ) &ImagerErrCnt, sizeof( ImagerErrCnt ) );
	DwBleInterface_SetCharacteristicValueRemote(  RECOG_ERR_HANDLE, ( uint8_t * ) &RecogErrCnt, sizeof( RecogErrCnt ) );
	DwBleInterface_SetCharacteristicValueRemote(  FILL_TO_ERR_HANDLE, ( uint8_t * ) &FillTOErrCnt, sizeof( FillTOErrCnt ) );
	DwBleInterface_SetCharacteristicValueRemote(  OVER_PRESS_ERR_HANDLE, ( uint8_t * ) &OverPressureErrCnt, sizeof( OverPressureErrCnt ) );
	DwBleInterface_SetCharacteristicValueRemote(  TOP_OF_TANK_ERR_HANDLE, ( uint8_t * ) &TopOfTankErrCnt, sizeof( TopOfTankErrCnt ) );
	DwBleInterface_SetCharacteristicValueRemote(  CARB_TO_ERR_HANDLE, ( uint8_t * ) &CarbTOErrCnt, sizeof( CarbTOErrCnt ) );
	DwBleInterface_SetCharacteristicValueRemote(  HANDLE_LIFT_ERR_HANDLE, ( uint8_t * ) &HandleLiftErrCnt, sizeof( HandleLiftErrCnt ) );
	DwBleInterface_SetCharacteristicValueRemote(  DELAY_PUNC_ERR_HANDLE, ( uint8_t * ) &PunctMechErrCnt, sizeof( PunctMechErrCnt ) );
	
	DwBleInterface_SetCharacteristicValueRemote(  CO2_CTRL_ERR_HANDLE, ( uint8_t * ) &CO2MechErrCnt, sizeof( CO2MechErrCnt ) );
	DwBleInterface_SetCharacteristicValueRemote(  RECORD_INDEX_HANDLE, ( uint8_t * ) &RecordIndex, sizeof( RecordIndex ) );
	DwBleInterface_SetCharacteristicValueRemote(  DISPENSE_RECORD_HANDLE, ( uint8_t * ) RecordData, sizeof( RecordData ) );
	DwBleInterface_SetCharacteristicValueRemote(  RECORD_COUNT_HANDLE, ( uint8_t * ) &RecordCount, sizeof( RecordCount ) );
*/
	bleInterface_SetCharacteristicValueRemote(  CO2_STATUS_HANDLE, ( uint8_t * ) &CO2Status, sizeof( CO2Status ) );
	bleInterface_SetCharacteristicValueRemote(  LAST_CLEANING_TIME, ( uint8_t * ) HygieneStatus, sizeof( HygieneStatus ) );
/*
	DwBleInterface_SetCharacteristicValueRemote(  FIRMWARE_VERS_HANDLE, ( uint8_t * ) FirmwareVersion, sizeof( FirmwareVersion ) );
	DwBleInterface_SetCharacteristicValueRemote(  OTA_COMMAND_HANDLE, ( uint8_t * ) OtaCommand, sizeof( OtaCommand ) );
	DwBleInterface_SetCharacteristicValueRemote(  OTA_RESPONSE_HANDLE, ( uint8_t * ) OtaStatus, sizeof( OtaStatus ) );

	DwBleInterface_SetCharacteristicValueRemote(  CURRENT_TIME_HANDLE, ( uint8_t * ) CurrentTime, sizeof( CurrentTime ) );
	DwBleInterface_SetCharacteristicValueRemote(  LOCAL_TIME_INFO_HANDLE, ( uint8_t * ) LocalTimeInfo, sizeof( LocalTimeInfo ) );
	DwBleInterface_SetCharacteristicValueRemote(  REFERENCE_TIME_INFO_HANDLE, ( uint8_t * ) ReferenceTimeInfo, sizeof( ReferenceTimeInfo ) );
*/	
	
	
//    DwBleInterface_GetCharacteristicValueRemote(  0x8004, &TestBuffer, ( uint16_t )( TEST_BUFFER_SIZE ) );

 }