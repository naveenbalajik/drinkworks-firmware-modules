/**
 * @file	json.h
 *
 * Created on: October 22, 2020
 * Author: Ian Whitehead
 */

#ifndef	_JSON_H_
#define	_JSON_H_

#include	<stdint.h>

typedef enum
{
	JSON_NONE,
	JSON_STRING,
	JSON_NUMBER,
	JSON_INTEGER,
	JSON_UINT16,
	JSON_INT32,
	JSON_UINT32,
	JSON_BOOL
} _json_type_t;

typedef union
{
	char *string;
	double *number;
	int16_t *integer;
	uint16_t *integerU16;
	int32_t *integer32;
	uint32_t *integerU32;
	bool *truefalse;
} _json_value_t;

typedef	struct
{
	const char *			section;
	const char *			key;
	const _json_type_t		jType;
	_json_value_t			jValue;
	bool					bUpdate;			/**< If true, update is required for item */
} _jsonItem_t;

char * json_formatItem0Level( _jsonItem_t * pItem );
char * json_formatItem1Level( _jsonItem_t * pItem, const char * level1 );
char * json_formatItem2Level( _jsonItem_t * pItem, const char * level1, const char * level2 );
const char * json_formatUTC( const char * key );
const char * json_formatSerialNumber( void );

#endif		/*	_JSON_H_	*/
