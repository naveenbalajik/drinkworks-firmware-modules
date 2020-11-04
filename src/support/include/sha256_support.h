/**
 * @file	sha256_support.h
 *
 *	Support functions for handling SHA256 hash values
 *
 * Created on: November 3, 2020
 * Author: Ian Whitehead
 */

#ifndef	_SHA256_SUPPORT_H_
#define _SHA256_SUPPORT_H_

#define	SHA256_LEN	32

/**
 * @brief	SHA256 Hash Value type definition (includes null-terminator)
 */
typedef	struct
{
	uint8_t	x[ SHA256_LEN ];
	uint8_t	terminator;
}	sha256_t;

sha256_t * sha256_generate( uint8_t * pBuffer, size_t length );
char * sha256_format( const char *tag, sha256_t *sha);

#endif	/* _SHA256_SUPPORT_H_ */
