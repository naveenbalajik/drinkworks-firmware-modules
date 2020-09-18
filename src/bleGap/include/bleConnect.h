/**
 * @file bleConnect.h
 */
 
#ifndef	_BLE_CONNECT_H_
#define	_BLE_CONNECT_H_

#include "bt_hal_manager.h"

void bleConnect_init( void );
void bleConnect_BleConnected( uint16_t connectionID, BTBdaddr_t * remoteAddress );
void bleConnect_BleDisconnected( void );
void bleConnect_PairingStateChangedCb( BTStatus_t xStatus,
                                  BTBdaddr_t * pxRemoteBdAddr,
                                  BTBondState_t bondState,
                                  BTSecurityLevel_t xSecurityLevel,
                                  BTAuthFailureReason_t xReason );
void bleConnect_NumericComparisonCb( BTBdaddr_t * pxRemoteBdAddr,
                             uint32_t ulPassKey );

#endif /* _BLE_CONNECT_H_ */
