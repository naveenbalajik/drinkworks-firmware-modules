/**
 * @file bleGap.c
 *
 *  Created on: Sep 17, 2019
 *      Author: ian.whitehead
 */


/* The config header is always included first. */
#include "iot_config.h"

#include <string.h>
#include "ble_logging.h"
#include "iot_ble_config.h"
#include "iot_ble.h"
#include "iot_ble_device_information.h"
//#include "bleGattTable.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "bleGap.h"

#include "nvs_utility.h"

static uint8_t _serialNumber[12] = { '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0' };
char	completeDeviceName[24] = "Drinkworks";

static const BTUuid_t _dwAdvUUID =
{
    .uu.uu128 = DW_SERVICE_UUID,
    .ucType   = eBTuuidType128
};

/**
 * @brief Contains custom parameters to be set in the scan response data.
 *
 * Note that total available data size in scan response
 * is 31 bytes. Parameters are chosen below such that overall size
 * does not exceed 31 bytes.
 */
static IotBleAdvertisementParams_t _dwScanRespParams =
{
    .includeTxPower    = true,
    .name              =
    {
        BTGattAdvNameComplete,
        0
    },
    .setScanRsp        = true,
    .appearance        = IOT_BLE_ADVERTISING_APPEARANCE,
    .minInterval       = 0, //IOT_BLE_ADVERTISING_CONN_INTERVAL_MIN,
    .maxInterval       = 0, //IOT_BLE_ADVERTISING_CONN_INTERVAL_MAX,
    .serviceDataLen    = 0,
    .pServiceData      = NULL,
    .manufacturerLen   = 0,
    .pManufacturerData = NULL,
    .pUUID1            = NULL,
    .pUUID2            = NULL
};

/**
 * @brief Contains custom parameters to be set in the advertisement data.
 *
 * Note that total available data size in advertisement
 * is 31 bytes. Parameters are chosen below such that overall size
 * does not exceed 31 bytes.
 *
 * prvBTSetAdvData() sets the advertising data, based on this structure.
 *   deviceName is set by IOT_BLE_DEVICE_COMPLETE_LOCAL_NAME in iot_ble_config.h
 *   flags are set to 0x06 (LE General Discovery Mode, BR/EDR not supported)
 */

static IotBleAdvertisementParams_t _dwAdvParams =
{
    .includeTxPower    = false,
    .name              =
	{
	    BTGattAdvNameNone,
	    0
	},
    .setScanRsp        = false,
    .appearance        = IOT_BLE_ADVERTISING_APPEARANCE,
    .minInterval       = 0,
    .maxInterval       = 0,
    .serviceDataLen    = 0,
    .pServiceData      = NULL,
    .manufacturerLen   = 0,
    .pManufacturerData = NULL,
    .pUUID1            = ( BTUuid_t * ) &_dwAdvUUID,
    .pUUID2            = NULL
};

/**
 * @brief Store Serial Number in NV Storage
 *
 * Serial Number is stored using enumerated NV_Items_t <i>NVS_SERIAL_NUM</i>.
 * Value is only stored if not already present in NV memory
 * Input serial number is an un-terminated 12 byte array.  The value saved as a Blob.
 *
 * @param[in] pSerialNumber pointer to serial number array.
 * @param[in] size			size of serial number
 */
static void _storeSerialNumber( const char *pSerialNumber, uint16_t size )
{
    esp_err_t xRet;
    size_t	sSize = ( size_t )size;

	xRet = NVS_Set( NVS_SERIAL_NUM, (void *)pSerialNumber, &sSize );

	if( xRet == ESP_OK )
	{
		IotLogInfo("storedSerialNumber(%.*s) - set OK", size, pSerialNumber );
	}
}

/**
 * @brief Set Custom Advertising Data callback
 *
 * The implementation of this function need to be given when IOT_BLE_SET_CUSTOM_ADVERTISEMENT_MSG is true
 * and when the user needs to set his own advertisement/scan response message.
 *
 * Scan Response data includes the <i>Complete Name</i> item, which is filled from the global variable
 * <i>completeDeviceName</i>.  This global variable must be set prior to this function being called.
 *
 * @param[out] pAdvParams: Advertisement structure. Needs to be filled by the user.
 * @param[out] pScanParams: Scan response structure. Needs to be filled by the user.
 *
 */
void IotBle_SetCustomAdvCb( IotBleAdvertisementParams_t * pAdvParams, IotBleAdvertisementParams_t * pScanParams )
{
	IotLogInfo("IotBle_SetCustomAdvCb: %s", completeDeviceName);

	*pAdvParams = _dwAdvParams;
	*pScanParams = _dwScanRespParams;
}


/**
 * @brief	Set Serial Number and Device Name
 *
 * Serial number array is copied into local array <i>_serialNumber</i>
 *
 * Array is also appended to fixed <b>Drinkworks</b> string to form <i>completeDeviceName</b>.
 * This is used for both the Scan Response Data and the Device Name characteristic (0x2A00)
 * in the Generic Access Service (0x1800)
 *
 * @param[in]	pSerialNumber	Pointer to serial number array, 12-byte length, unterminated
 * @param[in]	size			Number of bytes in serial number array
 */
void bleGap_setSerialNumberAndDeviceName( const uint8_t *pSerialNumber, const uint16_t size )
{
	_storeSerialNumber( ( char * )pSerialNumber, size );							// store Serial Number in NV storage

	memcpy( _serialNumber, pSerialNumber, sizeof( _serialNumber ) );
	completeDeviceName[10] = ' ';
	memcpy( &completeDeviceName[11], _serialNumber, sizeof( _serialNumber ) );
	completeDeviceName[23] = '\0';
	IotLogInfo( "bleGap_setSerialNumberAndDeviceName: %s", completeDeviceName );
}

/**
 * @brief	Restore Serial Number and Device Name using saved NVS value
 */
uint8_t * bleGap_restoreSerialNumberAndDeviceName( void )
{
    size_t size = sizeof( _serialNumber );

    /* Retrieve serial number from NVS */
	if( ESP_OK == NVS_Get( NVS_SERIAL_NUM, _serialNumber, &size ) )
	{
		completeDeviceName[10] = ' ';
		memcpy( &completeDeviceName[11], _serialNumber, sizeof( _serialNumber ) );
		completeDeviceName[23] = '\0';
		IotLogInfo( "bleGap_restoreSerialNumberAndDeviceName: %s", completeDeviceName );
	}
	return( &_serialNumber );
}

/**
 * @brief fetch Serial Number from NV memory
 *
 * Serial Number is retrieved from NV memory, Namespace = SysParam, Key= SerialNumber
 * Name and length of name are returned using passed pointers.
 *
 * @param[out] pSerialNumber	destination pointer
 * @param[in|out] length		string length pointer, set to buffer length on input, data length on output
 */
void bleGap_fetchSerialNumber( char *pSerialNumber, size_t *length )
{
    esp_err_t xRet;

    xRet = NVS_Get(NVS_SERIAL_NUM, pSerialNumber, length);

	if( xRet == ESP_OK )
	{
		IotLogInfo("fetchSerialNumber: %.*s, %d", *length, pSerialNumber, *length);
	}

}
