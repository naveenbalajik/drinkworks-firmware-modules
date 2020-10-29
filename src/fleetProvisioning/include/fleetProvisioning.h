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
 * @brief Initialize the PKCS11 objects with the final credentials. If the final credentials
 * are not yet stored in nvm then they will be requested from AWS using the claim credentials
 *
 * @param[in] pNetworkServerInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkInterface The network interface to use for this demo.
 * @param[in] pNetworkInterface The network interface to use for this demo.
 *
 * @return ESP_OK if successful, error otherwise
 */
int32_t fleetProv_FinalCredentialsInit(void * pNetworkServerInfo, void* pCredentials, const IotNetworkInterface_t * pNetworkInterface);

/**
 * @brief Initialize the fleet provisioning
 *
 * @param[in] pProvTemplateName		Fleet provisioning template name to use for provisioning
 *
 * @return ESP_OK if successful, error otherwise
 */
int32_t fleetProv_Init(const char * pProvTemplateName);

#endif // !FLEETPROVISIONING_H
