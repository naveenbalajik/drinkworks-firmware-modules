/**
 * @file nvs_utility.h
 *
 * Created on: May 4, 2020
 * 		Author: nick.weber
 */

#ifndef NVSUTILITY_H
#define NVSUTILITY_H

#include "nvs.h"
#include "nvsItems.h"				/* Project NVS Item definitions */


typedef struct
{
	const char * 	label;
	const bool 		encrypted;
	bool			initialized;
} NVS_Partition_Details_t;

typedef struct
{
	nvs_type_t			type;
	NVS_Partitions_t	partition;
	const char* 		namespace;
	const char* 		nvsKey;
} NVS_Entry_Details_t;

/**
 * @brief	NVS Item Abstraction struct
 */
typedef	struct
{
	NVS_Partition_Details_t *	partitions;
	size_t						numPartitions;
	const NVS_Entry_Details_t * 		items;
	const size_t						numItems;
} nvsItem_pal_t;



const nvsItem_pal_t * nvsItem_getPAL( void );


int32_t NVS_Initialize( const nvsItem_pal_t * pal );

int32_t NVS_Get_Size_Of(NVS_Items_t nvsItem, uint32_t* size);

int32_t NVS_pGet( const NVS_Entry_Details_t *pItem, void* pOutput, void* pSize );

int32_t NVS_Get(NVS_Items_t nvsItem, void* pOutput, void* pSize);

int32_t NVS_pSet( const NVS_Entry_Details_t *pItem, const void * pInput, size_t * pSize );

int32_t NVS_Set(NVS_Items_t nvsItem, void* pInput, size_t * pSize);

int32_t NVS_EraseKey(NVS_Items_t nvsItem);


#endif // !NVSUTILITY_H
