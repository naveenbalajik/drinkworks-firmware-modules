/**
 * @file fleetProvisioning.h
 *
 * Created on: May 4, 2020
 * 		Author: nick.weber
 */

#ifndef FLEETPROVISIONING_H
#define FLEETPROVISIONING_H

#include "platform/iot_network.h"

/**
 * @brief enum describing the state of fleet provisioning
 */
typedef enum{
	eFLEETPROV_NOT_INITIALIZED,
	eFLEETPROV_IN_PROCESS,
	eFLEETPROV_COMPLETED_SUCCESS,
	eFLEETPROV_COMPLETED_FAILED
}eFleetProv_Status_t;

/**
 * @brief Initialization parameters for the fleet provisioning task
 */
typedef struct{
	void * pConnectionParams;
	void * pCredentials;
	const IotNetworkInterface_t * pNetworkInterface;
	const char * pProvTemplateName;
}fleetProv_InitParams_t;

/**
 * @brief Initialize the fleet provisioning task. This function creates a new fleet provisioning task which
 * set the PKCS11 credentials of the ESP module. The task will request credentials from AWS if no final
 * credentials exist. If final credentials are not set, have a Wifi connection before calling this task.
 *
 * @param[in] pfleetProvInitParams		Initialization parameters for fleet provisioning
 * @param[in] pSemaphore				Semaphore that will be posted when the fleet provisioning completes
 *
 * @return ESP_OK if successful, error otherwise
 */
int32_t fleetProv_Init(fleetProv_InitParams_t * pfleetProvInitParams, IotSemaphore_t* pSemaphore);

#endif // !FLEETPROVISIONING_H
