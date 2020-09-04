/**
 * @file	event_fifo.h
 */

#ifndef	EVENT_FIFO_H
#define EVENT_FIFO_H

#include "nvs_utility.h"

	/* ************************************************************************** */
	/* ************************************************************************** */
	/* Section: Data Types                                                        */
	/* ************************************************************************** */
	/* ************************************************************************** */
/**
 * @brief Opaque circular buffer structure
 */
typedef struct fifo_s fifo_t;

/**
 * @brief Handle type, the way users interact with the API
 */
typedef fifo_t* fifo_handle_t;

fifo_handle_t fifo_init(const NVS_Partitions_t partition, const char * namespace, const char *keyPrefix, const uint16_t nItems, NVS_Items_t controlsKey, NVS_Items_t maxKey );

bool fifo_full( fifo_handle_t fifo );

bool fifo_empty( fifo_handle_t fifo );

uint16_t fifo_capacity( fifo_handle_t fifo );

uint16_t fifo_size( fifo_handle_t fifo );

int32_t fifo_put( fifo_handle_t fifo, const void* blob, size_t length);

int32_t fifo_get( fifo_handle_t fifo, void* blob, size_t *pLength );

uint16_t fifo_getHead( fifo_handle_t fifo);

#endif	/* EVENT_FIFO_H */
