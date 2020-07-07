/**
 * @file fleetProvisioning.h
 *
 * Created on: May 4, 2020
 * 		Author: nick.weber
 */

#ifndef FLEETPROVISIONING_H
#define FLEETPROVISIONING_H

#include "platform/iot_network.h"

int32_t fleetProv_FinalCredentialsInit(void * pNetworkServerInfo, void* pCredentials, const IotNetworkInterface_t * pNetworkInterface);

#endif // !FLEETPROVISIONING_H
