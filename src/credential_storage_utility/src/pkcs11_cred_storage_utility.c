/**
 * @file pkcs11_cred_storage_utility.c
 *
 * Created on: May 12, 2020
 * 		Author: nick.weber
 */

/* Standard includes. */
#include <stdio.h>
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* PKCS#11 includes. */
#include "iot_pkcs11_config.h"
#include "iot_pkcs11.h"

/* Client credential includes. */
#include "aws_clientcredential.h"
#include "aws_clientcredential_keys.h"
#include "iot_default_root_certificates.h"

/* Utilities include. */
#include "iot_pki_utils.h"

/* mbedTLS includes. */
#include "mbedtls/pk.h"
#include "mbedtls/oid.h"

#include "pkcs11_cred_storage_utility.h"

#include "credential_decryption_utility.h"

/* Debug Logging */
#include "credential_logging.h"


/* This function can be found in libraries/3rdparty/mbedtls/utils/mbedtls_utils.c. */
extern int convert_pem_to_der(const unsigned char* pucInput,
	size_t xLen,
	unsigned char* pucOutput,
	size_t* pxOlen);

/* Internal structure for parsing RSA keys. */

/* Length parameters for importing RSA-2048 private keys. */
#define MODULUS_LENGTH        pkcs11RSA_2048_MODULUS_BITS / 8
#define E_LENGTH              3
#define D_LENGTH              pkcs11RSA_2048_MODULUS_BITS / 8
#define PRIME_1_LENGTH        128
#define PRIME_2_LENGTH        128
#define EXPONENT_1_LENGTH     128
#define EXPONENT_2_LENGTH     128
#define COEFFICIENT_LENGTH    128

/* Adding one to all of the lengths because ASN1 may pad a leading 0 byte
 * to numbers that could be interpreted as negative */
typedef struct RsaParams_t
{
	CK_BYTE modulus[MODULUS_LENGTH + 1];
	CK_BYTE e[E_LENGTH + 1];
	CK_BYTE d[D_LENGTH + 1];
	CK_BYTE prime1[PRIME_1_LENGTH + 1];
	CK_BYTE prime2[PRIME_2_LENGTH + 1];
	CK_BYTE exponent1[EXPONENT_1_LENGTH + 1];
	CK_BYTE exponent2[EXPONENT_2_LENGTH + 1];
	CK_BYTE coefficient[COEFFICIENT_LENGTH + 1];
} RsaParams_t;

/**
 * @brief Import the specified RSA private key into storage object.
 *
 * @param[in] xSession  Handle of a valid PKCS #11 session.
 * @param[in] pucLabel             PKCS #11 CKA_LABEL attribute value to be used for key.
 *                                 This should be a string values. See iot_pkcs11_config.h
 * @param[out] pxObject            Pointer to the location where the created
 *                                 object's handle will be placed.
 * @param[in] ctx      		   	   The RSA context to be .
 *
 * @return CKR_OK upon successful key creation.
 * Otherwise, a positive PKCS #11 error code.
 */
static CK_RV _prvProvisionPrivateRSAKey( CK_SESSION_HANDLE xSession,
	uint8_t* pucLabel,
	CK_OBJECT_HANDLE_PTR pxObjectHandle,
	mbedtls_pk_context* pxMbedPkContext )
{
	CK_RV xResult = CKR_OK;
	CK_FUNCTION_LIST_PTR pxFunctionList = NULL;
	int lMbedResult = 0;
	CK_KEY_TYPE xPrivateKeyType = CKK_RSA;
	mbedtls_rsa_context* xRsaContext = pxMbedPkContext->pk_ctx;
	CK_OBJECT_CLASS xPrivateKeyClass = CKO_PRIVATE_KEY;
	RsaParams_t* pxRsaParams = NULL;
	CK_BBOOL xTrue = CK_TRUE;

	xResult = C_GetFunctionList( &pxFunctionList );

	pxRsaParams = pvPortMalloc( sizeof(RsaParams_t) );

	if( pxRsaParams == NULL )
	{
		xResult = CKR_HOST_MEMORY;
	}

	if( xResult == CKR_OK )
	{
		memset(pxRsaParams, 0, sizeof(RsaParams_t));

		lMbedResult = mbedtls_rsa_export_raw( xRsaContext,
			pxRsaParams->modulus, MODULUS_LENGTH + 1,
			pxRsaParams->prime1, PRIME_1_LENGTH + 1,
			pxRsaParams->prime2, PRIME_2_LENGTH + 1,
			pxRsaParams->d, D_LENGTH + 1,
			pxRsaParams->e, E_LENGTH + 1 );

		if( lMbedResult != 0 )
		{
			IotLogError( "Failed to parse RSA private key components." );
			xResult = CKR_ATTRIBUTE_VALUE_INVALID;
		}

		/* Export Exponent 1, Exponent 2, Coefficient. */
		lMbedResult |= mbedtls_mpi_write_binary( ( mbedtls_mpi const* )& xRsaContext->DP, pxRsaParams->exponent1, EXPONENT_1_LENGTH + 1 );
		lMbedResult |= mbedtls_mpi_write_binary( ( mbedtls_mpi const* )& xRsaContext->DQ, pxRsaParams->exponent2, EXPONENT_2_LENGTH + 1 );
		lMbedResult |= mbedtls_mpi_write_binary( ( mbedtls_mpi const* )& xRsaContext->QP, pxRsaParams->coefficient, COEFFICIENT_LENGTH + 1 );

		if( lMbedResult != 0 )
		{
			IotLogError( "Failed to parse RSA private key Chinese Remainder Theorem variables." );
			xResult = CKR_ATTRIBUTE_VALUE_INVALID;
		}
	}

	if( xResult == CKR_OK )
	{
		/* When importing the fields, the pointer is incremented by 1
		 * to remove the leading 0 padding (if it existed) and the original field length is used */


		CK_ATTRIBUTE xPrivateKeyTemplate[] =
		{
			{ CKA_CLASS,            &xPrivateKeyClass,            sizeof(CK_OBJECT_CLASS)                        },
			{ CKA_KEY_TYPE,         &xPrivateKeyType,             sizeof(CK_KEY_TYPE)                            },
			{ CKA_LABEL,            pucLabel,                     (CK_ULONG)strlen((const char*)pucLabel) },
			{ CKA_TOKEN,            &xTrue,                       sizeof(CK_BBOOL)                               },
			{ CKA_SIGN,             &xTrue,                       sizeof(CK_BBOOL)                               },
			{ CKA_MODULUS,          pxRsaParams->modulus + 1,     MODULUS_LENGTH                                   },
			{ CKA_PRIVATE_EXPONENT, pxRsaParams->d + 1,           D_LENGTH                                         },
			{ CKA_PUBLIC_EXPONENT,  pxRsaParams->e + 1,           E_LENGTH                                         },
			{ CKA_PRIME_1,          pxRsaParams->prime1 + 1,      PRIME_1_LENGTH                                   },
			{ CKA_PRIME_2,          pxRsaParams->prime2 + 1,      PRIME_2_LENGTH                                   },
			{ CKA_EXPONENT_1,       pxRsaParams->exponent1 + 1,   EXPONENT_1_LENGTH                                },
			{ CKA_EXPONENT_2,       pxRsaParams->exponent2 + 1,   EXPONENT_2_LENGTH                                },
			{ CKA_COEFFICIENT,      pxRsaParams->coefficient + 1, COEFFICIENT_LENGTH                               }
		};

		xResult = pxFunctionList->C_CreateObject( xSession,
			(CK_ATTRIBUTE_PTR)& xPrivateKeyTemplate,
			sizeof(xPrivateKeyTemplate) / sizeof(CK_ATTRIBUTE),
			pxObjectHandle );
	}

	if( NULL != pxRsaParams )
	{
		vPortFree( pxRsaParams );
	}

	return xResult;
}

/** @brief Provisions a private key using PKCS #11 library. Import the specified private key into storage.
 *
 * @param[in] xSession             An initialized session handle.
 * @param[in] pucPrivateKey        Pointer to private key.  Key may either be PEM formatted
 *                                 or ASN.1 DER encoded.
 * @param[in] xPrivateKeyLength    Length of the data at pucPrivateKey, in bytes.
 * @param[in] pucLabel             PKCS #11 CKA_LABEL attribute value to be used for key.
 *                                 This should be a string values. See iot_pkcs11_config.h
 * @param[out] pxObjectHandle      Points to the location that receives the PKCS #11
 *                                 private key handle created.
 *
 * @return CKR_OK upon successful key creation.
 * Otherwise, a positive PKCS #11 error code.
 */
static CK_RV _xProvisionPrivateKey( CK_SESSION_HANDLE xSession,
	uint8_t* pucPrivateKey,
	size_t xPrivateKeyLength,
	uint8_t* pucLabel,
	CK_OBJECT_HANDLE_PTR pxObjectHandle )
{
	CK_RV xResult = CKR_OK;
	int lMbedResult = 0;
	mbedtls_pk_context xMbedPkContext = { 0 };

	mbedtls_pk_init( &xMbedPkContext );
	lMbedResult = mbedtls_pk_parse_key( &xMbedPkContext, pucPrivateKey, xPrivateKeyLength, NULL, 0 );

	if( lMbedResult != 0 )
	{
		IotLogError( "Unable to parse private key." );
		xResult = CKR_ARGUMENTS_BAD;
	}

	/* Determine whether the key to be imported is RSA or EC. */
	if( xResult == CKR_OK )
	{
		xResult = _prvProvisionPrivateRSAKey( xSession,
			pucLabel,
			pxObjectHandle,
			&xMbedPkContext );
	}

	mbedtls_pk_free( &xMbedPkContext );

	return xResult;
}


/**
 * @brief Destroys specified credentials in PKCS #11 module. Delete the specified crypto object from storage.
 *
 * \note Some ports only support lookup of objects by label (and
 * not label + class).  For these ports, only the label field is used
 * for determining what objects to destroy.
 *
 * \note Not all ports support the deletion of all objects.  Successful
 * function return only indicates that all objects for which destroy is
 * supported on the port were erased from non-volatile memory.
 *
 * @param[in] xSession         A valid PKCS #11 session handle.
 * @param[in] ppxPkcsLabels    An array of pointers to object labels.
 *                               Labels are assumed to be NULL terminated
 *                               strings.
 * @param[in] pxClass          An array of object classes, corresponding
 *                               to the array of ppxPkcsLabels.  For example
 *                               the first label pointer and first class in
 *                               ppxPkcsLabels are used in combination for
 *                               lookup of the object to be deleted.
 * @param[in] ulCount          The number of label-class pairs passed in
 *                               to be destroyed.
 *
 * @return CKR_OK if all credentials were destroyed.
 *   Otherwise, a positive PKCS #11 error code.
 */
static CK_RV _xDestroyProvidedObjects( CK_SESSION_HANDLE xSession,
	CK_BYTE_PTR* ppxPkcsLabels,
	CK_OBJECT_CLASS* xClass,
	CK_ULONG ulCount )
{
	CK_RV xResult;
	CK_FUNCTION_LIST_PTR pxFunctionList;
	CK_OBJECT_HANDLE xObjectHandle;
	CK_BYTE* pxLabel;
	CK_ULONG uiIndex = 0;

	xResult = C_GetFunctionList( &pxFunctionList );

	for( uiIndex = 0; uiIndex < ulCount; uiIndex++ )
	{
		pxLabel = ppxPkcsLabels[uiIndex];

		xResult = xFindObjectWithLabelAndClass( xSession,
			(const char*)pxLabel,
			xClass[uiIndex],
			&xObjectHandle );

		while( ( xResult == CKR_OK ) && ( xObjectHandle != CK_INVALID_HANDLE ) )
		{
			xResult = pxFunctionList->C_DestroyObject( xSession, xObjectHandle );

			/* PKCS #11 allows a module to maintain multiple objects with the same
			 * label and type. The intent of this loop is to try to delete all of them.
			 * However, to avoid getting stuck, we won't try to find another object
			 * of the same label/type if the previous delete failed. */
			if( xResult == CKR_OK )
			{
				xResult = xFindObjectWithLabelAndClass( xSession,
					(const char*)pxLabel,
					xClass[uiIndex],
					&xObjectHandle );
			}
			else
			{
				break;
			}
		}

		if( xResult == CKR_FUNCTION_NOT_SUPPORTED )
		{
			break;
		}
	}

	return xResult;
}

/**
 * @brief Imports a certificate into the PKCS #11 module. Import the specified X.509 client certificate into storage.
 *
 * @param[in] xSession              A valid PKCS #11 session handle.
 * @param[in] pucCertificate        Pointer to a PEM certificate.
 *                                  See tools/certificate_configuration/PEMfileToCString.html
 *                                  for help with formatting.
 * @param[in] xCertificateLength    Length of pucCertificate, in bytes.
 * @param[in] pucLabel              PKCS #11 label attribute value for certificate to be imported.
 *                                  This should be a string value. See iot_pkcs11.h.
 *                                  This should be a string value. See iot_pkcs11_config.h.
 * @param[out] pxObjectHandle       Points to the location that receives the PKCS #11
 *                                  certificate handle created.
 *
 * @return CKR_OK if certificate import succeeded.
 * Otherwise, a positive PKCS #11 error code.
 */
static CK_RV _xProvisionCertificate( CK_SESSION_HANDLE xSession,
	uint8_t* pucCertificate,
	size_t xCertificateLength,
	uint8_t* pucLabel,
	CK_OBJECT_HANDLE_PTR pxObjectHandle )
{
	PKCS11_CertificateTemplate_t xCertificateTemplate;
	CK_OBJECT_CLASS xCertificateClass = CKO_CERTIFICATE;
	CK_CERTIFICATE_TYPE xCertificateType = CKC_X_509;
	CK_FUNCTION_LIST_PTR pxFunctionList;
	CK_RV xResult;
	uint8_t* pucDerObject = NULL;
	int32_t lConversionReturn = 0;
	size_t xDerLen = 0;
	CK_BBOOL xTokenStorage = CK_TRUE;

	/* TODO: Subject is a required attribute.
	 * Currently, this field is not used by FreeRTOS ports,
	 * this should be updated so that subject matches proper
	 * format for future ports. */
	CK_BYTE xSubject[] = "TestSubject";

	/* Initialize the client certificate template. */
	xCertificateTemplate.xObjectClass.type = CKA_CLASS;
	xCertificateTemplate.xObjectClass.pValue = &xCertificateClass;
	xCertificateTemplate.xObjectClass.ulValueLen = sizeof(xCertificateClass);
	xCertificateTemplate.xSubject.type = CKA_SUBJECT;
	xCertificateTemplate.xSubject.pValue = xSubject;
	xCertificateTemplate.xSubject.ulValueLen = strlen((const char*)xSubject);
	xCertificateTemplate.xValue.type = CKA_VALUE;
	xCertificateTemplate.xValue.pValue = (CK_VOID_PTR)pucCertificate;
	xCertificateTemplate.xValue.ulValueLen = (CK_ULONG)xCertificateLength;
	xCertificateTemplate.xLabel.type = CKA_LABEL;
	xCertificateTemplate.xLabel.pValue = (CK_VOID_PTR)pucLabel;
	xCertificateTemplate.xLabel.ulValueLen = strlen((const char*)pucLabel);
	xCertificateTemplate.xCertificateType.type = CKA_CERTIFICATE_TYPE;
	xCertificateTemplate.xCertificateType.pValue = &xCertificateType;
	xCertificateTemplate.xCertificateType.ulValueLen = sizeof(CK_CERTIFICATE_TYPE);
	xCertificateTemplate.xTokenObject.type = CKA_TOKEN;
	xCertificateTemplate.xTokenObject.pValue = &xTokenStorage;
	xCertificateTemplate.xTokenObject.ulValueLen = sizeof(xTokenStorage);

	xResult = C_GetFunctionList( &pxFunctionList );

	/* Test for a valid certificate: 0x2d is '-', as in ----- BEGIN CERTIFICATE. */
	if( ( pucCertificate == NULL ) || ( pucCertificate[0] != 0x2d ) )
	{
		xResult = CKR_ATTRIBUTE_VALUE_INVALID;
	}

	if( xResult == CKR_OK )
	{
		/* Convert the certificate to DER format if it was in PEM. The DER key
		 * should be about 3/4 the size of the PEM key, so mallocing the PEM key
		 * size is sufficient. */
		pucDerObject = pvPortMalloc( xCertificateTemplate.xValue.ulValueLen );
		xDerLen = xCertificateTemplate.xValue.ulValueLen;

		if( pucDerObject != NULL )
		{
			lConversionReturn = convert_pem_to_der( xCertificateTemplate.xValue.pValue,
				xCertificateTemplate.xValue.ulValueLen,
				pucDerObject,
				&xDerLen );

			if( 0 != lConversionReturn )
			{
				xResult = CKR_ARGUMENTS_BAD;
			}
		}
		else
		{
			xResult = CKR_HOST_MEMORY;
		}
	}

	if( xResult == CKR_OK )
	{
		/* Set the template pointers to refer to the DER converted objects. */
		xCertificateTemplate.xValue.pValue = pucDerObject;
		xCertificateTemplate.xValue.ulValueLen = xDerLen;
	}

	/* Best effort clean-up of the existing object, if it exists. */
	if( xResult == CKR_OK )
	{
		_xDestroyProvidedObjects( xSession,
			&pucLabel,
			&xCertificateClass,
			1 );
	}

	/* Create an object using the encoded client certificate. */
	if( xResult == CKR_OK )
	{
		xResult = pxFunctionList->C_CreateObject( xSession,
			(CK_ATTRIBUTE_PTR)& xCertificateTemplate,
			sizeof(xCertificateTemplate) / sizeof(CK_ATTRIBUTE),
			pxObjectHandle );
	}

	if( pucDerObject != NULL )
	{
		vPortFree( pucDerObject );
	}

	return xResult;
}

/**
 *@brief Destroys FreeRTOS credentials stored in device PKCS #11 module. Delete well-known crypto objects from storage.
 *
 * \note Not all ports support the deletion of all objects.  Successful
 * function return only indicates that all objects for which destroy is
 * supported on the port were erased from non-volatile memory.
 *
 * Destroys objects with the following labels, if applicable:
 *     pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS,
 *     pkcs11configLABEL_CODE_VERIFICATION_KEY,
 *     pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS,
 *     pkcs11configLABEL_DEVICE_PUBLIC_KEY_FOR_TLS
 *
 *   @param[in] xSession         A valid PKCS #11 session handle.
 *
 *   @return CKR_OK if all credentials were destroyed.
 *   Otherwise, a positive PKCS #11 error code.
 */
static CK_RV _xDestroyDefaultCryptoObjects( CK_SESSION_HANDLE xSession )
{
	CK_RV xResult;
	CK_BYTE* pxPkcsLabels[] =
	{
		(CK_BYTE*)pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS,
		(CK_BYTE*)pkcs11configLABEL_CODE_VERIFICATION_KEY,
		(CK_BYTE*)pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS,
		(CK_BYTE*)pkcs11configLABEL_DEVICE_PUBLIC_KEY_FOR_TLS
	};
	CK_OBJECT_CLASS xClass[] =
	{
		CKO_CERTIFICATE,
		CKO_PUBLIC_KEY,
		CKO_PRIVATE_KEY,
		CKO_PUBLIC_KEY
	};

	xResult = _xDestroyProvidedObjects( xSession,
		pxPkcsLabels,
		xClass,
		sizeof(xClass) / sizeof(CK_OBJECT_CLASS) );

	return xResult;
}

/**
 * @brief Provisiong a device given a valid PKCS #11 session.
 *
 * @param[in] xSession       A valid PKCS #11 session.
 * @param[in] pxParams       Pointer to an initialized provisioning
 *                           structure.
 *
 * @return CKR_OK upon successful credential setup.
 * Otherwise, a positive PKCS #11 error code.
 */
static CK_RV _xProvisionDevice( CK_SESSION_HANDLE xSession,
	ProvisioningParams_t* pxParams )
{
	CK_RV xResult;
	CK_FUNCTION_LIST_PTR pxFunctionList;
	CK_OBJECT_HANDLE xObject = 0;

	xResult = C_GetFunctionList( &pxFunctionList );

	/* Attempt to clean-up old crypto objects, but only if private key import is
	 * supported by this application, and only if the caller has provided new
	 * objects to use instead. */
	if( ( CKR_OK == xResult ) &&
		( NULL != pxParams->pucClientCertificate ) &&
		( NULL != pxParams->pucClientPrivateKey ) )
	{
		xResult = _xDestroyDefaultCryptoObjects( xSession );

		if( xResult != CKR_OK )
		{
			IotLogWarn( "Warning: could not clean-up old crypto objects. %d", xResult );
		}
	}

	/* If a client certificate has been provided by the caller, attempt to
	 * import it. */
	if( ( xResult == CKR_OK ) && ( NULL != pxParams->pucClientCertificate ) )
	{
		xResult = _xProvisionCertificate( xSession,
			pxParams->pucClientCertificate,
			pxParams->ulClientCertificateLength,
			(uint8_t*)pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS,
			&xObject );

		if( ( xResult != CKR_OK ) || ( xObject == CK_INVALID_HANDLE ) )
		{
			IotLogError( "ERROR: Failed to provision device certificate. %d", xResult );
		}
	}

	// Decrypt code signing certificate and import to pkcs11 object
	if( xResult == CKR_OK )
	{
		xResult = credentialUtility_decryptCredentials();
	}

	if( xResult == CKR_OK && codeSignCertLength )
	{
		xResult = _xProvisionCertificate( xSession,
					plaintextCodeSignCert,
					codeSignCertLength,
					(uint8_t*)pkcs11configLABEL_CODE_VERIFICATION_KEY,
					&xObject );

		if( ( xResult != CKR_OK ) || ( xObject == CK_INVALID_HANDLE ) )
		{
			IotLogError( "ERROR: Failed to provision code signing certificate. %d", xResult );
		}
	}

	/* If this application supports importing private keys, and if a private
	 * key has been provided by the caller, attempt to import it. */
	if( ( xResult == CKR_OK ) && ( NULL != pxParams->pucClientPrivateKey ) )
	{
		xResult = _xProvisionPrivateKey(xSession,
			pxParams->pucClientPrivateKey,
			pxParams->ulClientPrivateKeyLength,
			(uint8_t*)pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS,
			&xObject);

		if( ( xResult != CKR_OK ) || ( xObject == CK_INVALID_HANDLE ) )
		{
			IotLogError( "ERROR: Failed to provision device private key with status %d.", xResult );
		}
	}

	return xResult;
}

/**
 * @brief Sets the provisioning parameters of the PKCS11 object used in the TLS connection
 *
 * @param[in] xParams       Provisioning parameters for credentials
 *
 * @return CKR_OK upon successful credential setup.
 * Otherwise, a positive PKCS #11 error code.
 */
CK_RV setPKCS11CredObjectParams( ProvisioningParams_t* xParams )
{
	CK_RV xResult = CKR_OK;
	CK_FUNCTION_LIST_PTR pxFunctionList = NULL;
	CK_SESSION_HANDLE xSession = 0;

	xResult = C_GetFunctionList( &pxFunctionList );

	/* Initialize the PKCS Module */
	if( xResult == CKR_OK )
	{
		xResult = xInitializePkcs11Token();
	}

	if( xResult == CKR_OK )
	{
		xResult = xInitializePkcs11Session( &xSession );
	}

	if( xResult == CKR_OK )
	{
		xResult = _xProvisionDevice( xSession, xParams );

		pxFunctionList->C_CloseSession( xSession );
	}

	return xResult;
}
