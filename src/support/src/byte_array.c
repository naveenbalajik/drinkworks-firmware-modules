/**
 * @file	byte_array.c
 */

static char rawBuffer[64];						//57 is good for 28 bytes

/**
 * @brief	Format Byte Array as a hex string
 *
 *	A static format buffer is used, and the array formated.
 *
 *	@param[in]	pData	Pointer to byte array
 *	@param[in]	size	Size of byte array to be printed
 */
static const char* pszNibbleToHex = {"0123456789ABCDEF"};

char * formatHexByteArray(const uint8_t *pData, size_t size )
{
	int		i;
	char	*ptr;
	uint8_t	nibble;

	/* use static buffer */
	ptr = &rawBuffer[0];

	/* format each byte */
	for (i = 0; i < size; ++i)
	{
		nibble = *pData >> 4;							// High Nibble
		*ptr++ = pszNibbleToHex[ nibble ];
		nibble = *pData & 0x0f;							// Low Nibble
		*ptr++ = pszNibbleToHex[ nibble ];
		pData++;
	}
	*ptr = '\0';										// terminate

	return rawBuffer;
}

