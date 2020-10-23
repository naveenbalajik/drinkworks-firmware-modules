/**
 * @file	json.c
 *
 *	Common json helper functions
 *
 * Created on: October 22, 2020
 * Author: Ian Whitehead
 */
#include	<stdlib.h>
#include	<stdio.h>
#include	<stdint.h>
#include	<stdbool.h>
#include	<string.h>
#include	"mjson.h"
#include	"json.h"
#include	"TimeSync.h"
#include	"nvs_utility.h"
#include	"esp_err.h"

/**
 * @brief Format a JSON Item as a simple key-value pair
 *
 *	Buffer is allocated from heap and must be freed after use.
 *
 * @param[in] pItem			Pointer to JSON Item
 */
static char * _formatItem( _jsonItem_t * pItem )
{

	char *itemJSON = NULL;

	switch( pItem->jType )
	{
		case JSON_STRING:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:%Q}",
					pItem->key,
					pItem->jValue.string
					);
			break;

		case JSON_NUMBER:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:%f}",
					pItem->key,
					*pItem->jValue.number
					);
			break;

		case JSON_INTEGER:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:%d}",
					pItem->key,
					*pItem->jValue.integer
					);
			break;

		case JSON_UINT16:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:%d}",
					pItem->key,
					*pItem->jValue.integerU16
					);
			break;

		case JSON_UINT32:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:%d}",
					pItem->key,
					*pItem->jValue.integerU32
					);
			break;

		case JSON_BOOL:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:%B}",
					pItem->key,
					*pItem->jValue.truefalse
					);
			break;

		case JSON_NONE:
		default:
			break;
	}

	return itemJSON;
}

/**
 * @brief Format a JSON Item with zero static levels
 *
 *	Section value is optional
 *	Buffer is allocated from heap and must be freed after use.
 *
 * @param[in] pItem			Pointer to JSON Item
 */
char * json_formatItem0Level( _jsonItem_t * pItem )
{

	char *itemJSON = NULL;
	char *keyValue = NULL;

	/* format the base key-value pair */
	keyValue = _formatItem( pItem );

	/* Add optional section value */
	if( pItem->section != NULL )
	{
		mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:%s}", pItem->section, keyValue );
		free( keyValue );
	}
	else
	{
		itemJSON = keyValue;
	}

	return itemJSON;
}

/**
 * @brief Format a JSON Item with one static level
 *
 *	Section value is optional
 *	Buffer is allocated from heap and must be freed after use.
 *
 * @param[in] pItem			Pointer to JSON Item
 * @param[in] level1		Level1 Key value
 */
char * json_formatItem1Level( _jsonItem_t * pItem, const char * level1 )
{

	char *itemJSON = NULL;
	char *section = NULL;

	/* format the base key-value pair with option section */
	section = json_formatItem0Level( pItem );

	mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:%s}", level1, section );
	free( section );

	return itemJSON;
}

/**
 * @brief Format a JSON Item with two static levels
 *
 *	Section value is optional
 *	Buffer is allocated from heap and must be freed after use.
 *
 * @param[in] pItem			Pointer to JSON Item
 * @param[in] level1		Level1 Key value
 * @param[in] level2		Level2 Key value
 */
char * json_formatItem2Level( _jsonItem_t * pItem, const char * level1, const char * level2 )
{

	char *itemJSON = NULL;
	char *lower = NULL;

	/* format the base key-value pair with option section, and 1 static level */
	lower = json_formatItem1Level( pItem, level2 );

	mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:%s}", level1, lower );
	free( lower );

	return itemJSON;
}

/**
 * @brief	Format current time as a JSON key-pair, using given key
 *
 * Time value is formatted as fixed length ISO 8601 string.
 * Format buffer is allocated from heap, and must be freed after use.
 *
 * @param[in]	key		Key string to use in JSON format
 * @return		Formatted JSON key-value pair
 */
const char * json_formatUTC( const char * key )
{
	char utc[ 28 ] = { 0 };

	char *buffer = NULL;

	/* Get Current Time, UTC */
	getUTC( utc, sizeof( utc ) );

	/* Format timestamp */
	mjson_printf( &mjson_print_dynamic_buf, &buffer, "{%Q:%Q}", key, utc );

	return buffer;
}

/**
 * @brief	Format Serial Number as a JSON key-value pair
 *
 * Serial Number is fetched from NVS storage.
 * Format buffer is allocated from heap, and must be freed after use.
 *
 * @return		Formatted JSON key-value pair
 */
const char * json_formatSerialNumber( void )
{
    esp_err_t xRet;
	char sernum[13] = { 0 };
	char *buffer = NULL;
	size_t length = sizeof( sernum );

	/* Get Serial Number from NVS */
    xRet = NVS_Get(NVS_SERIAL_NUM, sernum, &length );

	if( xRet == ESP_OK )
	{
		/* Format serial number */
		mjson_printf( &mjson_print_dynamic_buf, &buffer, "{%Q:%Q}", "serialNumber", sernum );
	}

	return buffer;
}

