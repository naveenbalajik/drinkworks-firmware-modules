/**
 * @file bleConnect.h
 */
 
#ifndef	_BLE_CONNECT_H_
#define	_BLE_CONNECT_H_

#include "bt_hal_manager.h"
#include "bleStatus.h"
/**
 * @brief	Connection Mapping, store limited number of connection ID and Remote BT Addresses
 */
typedef struct
{
	uint16_t	connectionID;							/**< 16-bit connection ID, assigned by Network Manager */
	uint8_t		btAddress[6];							/**< Remote Bluetooth address */
	bool		inUse;									/**< true if entry is in use, false is available for storing values */
} _connectionMap_t;


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

_connectionMap_t *retrieveConnection( uint8_t handle );

_bleStatus_t bleConnect_getStatus( void );
void bleConnect_setStatus( _bleStatus_t status );

#endif /* _BLE_CONNECT_H_ */
