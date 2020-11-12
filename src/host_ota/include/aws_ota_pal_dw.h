// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _AWS_OTA_PAL_H
#define _AWS_OTA_PAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_partition.h"
#include "esp_spi_flash.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief	ESP Partition Type Descriptor
 *
 * Provide a mechanism to convey destination partition information to the ESP OTA PAL
 * layer, without needing to expose details to the generic AWS OTA PAL layer.
 * Structure pointer is passed as a void pointer and cast appropriately at the caller
 * and callee.
 */
typedef struct
{
	esp_partition_type_t type;
	esp_partition_subtype_t subtype;
} esp_partition_type_descriptor_t;

OTA_Err_t prvPAL_dw_Abort( OTA_FileContext_t * const C );

/**
 * @brief	Create a new receive file for the data chunks as they come in.
 *
 * @note Opens the file indicated in the OTA file context in the MCU file system. The parameter pointer
 * provides flexibility to define the destination.  For example it can be used as a Partition Descriptor
 * pointer, containing type and subtype values, to support updating of secondary processors.
 *
 * @note The previous image may be present in the designated image download partition or file, so the partition or file
 * must be completely erased or overwritten in this routine.
 *
 * @note The input OTA_FileContext_t C is checked for NULL by the OTA agent before this
 * function is called.
 * The device file path is a required field in the OTA job document, so C->pucFilePath is
 * checked for NULL by the OTA agent before this function is called.
 *
 * @param[in]	C OTA file context information.
 * @param[in]	pParam	Parameter pointer (e.g. Partition Descriptor pointer)
 *
 * @return The OTA PAL layer error code combined with the MCU specific error code. See OTA Agent
 * error codes information in aws_iot_ota_agent.h.
 *
 * kOTA_Err_None is returned when file creation is successful.
 * kOTA_Err_RxFileTooLarge is returned if the file to be created exceeds the device's non-volatile memory size contraints.
 * kOTA_Err_BootInfoCreateFailed is returned if the bootloader information file creation fails.
 * kOTA_Err_RxFileCreateFailed is returned for other errors creating the file in the device's non-volatile memory.
 */
OTA_Err_t prvPAL_dw_CreateFileForRx( OTA_FileContext_t * const C, void *pParam);

OTA_Err_t prvPAL_dw_CloseFile( OTA_FileContext_t * const C );
OTA_Err_t IRAM_ATTR prvPAL_dw_ResetDevice( void );
OTA_Err_t prvPAL_dw_ActivateNewImage( void );
int16_t prvPAL_dw_WriteBlock( OTA_FileContext_t * const C,
                           uint32_t iOffset,
                           uint8_t * const pacData,
                           uint32_t iBlockSize );



#ifdef __cplusplus
}
#endif

#endif /* _AWS_OTA_PAL_H */
