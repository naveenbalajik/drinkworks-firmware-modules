/**
 * @file	reset_support.h
 */

#ifndef	RESET_SUPPORT_H
#define	RESET_SUPPORT_H

/**
 * @brief Callback called on system Reset, for registered reasons
 */
typedef void (* _resetCallback_t)( void );

void reset_ProcessReason( void );
void reset_RegisterCallback( const esp_reset_reason_t reason, const _resetCallback_t handler );

#endif	/* RESET_SUPPORT_H */
