/**
 *  @file bleGap.h
 *
 *  Created on: Sep 17, 2019
 *      Author: ian.whitehead
 */

#ifndef _BLE_GAP_H_
#define _BLE_GAP_H_

/**
 * @brief  Drinkworks GATT service, characteristics and descriptor UUIDs (0d8fd548-266b-48ee-8b16-ef4c87e24953)
 */
#define  DW_SERVICE_MASK				0x53, 0x49, 0xE2, 0x87, 0x4C, 0xEF, 0x16, 0x8B, 0xEE, 0x48, 0x6B, 0x26
#define  DW_SERVICE_UUID				{DW_SERVICE_MASK, 0x48, 0xD5, 0x8F, 0x0D}

extern uint8_t own_addr_type;

// ESP Module Status
typedef enum {
	esp_idle,
	esp_advertising,
	esp_connected
} esp_status_t;

esp_status_t get_ble_status(void);

void DwBleGap_setDeviceName( void );
void bleGap_setSerialNumberAndDeviceName( const uint8_t *pSerialNumber, const uint16_t size );
void bleGap_fetchSerialNumber( char *pSerialNumber, size_t *length );
//typedef struct ble_gap_conn_desc ble_gap_conn_desc_t;

#endif /* _BLE_GAP_H_ */
