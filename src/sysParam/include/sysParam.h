/**
 * @file	sysParam.h
 *
 * Created on: October 21, 2020
 * Author: Ian Whitehead
 */

#ifndef	SYS_PARAM_H
#define	SYS_PARAM_H

#include "json.h"

typedef struct
{
	const char *topicProduction;				/**< MQTT Topic, Poduction */
	const char *topicDevelop;					/**< MQTT Topic, Develop */
	uint32_t	updateInterval;					/**< Update interval in millisceonds */
	_jsonItem_t *pList;							/**< System Parameter List */
} _sysParamConfig_t;

int32_t sysParam_init( _sysParamConfig_t * config );

#endif		/*	SYS_PARAM_H	*/
