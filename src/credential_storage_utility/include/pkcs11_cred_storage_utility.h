/**
 * @file pkcs11_cred_storage_utility.h
 *
 * Created on: May 12, 2020
 * 		Author: nick.weber
 */

#ifndef PKCS11CREDSTORAGEUTILITY_H
#define PKCS11CREDSTORAGEUTILITY_H

#include "iot_pkcs11_config.h"
#include "iot_pkcs11.h"

typedef struct ProvisioningParams_t
{
	uint8_t* pucClientPrivateKey;      /**< Pointer to the device private key in PEM format.
										 *   See tools/certificate_configuration/PEMfileToCString.html
										 *   for help with formatting.*/
	uint32_t ulClientPrivateKeyLength;  /**< Length of the private key data, in bytes. */
	uint8_t* pucClientCertificate;     /**< Pointer to the device certificate in PEM format.
										 *   See tools/certificate_configuration/PEMfileToCString.html
										 *   for help with formatting.*/
	uint32_t ulClientCertificateLength; /**< Length of the device certificate in bytes. */
} ProvisioningParams_t;


CK_RV setPKCS11CredObjectParams(ProvisioningParams_t* xParams);

#endif // !PKCS11CREDSTORAGEUTILITY_H
