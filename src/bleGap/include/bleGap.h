/**
 *  @file bleGap.h
 *
 *  Created on: Sep 17, 2019
 *      Author: ian.whitehead
 */

#ifndef _BLE_GAP_H_
#define _BLE_GAP_H_

extern uint8_t own_addr_type;

// ESP Module Status
typedef enum {
	esp_idle,
	esp_advertising,
	esp_connected
} esp_status_t;

esp_status_t get_ble_status(void);

void DwBleGap_setDeviceName( void );
void bleGap_setSerialNumberAndDeviceName( uint8_t *pSerialNumber, uint16_t size );
void bleGap_fetchSerialNumber( char *pSerialNumber, size_t *length );
//typedef struct ble_gap_conn_desc ble_gap_conn_desc_t;

#endif /* _BLE_GAP_H_ */
