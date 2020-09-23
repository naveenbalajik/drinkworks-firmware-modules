/**
 * @file	bleStatus.h
 *
 */

#ifndef	_BLE_STATUS_H_
#define	_BLE_STATUS_H_

/**
 * @brief Enumerated BM7x Status Responses
 */
typedef enum StatusResponse
{
	eUnknownStatus,													// 0x00
	eScanningMode,													// 0x01
	eConnectingMode,												// 0x02
	eStandbyMode,													// 0x03
	eBroadcastMode = 0x05,											// 0x05
	eTransparentServiceEnabledMode = 0x08,							// 0x08
	eIdleMode,														// 0x09
	eShutdownMode,													// 0x0A
	eConfigureMode,													// 0x0B
	eBleConnectedMode												// 0x0C
} _bleStatus_t;

#endif		/* _BLE_STATUS_H_ */
