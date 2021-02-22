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


#ifdef	DEPRECIATED
typedef enum
{
	NVS_PART_NVS,
	NVS_PART_STORAGE,
	NVS_PART_PDATA,
	NVS_PART_XDATA,
	NVS_PART_EDATA,
	NVS_PART_END			/**< End of List */
} NVS_Partitions_t;
#endif

typedef struct
{
	const char * 	label;
	const bool 		encrypted;
	bool			initialized;
} NVS_Partition_Details_t;

typedef struct
{
//	int					itemIndex;
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

#ifdef	DEPRECIATED
typedef enum
{
	NVS_CLAIM_CERT = 0,
	NVS_CLAIM_PRIVATE_KEY,
	NVS_FINAL_CERT,
	NVS_FINAL_PRIVATE_KEY,
	NVS_THING_NAME,
	NVS_SERIAL_NUM,
	NVS_FIFO_CONTROLS,
	NVS_FIFO_MAX,
	NVS_EVENT_RECORD,
//	NVS_LAST_PUB_INDEX,
	NVS_PROD_PUB_INDEX,
	NVS_PROD_REC_EVENT,
	NVS_DEV_PUB_INDEX,
	NVS_DEV_REC_EVENT,
	NVS_HOSTOTA_STATE,
	NVS_DATA_SHARE,
	NVS_PRODUCTION
//	NVS_FIFO_TEST						/**< For test purposes only */
} NVS_Items_t;
#endif


const nvsItem_pal_t * nvsItem_getPAL( void );


int32_t NVS_Initialize( const nvsItem_pal_t * pal );

int32_t NVS_Get_Size_Of(NVS_Items_t nvsItem, uint32_t* size);

int32_t NVS_pGet( const NVS_Entry_Details_t *pItem, void* pOutput, void* pSize );

int32_t NVS_Get(NVS_Items_t nvsItem, void* pOutput, void* pSize);

int32_t NVS_pSet( const NVS_Entry_Details_t *pItem, const void * pInput, void * pSize );

int32_t NVS_Set(NVS_Items_t nvsItem, void* pInput, void* pSize);

int32_t NVS_EraseKey(NVS_Items_t nvsItem);


#endif // !NVSUTILITY_H
