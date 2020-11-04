/**
 * @file	event_records.h
 *
 * Created on: September 3, 2020
 * Author: Ian Whitehead
 */

#ifndef	EVENT_RECORDS_H
#define	EVENT_RECORDS_H

#include	"event_fifo.h"
#include	"nvs_utility.h"

int32_t eventRecords_init( fifo_handle_t fifo, NVS_Items_t eventRecordKey );

void eventRecords_onChangedTopic( uint32_t lastRecordedEvent );

#endif		/*	EVENT_RECORDS_H	*/
