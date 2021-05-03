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
 *
 * Reserve 16 UUID's for product identification:
 *
 * 0D8FD548-266b-48ee-8b16-ef4c87e24953 : Model-A
 * 1B35C348-266b-48ee-8b16-ef4c87e24953 : Model-B
 * 2856A548-266b-48ee-8b16-ef4c87e24953
 * 37699648-266b-48ee-8b16-ef4c87e24953
 * 449A5948-266b-48ee-8b16-ef4c87e24953
 * 52AC3A48-266b-48ee-8b16-ef4c87e24953
 * 6163C648-266b-48ee-8b16-ef4c87e24953
 * 7195A948-266b-48ee-8b16-ef4c87e24953
 * 82A66A48-266b-48ee-8b16-ef4c87e24953
 * 94C95C48-266b-48ee-8b16-ef4c87e24953
 * A7399348-266b-48ee-8b16-ef4c87e24953
 * B85A6548-266b-48ee-8b16-ef4c87e24953
 * CB6C3648-266b-48ee-8b16-ef4c87e24953
 * DDA3CA48-266b-48ee-8b16-ef4c87e24953
 * E4C59C48-266b-48ee-8b16-ef4c87e24953
 * F03C5348-266b-48ee-8b16-ef4c87e24953
 */
#define  DW_SERVICE_MASK				0x53, 0x49, 0xE2, 0x87, 0x4C, 0xEF, 0x16, 0x8B, 0xEE, 0x48, 0x6B, 0x26
#define  DW_SERVICE_UUID				{ DW_SERVICE_MASK, 0x48, 0xD5, 0x8F, 0x0D }
#define  DW_MODB_SERVICE_UUID			{ DW_SERVICE_MASK, 0x48, 0xC3, 0x35, 0x1B }

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
uint8_t *  bleGap_restoreSerialNumberAndDeviceName( void );
void bleGap_fetchSerialNumber( char *pSerialNumber, size_t *length );
//typedef struct ble_gap_conn_desc ble_gap_conn_desc_t;

#endif /* _BLE_GAP_H_ */
