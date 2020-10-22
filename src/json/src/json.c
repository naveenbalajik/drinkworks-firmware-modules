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

/**
 * @brief Format a JSON Item with zero static levels
 *
 *	Assumes that Item has a Section
 *
 * @param[in] pItem			Pointer to JSON Item
 */
char * json_formatItem0Level( _jsonItem_t * pItem )
{

	char *itemJSON = NULL;

	switch( pItem->jType )
	{
		case JSON_STRING:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:%Q}}",
					pItem->section,
					pItem->key,
					pItem->jValue.string
					);
			break;

		case JSON_NUMBER:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:%f}}",
					pItem->section,
					pItem->key,
					*pItem->jValue.number
					);
			break;

		case JSON_INTEGER:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:%d}}",
					pItem->section,
					pItem->key,
					*pItem->jValue.integer
					);
			break;

		case JSON_UINT16:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:%d}}",
					pItem->section,
					pItem->key,
					*pItem->jValue.integerU16
					);
			break;

		case JSON_UINT32:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:%d}}",
					pItem->section,
					pItem->key,
					*pItem->jValue.integerU32
					);
			break;

		case JSON_BOOL:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:%B}}",
					pItem->section,
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
 * @brief Format a JSON Item with two static levels
 *
 *	Assumes that Item has a Section
 *
 * @param[in] pItem			Pointer to JSON Item
 */
char * json_formatItem2Level( _jsonItem_t * pItem, const char * level1, const char * level2 )
{

	char *itemJSON = NULL;

	switch( pItem->jType )
	{
		case JSON_STRING:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:{%Q:{%Q:%Q}}}}",
					level1,
					level2,
					pItem->section,
					pItem->key,
					pItem->jValue.string
					);
			break;

		case JSON_NUMBER:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:{%Q:{%Q:%f}}}}",
					level1,
					level2,
					pItem->section,
					pItem->key,
					*pItem->jValue.number
					);
			break;

		case JSON_INTEGER:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:{%Q:{%Q:%d}}}}",
					level1,
					level2,
					pItem->section,
					pItem->key,
					*pItem->jValue.integer
					);
			break;

		case JSON_UINT16:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:{%Q:{%Q:%d}}}}",
					level1,
					level2,
					pItem->section,
					pItem->key,
					*pItem->jValue.integerU16
					);
			break;

		case JSON_UINT32:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:{%Q:{%Q:%d}}}}",
					level1,
					level2,
					pItem->section,
					pItem->key,
					*pItem->jValue.integerU32
					);
			break;

		case JSON_BOOL:
			mjson_printf( &mjson_print_dynamic_buf, &itemJSON, "{%Q:{%Q:{%Q:{%Q:%B}}}}",
					level1,
					level2,
					pItem->section,
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


