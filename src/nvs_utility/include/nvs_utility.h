/**
 * @file nvs_utility.h
 *
 * Created on: May 4, 2020
 * 		Author: nick.weber
 */

#ifndef NVSUTILITY_H
#define NVSUTILITY_H

#include "nvs.h"

typedef enum
{
	NVS_PART_NVS,
	NVS_PART_STORAGE,
	NVS_PART_PDATA,
	NVS_PART_XDATA,
	NVS_PART_EDATA,
	NVS_PART_END			/**< End of List */
} NVS_Partitions_t;

typedef struct
{
	const char * 	label;
	const bool 		encrypted;
	bool			initialized;
} NVS_Partition_Details_t;

typedef struct
{
	int					itemIndex;
	nvs_type_t			type;
	NVS_Partitions_t	partition;
	const char* 		namespace;
	const char* 		nvsKey;
} NVS_Entry_Details_t;


typedef enum
{
	NVS_CLAIM_CERT = 0,
	NVS_CLAIM_PRIVATE_KEY,
	NVS_FINAL_CERT,
	NVS_FINAL_PRIVATE_KEY,
	NVS_THING_NAME,
	NVS_SERIAL_NUM
} NVS_Items_t;

int32_t NVS_Initialize( void );

int32_t NVS_Get_Size_Of(NVS_Items_t nvsItem, uint32_t* size);

int32_t NVS_Get(NVS_Items_t nvsItem, void* pOutput, void* pSize);

int32_t NVS_Set(NVS_Items_t nvsItem, void* pInput, void* pSize);

int32_t NVS_EraseKey(NVS_Items_t nvsItem);


#endif // !NVSUTILITY_H
