/**
 * @file dw_test_shci.h
 */
 
#ifndef	_DW_TEST_SHCI_H_
#define	_DW_TEST_SHCI_H_

#include "bt_hal_manager_types.h"

extern void bleFunction_init( void );
extern void bleFunction_BleConnected( uint16_t connectionID, BTBdaddr_t * remoteAddress );
extern void bleFunction_BleDisconnected( void );
extern void void bleFunction_writeCharacteristic( uint16_t remoteHandle, uint8_t *pData, uint16_t size );

#endif /* _DW_TEST_SHCI_H_ */
