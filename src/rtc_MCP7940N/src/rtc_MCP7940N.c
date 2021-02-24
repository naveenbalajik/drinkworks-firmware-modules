/**
 *	@file rtc.c
 *
 *	@brief	Support for Microchip MCP7940N Real Time Clock
 *
 *	Author: I Whitehead
 *	Created on December 18, 2020
 *
 *	Copyright © 2016-2021 Drinkworks. All rights reserved.
 */
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include "driver/i2c.h"
#include "TimeSync.h"
#include "TimeSync_logging.h"
#include "rtc_MCP7940N.h"
#include "esp_log.h"
#include "sdkconfig.h"

extern int SCCB_Init(int pin_sda, int pin_scl);

#if CONFIG_SCCB_HARDWARE_I2C_PORT1
const int RTC_I2C_PORT         = 1;
#else
const int RTC_I2C_PORT         = 0;
#endif

#define WRITE_BIT               I2C_MASTER_WRITE /*!< I2C master write */
#define READ_BIT                I2C_MASTER_READ  /*!< I2C master read */
#define ACK_CHECK_EN            0x1              /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS           0x0              /*!< I2C master will not check ack from slave */
#define ACK_VAL                 0x0              /*!< I2C ack value */
#define NACK_VAL                0x1              /*!< I2C nack value */

static bool	bRtcPresent;						/**< True if RTC has been successfully detected and initialized */



/**
 *	@brief	Convert Binary-Coded-Decimal value to Binary
 *
 *	Converts BCD byte to Binary. No checking is performed to ensure BCD is valid.
 *
 *	@param[in]	bcd	BCD Byte (0x00 - 0x99)
 *
 *	@return Binary Value (0x00 - 0x63)
 */
static uint8_t bcd2bin( uint8_t bcd )
{
	uint8_t bin;

	bin = ( ( ( bcd & 0xF0 ) >> 4 ) * 10 ) + ( bcd & 0x0f );

	return bin;
}


/**
 *	@brief	Convert Binary value to Binary-Coded-Decimal
 *
 *	Converts Binary byte to BCD. No checking is performed to ensure binary value is valid.
 *
 *	@param[in]	Binary Byte (0 - 99, to 0x00 - 0x63)
 *
 *	@return BCD Value (0x00 - 0x99)
 */
static uint8_t bin2bcd(uint8_t bin)
{
	uint8_t bcd;

	bcd =  ( ( bin / 10) << 4 ) + ( bin % 10 );

	return bcd;
}


/**
 * @brief	Read a single RTC Register
 *
 * @param[in]	slv_addr	I2C slave address of the RTC chip
 * @param[in]	reg			RTC register address
 * @param[out]	pData		Destination for value read from the RTC register
 *
 * @return	0 if successful, -1 on error
 */
static int8_t rtc_read_byte( uint8_t slv_addr, uint8_t reg, uint8_t * pData )
{
    esp_err_t ret = ESP_FAIL;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start( cmd );
    i2c_master_write_byte( cmd, ( slv_addr << 1 ) | WRITE_BIT, ACK_CHECK_EN );
    i2c_master_write_byte( cmd, reg, ACK_CHECK_EN );
    i2c_master_stop( cmd );
    ret = i2c_master_cmd_begin( RTC_I2C_PORT, cmd, 1000 / portTICK_RATE_MS );
    i2c_cmd_link_delete( cmd );
    if( ret != ESP_OK ) return -1;

    cmd = i2c_cmd_link_create();
    i2c_master_start( cmd );
    i2c_master_write_byte( cmd, ( slv_addr << 1 ) | READ_BIT, ACK_CHECK_EN );
    i2c_master_read_byte( cmd, pData, NACK_VAL );
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin( RTC_I2C_PORT, cmd, 1000 / portTICK_RATE_MS );
    i2c_cmd_link_delete( cmd );
    if( ret != ESP_OK )
    {
        IotLogError( "rtc_read Failed addr:0x%02x, reg:0x%02x, data:0x%02x, ret:%d", slv_addr, reg, *pData, ret );
    }
    return ret == ESP_OK ? 0 : -1;
}


/**
 * @brief	Read multiple RTC Registers (consecutive)
 *
 * @param[in]	slv_addr	I2C slave address of the RTC chip
 * @param[in]	reg			RTC register address
 * @param[out]	data		Destination for value read from the RTC register
 * @param[in]	data_len	number of registers to be read
 *
 * @return	0 if successful, -1 on error
 */
static int8_t rtc_read( uint8_t slv_addr, uint8_t reg, uint8_t * pData, size_t data_len )
{
    esp_err_t ret = ESP_FAIL;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

//    IotLogInfo( "rtc_read" );
    i2c_master_start( cmd );
    i2c_master_write_byte( cmd, ( slv_addr << 1 ) | WRITE_BIT, ACK_CHECK_EN );
    i2c_master_write_byte( cmd, reg, ACK_CHECK_EN );
    i2c_master_stop( cmd );
    ret = i2c_master_cmd_begin( RTC_I2C_PORT, cmd, 1000 / portTICK_RATE_MS );
    i2c_cmd_link_delete( cmd );
//    IotLogInfo( "  write command, ret = 0x%X", ret );
    if( ret != ESP_OK ) return -1;

    cmd = i2c_cmd_link_create();
    i2c_master_start( cmd );
    i2c_master_write_byte( cmd, ( slv_addr << 1 ) | READ_BIT, ACK_CHECK_EN );
    i2c_master_read( cmd, pData, data_len, I2C_MASTER_LAST_NACK );
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin( RTC_I2C_PORT, cmd, 1000 / portTICK_RATE_MS );
    i2c_cmd_link_delete( cmd );
//    IotLogInfo( "  read command, ret = 0x%X", ret );
    if( ret != ESP_OK )
    {
    	IotLogError( "rtc_read Failed addr:0x%02x, reg:0x%02x, data[0]:0x%02x, ret:%d", slv_addr, reg, pData[0], ret );
    }
    return ret == ESP_OK ? 0 : -1;
}


/**
 * @brief	Write a single RTC Register
 *
 * @param[in]	slv_addr	I2C slave address of the RTC chip
 * @param[in]	reg			RTC register address
 * @param[in]	data		Value to be written to the RTC register
 *
 * @return	0 if successful, -1 on error
 */
static int8_t rtc_write_byte( uint8_t slv_addr, uint8_t reg, uint8_t data )
{
    esp_err_t ret = ESP_FAIL;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start( cmd );
    i2c_master_write_byte( cmd, ( slv_addr << 1 ) | WRITE_BIT, ACK_CHECK_EN );
    i2c_master_write_byte( cmd, reg, ACK_CHECK_EN );
    i2c_master_write_byte( cmd, data, ACK_CHECK_EN );
    i2c_master_stop( cmd );
    ret = i2c_master_cmd_begin( RTC_I2C_PORT, cmd, 1000 / portTICK_RATE_MS );
    i2c_cmd_link_delete( cmd );
    if( ret != ESP_OK )
    {
    	IotLogError( "rtc_write Failed addr:0x%02x, reg:0x%02x, data:0x%02x, ret:%d", slv_addr, reg, data, ret );
    }
    return ret == ESP_OK ? 0 : -1;
}


/**
 * @brief	Write multiple RTC Registers (consecutive)
 *
 * @param[in]	slv_addr	I2C slave address of the RTC chip
 * @param[in]	reg			RTC register starting address
 * @param[in]	data		Pointer to buffer to be written to the RTC registers
 * @param[in]	data_len	Number of registers to write
 *
 * @return	0 if successful, -1 on error
 */
static int8_t rtc_write( uint8_t slv_addr, uint8_t reg, uint8_t * pData, size_t data_len )
{
    esp_err_t ret = ESP_FAIL;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start( cmd );
    i2c_master_write_byte( cmd, ( slv_addr << 1 ) | WRITE_BIT, ACK_CHECK_EN );
    i2c_master_write_byte( cmd, reg, ACK_CHECK_EN );
    i2c_master_write( cmd, pData, data_len, ACK_CHECK_EN );
    i2c_master_stop( cmd );
    ret = i2c_master_cmd_begin( RTC_I2C_PORT, cmd, 1000 / portTICK_RATE_MS );
    i2c_cmd_link_delete( cmd );
    if( ret != ESP_OK )
    {
    	IotLogError( "rtc_write_bytes Failed addr:0x%02x, reg:0x%02x, data[0]:0x%02x, ret:%d", slv_addr, reg, pData[ 0 ], ret );
    }
    return ret == ESP_OK ? 0 : -1;
}


/**
 * @brief	Stop RTC so that the time-keeping registers can be updated reliably.
 *
 * @return
 * 		true = RTC successfully stopped
 *		false = Error accessing RTC
 */
static bool	rtc_StopClock(void)
{
	uint8_t datum = 0;					// Clear the ST bit
	uint8_t	retry;

	if( 0 == rtc_write_byte( RTC_I2C_ADDR, RTC_REG_BLOCK, datum ) )
	{
		for( retry = 0; retry <= RTC_STOP_RETRY; ++retry )										// limit number of times to check the OSCRUN bit
		{
			if( rtc_read_byte( RTC_I2C_ADDR, RTC_WKDAY_REG, &datum ) )							// Read Day-of-Week register
			{
				return false;																	// Error reading RTC
			}
			if( ( datum & RTC_OSCRUN_BIT ) == 0 )												// if OSCRUN bit is clear, Clock is stopped
			{
				return true;
			}
			vTaskDelay( 10 / portTICK_PERIOD_MS );												// yield to lower priority tasks
		}
	}
	return false;																				// Error writing to RTC, or time-out
}


/**
 * @brief	Convert Time Keep register values to POSIX tm
 *
 * @param[in]	ptk	Pointer to source Time Keep register values
 * @param[out]	gm	Pointer to destination tm structure
 */
static void convert_tk2tm( _RTC_TIMEKEEP_t	* ptk, struct tm * gm )
{
	/* convert register values into standard POSIX tm struct */
	gm->tm_year = 100 + bcd2bin( ptk->YEAR );																		// convert year to binary		(Last 2 digits)
	gm->tm_mon = bcd2bin( ptk->MONTH ) - 1;																			// convert month to binary		(1 - 12)
	gm->tm_mday = bcd2bin( ptk->DATE );																				// convert date to binary		(1 - 31)
	gm->tm_wday = bcd2bin( ptk->WKDAY );																			// convert weekday to binary	(1-7; 1=Monday)
	gm->tm_wday = ( gm->tm_wday == 7 ) ? 0 : gm->tm_wday;															/*  RTC [1,7] 1 = Monday -> POSIX: day of week [0,6] 0 = Sunday */
	gm->tm_hour = bcd2bin( ptk->HOURS );																			// convert hour to binary		(00-23)
	gm->tm_min = bcd2bin( ptk->MINUTES );																			// convert minute to binary		(0 - 59)
	gm->tm_sec = bcd2bin( ptk->SECONDS );																			// convert second to binary		(0 - 59)
}


/**
 * @brief	Convert POSIX tm to Time Keep register values
 *
 * POSIX Month values are 0 - 11, Time Keep values are 1 - 12
 * POSIX Year value is the number of years since 1900
 *
 * @param[in]	gm	Pointer to source tm values
 * @param[out]	ptk	Pointer to destination Time Keep register structure
 */
static void convert_tm2tk( struct tm *gm, _RTC_TIMEKEEP_t *ptk )
{
	uint8_t wday;

	ptk->YEAR = bin2bcd( ( gm->tm_year - 100 ) );																	// Convert Year to BCD
	ptk->MONTH = bin2bcd( gm->tm_mon + 1 );																			// Convert Month to BCD
	ptk->DATE = bin2bcd(gm->tm_mday);																				// Convert Date to BCD
	wday = ( gm->tm_wday ? gm->tm_wday : 7 );																		// Convert POSIX Sunday (0) to RTC Sunday (7)
	ptk->WKDAY = bin2bcd( wday );																					// Convert Weekday to BCD
	ptk->HOURS = bin2bcd( gm->tm_hour );																			// Convert Hour to BCD
	ptk->MINUTES = bin2bcd( gm->tm_min );																			// Convert Minute to BCD
	ptk->SECONDS = bin2bcd( gm->tm_sec );																			// Convert Second to BCD,
}


/**
 * @brief	Get Time from MCP7940N RTC
 *
 * @return	Time, in seconds, since the beginning of the Epoch 1/1/1970
 * 			Any error will return -1
 */
static time_t MCP7940N_time_get( void )
{
	_RTC_TIMEKEEP_t	rtc_tkregs;
	struct tm gm;
	time_t	rtcTime = -1;

	/* Only try to access RTC if it is known to be present */
	if( bRtcPresent )
	{
		/* Read RTC Timekeeping registers */
		if( 0 == rtc_read( RTC_I2C_ADDR, RTC_REG_BLOCK, ( uint8_t * )&rtc_tkregs, RTC_TIMEKEEP_SIZE ) )		// Read the 9 Timekeep Registers
		{
			/* convert register values into standard POSIX tm struct */
			convert_tk2tm( &rtc_tkregs, &gm );

			/* convert broken-down time into time since the Epoch */
			rtcTime = mktime( &gm );
		}
	}
	return rtcTime;
}


/**
 * @brief	MCP7940N RTC Set Time
 *
 *	Set Real Time Clock Time keeping registers, and enable device.
 *
 *	http://ww1.microchip.com/downloads/en/DeviceDoc/MCP7940N-Family-Silicon-Errata-80000611D.pdf
 *
 *	Documented Device Errata:
 *		When writing a different value to the month register, RTCMTH (0x05),
 *		it may change the value of the date register, RTCDATE (0x04).
 *	Work around:
 *		After writing to the RTCMTH register, verify the RTCDATE value
 *		is correct or write the correct RTCDATE value again.
 *
 *	Documented Device Errata:
 *		If the RTCWKDAY register is written while the
 *		oscillator is stopped, it is possible that the value
 *		will read back as a different value after the
 *		oscillator is started.
 *	Work around:
 *		Writing the RTCWKDAY register twice, with a delay of 1.5ms between
 *		writes, and the RTC oscillator re-enabled for the delay period.
 *		Also, value is verified after being re-written.
 *
 *	Both of the above issues are addressed together, with the two registers
 *	RTCWKDAY and RTCDATE being written a second time after a 1ms delay.
 *
 * @param[in] time Time value, seconds since epoc (1/1/1900)
 */
static void MCP7940N_time_set( time_t time )
{
	
	_RTC_TIMEKEEP_t	rtc_tkregs;
	time_t	readBack;

	/* Only try to access RTC if it is known to be present */
	if( bRtcPresent )
	{
		/* Convert time value to POSIX stuct tm */
		struct tm *gm = gmtime( &time );
	
		rtc_tkregs.Buff[0] = 0;																							// Clean timekeep structure
		rtc_tkregs.Buff[1] = 0;
		rtc_tkregs.Buff[2] = 0;
		IotLogInfo( "rtc_SetTime" );

		if( rtc_StopClock() )																							// Stop RTC clock, before trying to update timekeeping registers
		{
			/* Convert POSIX tm values to RTC register values (BCD) */
			convert_tm2tk( gm, &rtc_tkregs );

			/* Set additional Time Keepcontrol bits */
			rtc_tkregs.VBATEN = 1;																						// Enable Battery Backup Supply
			rtc_tkregs.FMT12 = 0;																						// Set 24hr mode
			rtc_tkregs.CONTROL = 0x00;																					// disable Square Wave Output and Alarms
			rtc_tkregs.OSCTRIM = 0x00;																					// disable trimming

			IotLogInfo( "MCP7940N_time_set: %02X-%02X-%02X %02X:%02X:%02X",
					rtc_tkregs.YEAR, rtc_tkregs.MONTH, rtc_tkregs.DATE,
					rtc_tkregs.HOURS, rtc_tkregs.MINUTES, rtc_tkregs.SECONDS );

			/* Write to RTC time-keeping registers */
			if( 0 == rtc_write( RTC_I2C_ADDR, RTC_REG_BLOCK, ( uint8_t * )&rtc_tkregs, RTC_TIMEKEEP_SIZE ) )			// Write the Registers
			{
				rtc_tkregs.ST = 1;																						// Enable Oscillator after writing the time registers (otherwise registers may get corrupted)
				if( 0 == rtc_write_byte( RTC_I2C_ADDR, RTC_REG_BLOCK, rtc_tkregs.RTCSEC ) )								// This write Enables Oscillator
				{

					vTaskDelay( 10 / portTICK_PERIOD_MS );																// Delay between writes (NOTE: due to Microchip issue with RTC Chip on certain days)

					/* Write time-keeping register a second time */
					if( rtc_StopClock() )																				// Stop RTC clock, before trying to update timekeeping registers
					{
						rtc_tkregs.ST = 0;																				// Keep Oscillator Disabled after writing the time registers (otherwise registers may get corrupted)
						if( 0 == rtc_write( RTC_I2C_ADDR, RTC_REG_BLOCK, ( uint8_t * )&rtc_tkregs, RTC_TIMEKEEP_SIZE ) )// Write the 9 Timekeep Registers
						{
							rtc_tkregs.ST = 1;																			// Re-enable Oscillator after writing the time registers
							if( 0 == rtc_write_byte( RTC_I2C_ADDR, RTC_REG_BLOCK, rtc_tkregs.RTCSEC ) )					// This write Enables Oscillator
							{
								vTaskDelay( 10 / portTICK_PERIOD_MS );													// Delay between writes (NOTE: due to Microchip issue with RTC Chip on certain days)
								readBack = MCP7940N_time_get();															// Attempt to read RTC
								if( ( readBack - time ) < 2 )															// Read-back value should be with 2 seconds of desired setting
								{
									IotLogInfo( "MCP7940N_time_set: success" );
								}
							}
						}
					}
				}
			}
		}
	}
}


/**
 * @brief	Determine whether the RTC has been configured yet
 *
 *	By default the chip comes up unconfigured. i.e. clock and
 *	oscillator is not running and battery input is not enabled.
 *	After configuration is completed (in rtcSetTime() ), the
 *	configuration remains SET until/unless the battery is removed
 *	or dies (i.e. maintained only in internal RAM). Once configured,
 *	this routine lets us know that the configuration has occurred.
 *
 *	The MCP7940N does not have any indication if a battery is present
 *	other than the VBATEN and OSCRUN bits will be clear. So cannot
 *	distinguish between a device that has never been configured vs
 *	one where the battery has died, by just looking at the device.
 *
 *	If the RTC has not been previously configured, its Time-Keep
 *	registers are set to default values: Friday 01-01-2021 09:00:00
 *
 *	@return:
 *			eRtcStatus_Configured:	OSCRUN and VBATEN are set, i.e. RTC is configured
 *			eRtcStatus_Initialized: OSCRUN and/or VBATEN were not set, default values loaded
 *			eRtcStatus_ComError:	Error accessing RTC
 */
static _rtcStatus_t rtc_IsConfigured(void)
{
	_RTC_TIMEKEEP_t	rtc_tkregs;
	_rtcStatus_t status;
	struct tm gm;																					// Time struct, if we need to set the date/time
	time_t	rtcTime = -1;
	char utc[ 28 ] = { 0 };

	if( rtc_read( RTC_I2C_ADDR, RTC_REG_BLOCK, ( uint8_t * )&rtc_tkregs, RTC_TIMEKEEP_SIZE ) )
	{
		status = eRtcStatus_ComError;											// Communication Error
	}
	else
	{
		if( rtc_tkregs.OSCRUN && rtc_tkregs.VBATEN )
		{
			/* convert register values into standard POSIX tm struct */
			convert_tk2tm( &rtc_tkregs, &gm );

			strftime( utc, sizeof( utc ), "%Y-%m-%dT%H:%M:%SZ", &gm );
			IotLogInfo( "rtc is configured: %s", utc );

			status = eRtcStatus_Configured;									// RTC is configured
		}
		else
		{
			IotLogInfo( "rtc is not initialized, initialize with default date-time" );

			/* default to Friday 01-01-2021 09:00:00 */
			const struct tm gm_default =
			{
				.tm_year = 2021,
				.tm_mon = 1,
				.tm_mday = 1,
				.tm_wday = 5,
				.tm_hour = 9,
				.tm_min = 00,
				.tm_sec = 00,
			};

			/* convert broken-down time into time since the Epoch */
			rtcTime = mktime( ( struct tm * )&gm_default );

			MCP7940N_time_set( rtcTime );											// Set RTC
			status = eRtcStatus_Initialized;										//RTC successfully initialized
		}
	}
	return status;
}


/**
 * @brief	Initialize MCP7940N Real Time Clock
 *
 *	Perform hardware initialization of interface to RTC. The RTC is connected
 *	to a shared I2C bus, which may or may not be already configured, and driver installed.
 *
 *	To handle both cases, i2c_master_cmd_begin() is invoked: If the driver is not installed
 *	the call will fail return ESP_ERR_INVALID_STATE. At which point the GPIO pins controlling
 *	the I2C bus can be configured, then the I2C driver installed.
 *
 *	Function rtc_IsConfigured() first calls i2c_master_cmd_begin(), so is used to achieve
 *	the required probing.
 */
static void	MCP7940N_init(void)
{
	bRtcPresent = false;																	// default to not present

	if( eRtcStatus_ComError == rtc_IsConfigured() )											// determine if RTC is configured
	{
		IotLogInfo( "RTC is not configured - I2C driver may not be installed" );

		/* Try initializing I2C port */
		int pin_sscb_sda = 26;               /*!< GPIO pin for camera SDA line */
		int pin_sscb_scl = 33;               /*!< GPIO pin for camera SCL line */
		SCCB_Init( pin_sscb_sda, pin_sscb_scl);

		if( eRtcStatus_ComError == rtc_IsConfigured() )										// determine if RTC is configured
		{
			IotLogInfo( "RTC not detected" );
		}
		else
		{
			bRtcPresent = true;
		}
	}
	else
	{
		bRtcPresent = true;
	}
}

/* ************************************************************************* */
/* ************************************************************************* */
/* **********        I N T E R F A C E   F U N C T I O N S        ********** */
/* ************************************************************************* */
/* ************************************************************************* */

/**
 * @brief	Hardware Abstraction Layer (HAL)
 *
 * All accesses to the RTC hardware are directed through the HAL structure
 */
const rtc_hal_t MCP7940N_HAL =
{
	.init		= MCP7940N_init,
	.getTime	= MCP7940N_time_get,
	.setTime	= MCP7940N_time_set
};

/**
 * @brief	Get HAL for MCP7940N RTC
 */
const rtc_hal_t	* MCP7940N_getHAL( void )
{
	return &MCP7940N_HAL;
}

