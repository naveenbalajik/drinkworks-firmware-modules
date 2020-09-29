/*
 * ota_update.h
 *
 *  Created on: Jun 2, 2020
 *      Author: ian.whitehead
 */

#ifndef _OTA_UPDATE_H_
#define _OTA_UPDATE_H_

int OTAUpdate_startTask( 	void * pNetworkServerInfo,
							IotMqttConnection_t * pMqttConnection,
                            const char * pIdentifier,
                            void * pNetworkCredentialInfo,
                            const IotNetworkInterface_t * pNetworkInterface,
							IotSemaphore_t *pSemaphore );


#endif /* _OTA_UPDATE_H_ */
