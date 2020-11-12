/*
 * ota_update.h
 *
 *  Created on: Jun 2, 2020
 *      Author: ian.whitehead
 */

#ifndef _OTA_UPDATE_H_
#define _OTA_UPDATE_H_

/**
 * @brief Type definition of function to call to get Host Ota Update module's status
 */
typedef bool (* hostOtaPendUpdateCallback_t)( void );

int OTAUpdate_startTask( 	const char * pIdentifier,
                            void * pNetworkCredentialInfo,
                            const IotNetworkInterface_t * pNetworkInterface,
							IotSemaphore_t *pSemaphore,
							hostOtaPendUpdateCallback_t function );


#endif /* _OTA_UPDATE_H_ */
