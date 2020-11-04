/**
 * @file	sha256.c
 *
 *	Support functions for handling SHA256 hash values
 *
 * Created on: November 3, 2020
 * Author: Ian Whitehead
 */
#include	"iot_config.h"

#include	<stdlib.h>
#include	<stdio.h>
#include	<stdint.h>
#include	<stdbool.h>
#include	<string.h>
#include	"../../../../freertos/libraries/3rdparty/mbedtls/include/mbedtls/sha256.h"
#include	"sha256_support.h"


#define	SHA256_FORMAT_BUFFER_LEN	120

/* ************************************************************************* */
/* ************************************************************************* */
/* **********        I N T E R F A C E   F U N C T I O N S        ********** */
/* ************************************************************************* */
/* ************************************************************************* */

/**
 * @brief	Generate an SHA256 hash on the input buffer
 *
 * SHA256 Hash is copied to a buffer allocated from the heap.  It must be freed after processing.
 *
 * @param[in]	pBuffer	Pointer to input buffer
 * @param[in]	length	Size of input buffer
 *
 * @return		pointer to generated hash
 */
sha256_t * sha256_generate( uint8_t * pBuffer, size_t length )
{
    sha256_t *	pHash;
	mbedtls_sha256_context ctx;

	/* Allocate an SHA256 object */
	pHash = pvPortMalloc( sizeof( sha256_t ) );

	if( NULL != pHash )
	{
		mbedtls_sha256_init( &ctx );
		mbedtls_sha256_starts( &ctx, 0 );						/* SHA-256, not 224 */
		mbedtls_sha256_update( &ctx, pBuffer, length );
		mbedtls_sha256_finish( &ctx, ( uint8_t * ) pHash );		/* get result */
	}
	return pHash;
}

/**
 * @brief	Format SHA256 hash value as a string with a tag
 *
 * Format buffer is allocated from heap and must be freed once processed
 *
 * buffer size:
 * 	Const tag length = 6
 * 	If max tag length = 9
 * 	Hash length = 32 * 3 = 96
 * 	terminator = 1
 * 	Total length = 6 + 9 + 96 + 1 = 112
 *
 */
char * sha256_format( const char *tag, sha256_t *sha)
{
	int i;
	char * pBuffer;
	char temp[ 3 ];

	/* Allocate a format buffer */
	pBuffer = pvPortMalloc( SHA256_FORMAT_BUFFER_LEN );

	if( NULL != pBuffer )
	{
		snprintf( pBuffer, SHA256_FORMAT_BUFFER_LEN, "SHA256%s: ", tag );

		for (i = 0; i < SHA256_LEN; ++i)
		{
			/* format each byte */
			snprintf( temp, 3, "%02x", sha->x[ i ] );
			/* append to format buffer */
			strlcat( pBuffer, temp, SHA256_FORMAT_BUFFER_LEN );
		}
	}
	return pBuffer;
}




