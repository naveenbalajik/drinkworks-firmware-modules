/*
 * Copyright 2018 Espressif Systems (Shanghai) PTE LTD
 *
 * FreeRTOS OTA PAL for ESP32-DevKitC ESP-WROVER-KIT V1.0.4
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.*/
/**
 * @file host_ota_pal.c
 */
/* OTA PAL implementation for Espressif esp32_devkitc_esp_wrover_kit platform adapted for secondary processor */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "aws_iot_ota_agent.h"
#include "aws_iot_ota_pal.h"
#include "aws_iot_ota_interface.h"
#include "aws_ota_agent_config.h"
#include "types/iot_network_types.h"
#include "aws_iot_network_config.h"
#include "iot_crypto.h"
#include "iot_pkcs11.h"
#include "esp_system.h"
#include "esp_log.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_wdt.h"
#include "aws_ota_codesigner_certificate.h"

#include "esp_partition.h"
#include "esp_spi_flash.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "host_ota_ops.h"
#include "mbedtls/asn1.h"
#include "mbedtls/bignum.h"
#include "mbedtls/base64.h"

#include "host_ota_pal.h"

#define kOTA_HalfSecondDelay    pdMS_TO_TICKS( 500UL )
#define ECDSA_INTEGER_LEN       32

/* Check configuration for memory constraints provided SPIRAM is not enabled */
#if !CONFIG_SPIRAM_SUPPORT
#if (configENABLED_DATA_PROTOCOLS & OTA_DATA_OVER_HTTP) && (configENABLED_NETWORKS & AWSIOT_NETWORK_TYPE_BLE)
    #error "Cannot enable OTA data over HTTP together with BLE because of not enough heap."
#endif
#endif // !CONFIG_SPIRAM_SUPPORT

/**
 * @brief	Block size used when reading the image from SPI Flash.
 *
 * If the image is saved in a non-encrypted partition, and encryption has been enabled on the system, the flash must
 * be accessed using esp_partion_read(), rather than using the MMU to map the SPI Flash to system address space and
 * utilize the cache (this would enable the hardware decryption engine and prevent reading the stored data).
 */
#define	FLASH_READ_BLOCK_SIZE	1024

/*
 * Includes 4 bytes of version field, followed by 64 bytes of signature
 * (Rest 12 bytes for padding to make it 16 byte aligned for flash encryption)
 */
#define ECDSA_SIG_SIZE          80

typedef struct
{
    const esp_partition_t * update_partition;
    const OTA_FileContext_t * cur_ota;
    esp_ota_handle_t update_handle;
    uint32_t data_write_len;
    bool valid_image;
} esp_ota_context_t;

typedef struct
{
    uint8_t sec_ver[ 4 ];
    uint8_t raw_ecdsa_sig[ 64 ];
    uint8_t pad[ 12 ];
} esp_sec_boot_sig_t;

static esp_ota_context_t ota_ctx;
static const char * TAG = "ota_pal_dw";

static CK_RV prvGetCertificateHandle( CK_FUNCTION_LIST_PTR pxFunctionList,
                                      CK_SESSION_HANDLE xSession,
                                      const char * pcLabelName,
                                      CK_OBJECT_HANDLE_PTR pxCertHandle );
static CK_RV prvGetCertificate( const char * pcLabelName,
                                uint8_t ** ppucData,
                                uint32_t * pulDataSize );

/**
 * @brief	Convert ASN1 signature to raw ECDSA
 */
static OTA_Err_t asn1_to_raw_ecdsa( uint8_t * signature,
                                    uint16_t sig_len,
                                    uint8_t * out_signature )
{
    int ret = 0;
    const unsigned char * end = signature + sig_len;
    size_t len;
    mbedtls_mpi r = { 0 };
    mbedtls_mpi s = { 0 };

    if( out_signature == NULL )
    {
        ESP_LOGE( TAG, "ASN1 invalid argument !" );
        goto cleanup;
    }

    mbedtls_mpi_init( &r );
    mbedtls_mpi_init( &s );

    if( ( ret = mbedtls_asn1_get_tag( &signature, end, &len,
                                      MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE ) ) != 0 )
    {
        ESP_LOGE( TAG, "Bad Input Signature" );
        goto cleanup;
    }

    if( signature + len != end )
    {
        ESP_LOGE( TAG, "Incorrect ASN1 Signature Length" );
        goto cleanup;
    }

    if( ( ( ret = mbedtls_asn1_get_mpi( &signature, end, &r ) ) != 0 ) ||
        ( ( ret = mbedtls_asn1_get_mpi( &signature, end, &s ) ) != 0 ) )
    {
        ESP_LOGE( TAG, "ASN1 parsing failed" );
        goto cleanup;
    }

    ret = mbedtls_mpi_write_binary( &r, out_signature, ECDSA_INTEGER_LEN );
    ret = mbedtls_mpi_write_binary( &s, out_signature + ECDSA_INTEGER_LEN, ECDSA_INTEGER_LEN );

cleanup:
    mbedtls_mpi_free( &r );
    mbedtls_mpi_free( &s );

    if( ret == 0 )
    {
        return kOTA_Err_None;
    }
    else
    {
        return kOTA_Err_BadSignerCert;
    }
}

/**
 * @brief	Clear OTA Context
 *
 * @param[in]	C OTA file context
 */
static void _clearOtaCtx( esp_ota_context_t * ota_ctx )
{
    if( ota_ctx != NULL )
    {
        memset( ota_ctx, 0, sizeof( esp_ota_context_t ) );
    }
}

/**
 * @brief	Validate OTA Context
 *
 * @param[in]	C OTA file context
 */
static bool _validateOtaCtx( OTA_FileContext_t * C )
{
    return( C != NULL && ota_ctx.cur_ota == C && C->pucFile == ( uint8_t * ) &ota_ctx );
}

/**
 * @brief	Close OTA Context
 *
 * @param[in]	C OTA file context
 */
static void _closeOtaCtx( OTA_FileContext_t * C )
{
    if( C != NULL )
    {
        C->pucFile = 0;
    }

    /*memset(&ota_ctx, 0, sizeof(esp_ota_context_t)); */
    ota_ctx.cur_ota = 0;
}

/**
 * @brief	Abort OTA transfer
 *
 * Abort receiving the specified OTA update by closing the file.
 *
 * @param[in]	C OTA file context
 */
OTA_Err_t hostOta_Abort( OTA_FileContext_t * const C )
{
    OTA_Err_t ota_ret = kOTA_Err_FileAbort;

    if( _validateOtaCtx( C ) )
    {
        _closeOtaCtx( C );
        ota_ret = kOTA_Err_None;
    }
    else if( C && ( C->pucFile == NULL ) )
    {
        ota_ret = kOTA_Err_None;
    }

    return ota_ret;
}


/**
 * @brief	Create File for Secondary Processor Update
 *
 *	Attempt to create a new receive file for the file chunks as they come in for secondary processor.
 *	Use the supplied partition type/subtype and FilePath from the OTA Context to define the file location.
 *
 *	Example:
 *		Use Partition Type 0x44 (D) and subType 0x57 (W)
 *
 * @param[in]	C OTA file context
 * @param[in]	pParam		Pointer to Partition Descriptor
 */
OTA_Err_t hostOta_CreateFileForRx( OTA_FileContext_t * const C, void *pParam)
{
    if( ( NULL == C ) || ( NULL == C->pucFilePath ) || ( NULL == pParam ) )
    {
        return kOTA_Err_RxFileCreateFailed;
    }

    /* access destination partition information from parameter */
    esp_partition_type_descriptor_t * pPartition = ( esp_partition_type_descriptor_t * )pParam;

	ESP_LOGI( TAG, "find_partition(%s), partition(%02X,%02X)", ( ( char * )C->pucFilePath ), pPartition->type, pPartition->subtype );
	const esp_partition_t * update_partition = esp_partition_find_first( pPartition->type, pPartition->subtype, ( const char * )C->pucFilePath );

	if( update_partition == NULL )
	{
			ESP_LOGE( TAG, "failed to find update partition" );
			return kOTA_Err_RxFileCreateFailed;
	}

	ESP_LOGI( TAG, "Writing to partition subtype %d at offset 0x%x",
              update_partition->subtype, update_partition->address );

	esp_ota_handle_t update_handle;
	esp_err_t err = hostOta_begin( update_partition, OTA_SIZE_UNKNOWN, &update_handle );

	if( err != ESP_OK )
	{
		ESP_LOGE( TAG, "aws_esp_dw_begin failed (%d)", err );
		return kOTA_Err_RxFileCreateFailed;
	}

	ota_ctx.cur_ota = C;
	ota_ctx.update_partition = update_partition;
	ota_ctx.update_handle = update_handle;

	C->pucFile = ( uint8_t * ) &ota_ctx;
	ota_ctx.data_write_len = 0;
	ota_ctx.valid_image = false;

	ESP_LOGI( TAG, "aws_esp_dw_begin succeeded" );

	return kOTA_Err_None;
}

/**
 * @brief	Get Certificate Handle
 */
static CK_RV prvGetCertificateHandle( CK_FUNCTION_LIST_PTR pxFunctionList,
                                      CK_SESSION_HANDLE xSession,
                                      const char * pcLabelName,
                                      CK_OBJECT_HANDLE_PTR pxCertHandle )
{
    CK_ATTRIBUTE xTemplate;
    CK_RV xResult = CKR_OK;
    CK_ULONG ulCount = 0;
    CK_BBOOL xFindInit = CK_FALSE;

    /* Get the certificate handle. */
    if( 0 == xResult )
    {
        xTemplate.type = CKA_LABEL;
        xTemplate.ulValueLen = strlen( pcLabelName ) + 1;
        xTemplate.pValue = ( char * ) pcLabelName;
        xResult = pxFunctionList->C_FindObjectsInit( xSession, &xTemplate, 1 );
    }

    if( 0 == xResult )
    {
        xFindInit = CK_TRUE;
        xResult = pxFunctionList->C_FindObjects( xSession,
                                                 ( CK_OBJECT_HANDLE_PTR ) pxCertHandle,
                                                 1,
                                                 &ulCount );
    }

    if( CK_TRUE == xFindInit )
    {
        xResult = pxFunctionList->C_FindObjectsFinal( xSession );
    }

    return xResult;
}

/**
 * @brief	Get Certificate
 *
 * Note that this function mallocs a buffer for the certificate to reside in,
 * and it is the responsibility of the caller to free the buffer.
 */
static CK_RV prvGetCertificate( const char * pcLabelName,
                                uint8_t ** ppucData,
                                uint32_t * pulDataSize )
{
    /* Find the certificate */
    CK_OBJECT_HANDLE xHandle = 0;
    CK_RV xResult;
    CK_FUNCTION_LIST_PTR xFunctionList;
    CK_SLOT_ID xSlotId;
    CK_ULONG xCount = 1;
    CK_SESSION_HANDLE xSession;
    CK_ATTRIBUTE xTemplate = { 0 };
    uint8_t * pucCert = NULL;
    CK_BBOOL xSessionOpen = CK_FALSE;

    xResult = C_GetFunctionList( &xFunctionList );

    if( CKR_OK == xResult )
    {
        xResult = xFunctionList->C_Initialize( NULL );
    }

    if( ( CKR_OK == xResult ) || ( CKR_CRYPTOKI_ALREADY_INITIALIZED == xResult ) )
    {
        xResult = xFunctionList->C_GetSlotList( CK_TRUE, &xSlotId, &xCount );
    }

    if( CKR_OK == xResult )
    {
        xResult = xFunctionList->C_OpenSession( xSlotId, CKF_SERIAL_SESSION, NULL, NULL, &xSession );
    }

    if( CKR_OK == xResult )
    {
        xSessionOpen = CK_TRUE;
        xResult = prvGetCertificateHandle( xFunctionList, xSession, pcLabelName, &xHandle );
    }

    if( ( xHandle != 0 ) && ( xResult == CKR_OK ) ) /* 0 is an invalid handle */
    {
        /* Get the length of the certificate */
        xTemplate.type = CKA_VALUE;
        xTemplate.pValue = NULL;
        xResult = xFunctionList->C_GetAttributeValue( xSession, xHandle, &xTemplate, xCount );

        if( xResult == CKR_OK )
        {
            pucCert = pvPortMalloc( xTemplate.ulValueLen );
        }

        if( ( xResult == CKR_OK ) && ( pucCert == NULL ) )
        {
            xResult = CKR_HOST_MEMORY;
        }

        if( xResult == CKR_OK )
        {
            xTemplate.pValue = pucCert;
            xResult = xFunctionList->C_GetAttributeValue( xSession, xHandle, &xTemplate, xCount );

            if( xResult == CKR_OK )
            {
                *ppucData = pucCert;
                *pulDataSize = xTemplate.ulValueLen;
            }
            else
            {
                vPortFree( pucCert );
            }
        }
    }
    else /* Certificate was not found. */
    {
        *ppucData = NULL;
        *pulDataSize = 0;
    }

    if( xSessionOpen == CK_TRUE )
    {
        ( void ) xFunctionList->C_CloseSession( xSession );
    }

    return xResult;
}

/**
 * @brief	Read Code Signer Certificate
 */
static u8 * _ReadAndAssumeCertificate( const u8 * const pucCertName,
                                      uint32_t * const ulSignerCertSize )
{
    uint8_t * pucCertData;
    uint32_t ulCertSize;
    uint8_t * pucSignerCert = NULL;
    CK_RV xResult;

    xResult = prvGetCertificate( ( const char * ) pucCertName, &pucSignerCert, ulSignerCertSize );

    if( ( xResult == CKR_OK ) && ( pucSignerCert != NULL ) )
    {
        ESP_LOGI( TAG, "Using cert with label: %s OK\r\n", ( const char * ) pucCertName );
    }
    else
    {
        ESP_LOGI( TAG, "No such certificate file: %s. Using aws_ota_codesigner_certificate.h.\r\n",
                  ( const char * ) pucCertName );

        /* Allocate memory for the signer certificate plus a terminating zero so we can copy it and return to the caller. */
        ulCertSize = sizeof( signingcredentialSIGNING_CERTIFICATE_PEM );
        pucSignerCert = pvPortMalloc( ulCertSize );                           /*lint !e9029 !e9079 !e838 malloc proto requires void*. */
        pucCertData = ( uint8_t * ) signingcredentialSIGNING_CERTIFICATE_PEM; /*lint !e9005 we don't modify the cert but it could be set by PKCS11 so it's not const. */

        if( pucSignerCert != NULL )
        {
            memcpy( pucSignerCert, pucCertData, ulCertSize );
            *ulSignerCertSize = ulCertSize;
        }
        else
        {
            ESP_LOGE( TAG, "Error: No memory for certificate in prvPAL_ReadAndAssumeCertificate!\r\n" );
        }
    }

    return pucSignerCert;
}

/**
 * @brief Verify the signature of the specified file.
 *
 *	The signature is the SHA256 hash of the image that has been cryptographically signed using
 *	the CodeSigning certificate.
 *
 * @param[in]	C OTA file context information.
 *
 * @return The OTA PAL layer error code combined with the MCU specific error code. See OTA Agent
 * error codes information in aws_iot_ota_agent.h.
 */
static OTA_Err_t _CheckFileSignature( OTA_FileContext_t * const C )
{
    OTA_Err_t result;
    uint32_t ulSignerCertSize;
    void * pvSigVerifyContext;
    u8 * pucSignerCert = 0;
    void * buf = NULL;
    size_t size;
    size_t src_offset;
    size_t remaining;


    /* Verify an ECDSA-SHA256 signature. */
    if( CRYPTO_SignatureVerificationStart( &pvSigVerifyContext, cryptoASYMMETRIC_ALGORITHM_ECDSA,
                                           cryptoHASH_ALGORITHM_SHA256 ) == pdFALSE )
    {
        ESP_LOGE( TAG, "signature verification start failed" );
        return kOTA_Err_SignatureCheckFailed;
    }

    pucSignerCert = _ReadAndAssumeCertificate( ( const u8 * const ) C->pucCertFilepath, &ulSignerCertSize );

    if( pucSignerCert == NULL )
    {
        ESP_LOGE( TAG, "cert read failed" );
        return kOTA_Err_BadSignerCert;
    }

    /* allocate a buffer */
	buf = pvPortMalloc( FLASH_READ_BLOCK_SIZE );

	if( NULL != buf )
	{
		/* Read the image, block-by-block.  Can not use the MMU and Cache to map the SPI Flash into address space: encryption may be enabled */
		src_offset = 0;
		for( remaining = ota_ctx.data_write_len; remaining ; ( remaining -= size ), ( src_offset += size ) )
		{
			/* read a block of image */
			size = ( FLASH_READ_BLOCK_SIZE < remaining ) ? FLASH_READ_BLOCK_SIZE : remaining;
			printf("  Reading %d bytes at offset %08X\n", size, src_offset );
			esp_partition_read( ota_ctx.update_partition, src_offset, buf, size );
			/* add to hash */
		    CRYPTO_SignatureVerificationUpdate( pvSigVerifyContext, buf, size );
		}
		vPortFree( buf );
	}
	else
	{
		ESP_LOGE( TAG, "allocating read buffer failed" );
		result = kOTA_Err_SignatureCheckFailed;
		goto end;
	}

	/* Finalize the hash calculation and then verify signature using Code Signer certificate */
    if( CRYPTO_SignatureVerificationFinal( pvSigVerifyContext, ( char * ) pucSignerCert, ulSignerCertSize,
                                           C->pxSignature->ucData, C->pxSignature->usSize ) == pdFALSE )
    {
        ESP_LOGE( TAG, "signature verification failed" );
        result = kOTA_Err_SignatureCheckFailed;
    }
    else
    {
        result = kOTA_Err_None;
    }

end:

    /* Free the signer certificate that we now own after prvReadAndAssumeCertificate(). */
    if( pucSignerCert != NULL )
    {
        vPortFree( pucSignerCert );
    }

    return result;
}

/**
 * @brief	Close the specified file
 *
 * This shall authenticate the file if it is marked as secure.
 *
 * @param[in]	C OTA file context information.
 *
 * @return The OTA PAL layer error code combined with the MCU specific error code. See OTA Agent
 * error codes information in aws_iot_ota_agent.h.
 */
OTA_Err_t hostOta_CloseFile( OTA_FileContext_t * const C )
{
    OTA_Err_t result = kOTA_Err_None;

    if( !_validateOtaCtx( C ) )
    {
        return kOTA_Err_FileClose;
    }

    if( C->pxSignature == NULL )
    {
        ESP_LOGE( TAG, "Image Signature not found" );
        _clearOtaCtx( &ota_ctx );
        result = kOTA_Err_SignatureCheckFailed;
    }
    else if( ota_ctx.data_write_len == 0 )
    {
        ESP_LOGE( TAG, "No data written to partition" );
        result = kOTA_Err_SignatureCheckFailed;
    }
    else
    {
        /* Verify the file signature, close the file and return the signature verification result. */
        result = _CheckFileSignature( C );

        if( result != kOTA_Err_None )
        {
            esp_partition_erase_range( ota_ctx.update_partition, 0, ota_ctx.update_partition->size );
        }
        else
        {
            /* Write ASN1 decoded signature at the end of firmware image for bootloader to validate during bootup */
            esp_sec_boot_sig_t * sec_boot_sig = ( esp_sec_boot_sig_t * ) malloc( sizeof( esp_sec_boot_sig_t ) );

            if( sec_boot_sig != NULL )
            {
                memset( sec_boot_sig->sec_ver, 0x00, sizeof( sec_boot_sig->sec_ver ) );
                memset( sec_boot_sig->pad, 0xFF, sizeof( sec_boot_sig->pad ) );
                result = asn1_to_raw_ecdsa( C->pxSignature->ucData, C->pxSignature->usSize, sec_boot_sig->raw_ecdsa_sig );

                if( result == kOTA_Err_None )
                {
                    esp_err_t ret = hostOta_write( ota_ctx.update_handle, sec_boot_sig, ota_ctx.data_write_len, ECDSA_SIG_SIZE );

                    if( ret != ESP_OK )
                    {
                        return kOTA_Err_FileClose;
                    }

                    ota_ctx.data_write_len += ECDSA_SIG_SIZE;
                }

                free( sec_boot_sig );
                ota_ctx.valid_image = true;
            }
            else
            {
                result = kOTA_Err_SignatureCheckFailed;
            }
        }
    }

    return result;
}

/**
 * @brief	Reset Device
 */
OTA_Err_t IRAM_ATTR hostOta_ResetDevice( void )
{
    /* Short delay for debug log output before reset. */
    vTaskDelay( kOTA_HalfSecondDelay );
    esp_restart();
    return kOTA_Err_None;
}

/**
 * @brief	Activate New Image for Secondary Processor
 */
OTA_Err_t hostOta_ActivateNewImage( void )
{
    if( ota_ctx.cur_ota != NULL )
    {
		/* Secondary Processor */
		if( hostOta_end( ota_ctx.update_handle ) != ESP_OK )
		{
			ESP_LOGE( TAG, "aws_esp_dw_end failed!" );
			esp_partition_erase_range( ota_ctx.update_partition, 0, ota_ctx.update_partition->size );
		}
		_clearOtaCtx( &ota_ctx );
		return kOTA_Err_None;
    }

    _clearOtaCtx( &ota_ctx );
    prvPAL_ResetDevice();
    return kOTA_Err_None;
}


/**
 * @brief Write a block of data to the specified file.
 *
 * @param[in]	C OTA file context information.
 * @param[in]	iOffset	File offset
 * @param[in]	pacData	Pointer to data
 * @param[in]	iBlockSize Size of data to be written
 *
 * @return Number of bytes written, -1 on error
 */
int16_t hostOta_WriteBlock( OTA_FileContext_t * const C,
                           uint32_t iOffset,
                           uint8_t * const pacData,
                           uint32_t iBlockSize )
{
    if( _validateOtaCtx( C ) )
    {
        esp_err_t ret = hostOta_write( ota_ctx.update_handle, pacData, iOffset, iBlockSize );

        if( ret != ESP_OK )
        {
            ESP_LOGE( TAG, "Couldn't flash at the offset %d", iOffset );
            return -1;
        }

        ota_ctx.data_write_len += iBlockSize;
    }
    else
    {
        ESP_LOGI( TAG, "Invalid OTA Context" );
        return -1;
    }

    return iBlockSize;
}




#ifdef AMAZON_FREERTOS_ENABLE_UNIT_TESTS
    #include "aws_ota_pal_test_access_define.h"
#endif
