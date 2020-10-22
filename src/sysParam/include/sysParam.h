/**
 * @file	sysParam.h
 *
 * Created on: October 21, 2020
 * Author: Ian Whitehead
 */

#ifndef	SYS_PARAM_H
#define	SYS_PARAM_H

typedef enum
{
	JSON_NONE,
	JSON_STRING,
	JSON_NUMBER,
	JSON_INTEGER,
	JSON_UINT16,
	JSON_UINT32,
	JSON_BOOL
} json_type_t;

typedef union
{
	char *string;
	double *number;
	int16_t *integer;
	uint16_t *integerU16;
	uint32_t *integerU32;
	bool *truefalse;
} json_value_t;

typedef	struct {
	const char *			section;
	const char *			key;
	const json_type_t		jType;
	json_value_t			jValue;
	bool					bUpdate;			/**< If true, Shadow update is required for item */
} _paramItem_t;

typedef struct
{
	const char *topicProduction;				/**< MQTT Topic, Poduction */
	const char *topicDevelop;					/**< MQTT Topic, Develop */
	uint32_t	updateInterval;					/**< Update interval in millisceonds */
	_paramItem_t *pList;						/**< System Parameter List */
} _sysParamConfig_t;

int32_t sysParam_init( _sysParamConfig_t * config );

#endif		/*	SYS_PARAM_H	*/
