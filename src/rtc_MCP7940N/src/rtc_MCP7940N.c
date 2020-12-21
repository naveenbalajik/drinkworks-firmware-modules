/**  *************************************************************************
 *
 *	@file rtc.c
 *
 *	@brief	Support for Microchip MCP7940N Real Time Clock
 *
 *	Author: I Whitehead
 *	Created on December 18, 2020
 *
 *	Copyright © 2016-2020 Drinkworks. All rights reserved.
 */
//***************************************************************************
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include "driver/i2c.h"
#include "TimeSync.h"
#include "TimeSync_logging.h"
#include "rtc_MCP7940N.h"
#include "esp_log.h"
#include "sdkconfig.h"

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

#ifdef LATER
_RTC_TIMEKEEP_t	timekeep;
_RTC_PWR_REGS_t pwrReg;
_rtcStatus_t	rtcConfigStatus;												// RTC configuration status

TIME	CurrentTime;																// Current Date/Time - Used for determining when "Cleaning Required" messages put up
TIME	PwrDownTime;																// Records the Time when the AC power went down (used in run-time for comparison for 7-day cleaning message)
PWRFAIL PwrDownCalc;																// Used to review Power Up/Down registers and calculate a down time
bool	rtcPwrFailStatus;															// Used to save the status of rtcPwrFail(), called during system initialization

unsigned char	RTCValid = 0;													// used in rtcUpdateTime() to indicate if real time clock read was valid


//
//	Accumulated number of days in preceeding months
// Index is 1-based, i.e. January = 1
//
const unsigned int monthDays[] = {0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

void rtcLinearTest(void);
int rtcToLinearT(TIME *pt);
TIME rtcFromLinearT(int LinearTime);

typedef enum {
	RTC_SET_TIME_IDLE,
	RTC_SET_TIME_UPDATE
} RTC_SET_TIME_STATES;																				// rtcSetTimeTask() states

RTC_SET_TIME_STATES rtcSetTimeState = RTC_SET_TIME_IDLE;
TIME	NewRtcTime;																						// New RTC time value, set by Bluetooth code
bool	NewRtcTimeAvailable = false;																// Flag set to true when new RTC time value is available, set by Bluetooth code


//Days In Months
// Index is 1-base, i.e. January = 1
const unsigned char daysInMonth[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};

const unsigned char daysInMonthExt[2][13] = {
	{0,31,28,31,30,31,30,31,31,30,31,30,31},
	{0,31,29,31,30,31,30,31,31,30,31,30,31}
};

// MinutesInYear[] gives the number of minutes in a year, when the index is (year%4)		'RTC09
// It is accurate for years 2000 thru 2099
int MinutesInYear[] = {MINUTES_IN_YEAR + MINUTES_IN_DAY, MINUTES_IN_YEAR, MINUTES_IN_YEAR, MINUTES_IN_YEAR};

#endif

//
//	Day-of-Week
//
//	MCP7940N RTC maintains a Day-of-Week Register: "Contains a value from 1 to 7. The representation is user-defined."
// org.bluetooth.characteristic.day_of_week defines:  1 = Monday, 2 = Tuesday .... 7 = Sunday
//
//	The Bluetooth definition is used, and important in computing year value in rtcSetYear()

//*********************************************************************
//**							bcd2bin()
//**				CONVERT BINARY-CODED-DECIMAL TO BINARY
//**
//**	Converts BCD byte to Binary. No checking is performed to ensure
//**	BCD is valid.
//**
//**	INPUTS:  BCD Byte (0x00 - 0x99)
//**
//**	OUTPUTS: Binary Value (0x00 - 0x63)
//**
//*********************************************************************
static uint8_t bcd2bin( uint8_t bcd )
{
	uint8_t bin;

	bin = ( ( ( bcd & 0xF0 ) >> 4 ) * 10 ) + ( bcd & 0x0f );

	return bin;
}



//*********************************************************************
//**							bcd2bin()
//**				CONVERT BINARY TO BINARY-CODED-DECIMAL
//**
//**	Converts Binary Byte to BCD byte. No checking is performed to ensure
//**	Binary value can be converted (i.e. bin <= 99).
//**
//**	INPUTS:  Binary Byte (0 - 99 or 0x00 - 0x63)
//**
//**	OUTPUTS: BCD Value (0x00 - 0x99)
//**
//*********************************************************************
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

//*********************************************************************
//**							rtcStopClock()
//**						STOP RTC CLOCK
//**
//**	Stop RTC so that the time-keeping registers can be updated reliably.
//**
//**	INPUTS:  None
//**
//**	OUTPUTS:
//**				true = RTC successfully stopped
//**				false = Error accessing RTC
//**
//*********************************************************************
static bool	rtc_StopClock(void)
{
	uint8_t datum = 0;					// Clear the ST bit
	uint8_t	retry;

//	IotLogInfo("rts_StopClock" );
	if( 0 == rtc_write_byte( RTC_I2C_ADDR, RTC_REG_BLOCK, datum ) )
	{
//		IotLogInfo( "  write: %02X", datum );
		for( retry = 0; retry <= RTC_STOP_RETRY; ++retry )										// limit number of times to check the OSCRUN bit
		{
			if( rtc_read_byte( RTC_I2C_ADDR, RTC_WKDAY_REG, &datum ) )						// Read Day-of-Week register
			{
//				IotLogInfo( "  FALSE: read: %02X", datum );
				return false;																	// Error reading RTC
			}
			if( ( datum & RTC_OSCRUN_BIT ) == 0 )												// if OSCRUN bit is clear, Clock is stopped
			{
//				IotLogInfo( "  TRUE: read: %02X", datum );
				return true;
			}
//			IotLogInfo( "  read: %02X", datum );
		}
	}
	return false;																				// Error writing to RTC, or time-out
}


#ifdef	LATER

//*********************************************************************
//**							rtcPwrFail()
//**			DETERMINE POWER FAILURE INTERVAL DURATION USING RTC
//**
//**	Determine if there has been a power failure and if so read the power
//**	up/down registers and determine how long the power was out. Since the
//**	power-up/down registers do not include year values, the year must be
//**	inferred.  It is assumed that the current year is the power-up year,
//**  this leaves a small window for error, if the system is powered up just
//**	before midnight on 12/31, and the clock rolls over to 1/1 before this
//**	function is called.  For Power-down, the weekday register value is used,
//**	in conjunction with the month and date values, to determine the first
//**	possible year, working backwards from the current year.  This should
//**	give accurate power-fail intervals in excess of 4 years.
//**
//**	The intent is that this function should be called once, upon power-up
//**
//**	***********************************************************************
//**	=======================================================================
//**
//**	NOTE: The MCP7940N Latches the Power-Up Power-Down times only if the
//**	PWRFAIL bit is clear:
//**		"The PWRFAIL bit must be cleared to log new time-stamp data. This 
//**		is to ensure previous time-stamp data is not lost."
//**
//**	This function performs the PWRFAIL bit clear operation
//**
//**	=======================================================================
//**	***********************************************************************
//**
//**	INPUTS:  ppf - pointer to Power-Fail structure; pdt - pointer to
//**				TIME structure for the read Power-Down Time
//**
//**	OUTPUTS: Returns true if RTC Power Registers were successfully accessed
//**				and they contain Power Failure data, otherwise return false
//**				Power-Fail structure is updated with Power-Fail											~~?? Where?	Also what if battery fails, do we know? How?
//**
//*********************************************************************
bool rtcPwrFail(PWRFAIL *ppf, TIME *pdt)														// Power fail structure AND TIME structure for ONLY the calculated PowerDown Time/Date
{
	unsigned char datum;
	unsigned char year;
	bool	status;
	
	if (I2C_ReadBytes(I2C_RTCC, RTC_I2C_ADDR, RTC_WKDAY_REG, 1, &datum) != 1) {	// Read Day-of-Week register
		return false;																					// Error reading RTC
	}
	if (I2C_ReadBytes(I2C_RTCC, RTC_I2C_ADDR, RTC_YEAR_REG, 1, &year) != 1) {		// Read Year register
		return false;
	}
	if ((datum & RTC_PWRFAIL_BIT) == RTC_PWRFAIL_BIT) {									// if PWRFAIL bit is set, Power Failure was detected
																											// Read RTC Power registers
		if (I2C_ReadBytes(I2C_RTCC, RTC_I2C_ADDR, RTC_PWR_REG, sizeof(_RTC_PWR_REGS_t), (unsigned char *)&pwrReg) == sizeof(_RTC_PWR_REGS_t)) {

			ppf->down.minute = bcd2bin(pwrReg.PWRDN.MINUTE);								// Convert Power-down Minute to binary				~~?? are only most recent  up/down saved - yes? what happens if power cycled quickly after appliance down for a long time?
			ppf->down.hour = bcd2bin(pwrReg.PWRDN.HOUR);										// Convert Power-down Hour to binary
			ppf->down.date = bcd2bin(pwrReg.PWRDN.DATE);										// Convert Power-down Date to binary
			ppf->down.weekday = bcd2bin(pwrReg.PWRDN.WKDAY);								// Convert Power-down Weekday to binary
			ppf->down.month = bcd2bin(pwrReg.PWRDN.MONTH);									// Convert Power-down Month to binary
			
			ppf->up.minute = bcd2bin(pwrReg.PWRUP.MINUTE);									// Convert Power-up Minute to binary
			ppf->up.hour = bcd2bin(pwrReg.PWRUP.HOUR);										// Convert Power-up Hour to binary
			ppf->up.date = bcd2bin(pwrReg.PWRUP.DATE);										// Convert Power-up Date to binary
			ppf->up.weekday = bcd2bin(pwrReg.PWRUP.WKDAY);									// Convert Power-up Weekday to binary
			ppf->up.month = bcd2bin(pwrReg.PWRUP.MONTH);										// Convert Power-up Month to binary
			
			status = rtcPwrOutage(ppf, bcd2bin(year));										// Calculate Power Outage duration, save status

											
			pdt->hour = ppf->down.hour;															// ***** Write the newly calculated Power Down Time into a TIME structure (used in Standby)
			pdt->minute = ppf->down.minute;
			pdt->second = 0;																			// Second not recorded by RTC Registers
			pdt->year = (unsigned char) (ppf->down.year - 2000);							// Year not recorded by RTC Registers, but calculated above in rtcPwroutage())
			pdt->month = ppf->down.month;
			pdt->date = ppf->down.date;
			pdt->weekday = ppf->down.weekday;
			
			
																													// Write RTCWKDAY to clear PWRFAIL flag (PWRFAIL bit cannot be written to a '1')
			if (I2C_WriteBytes(I2C_RTCC, RTC_I2C_ADDR, RTC_WKDAY_REG, 1, &datum) == 1) {
				return status;																					// return status of rtcPwrFail function
			}
		}
	}
	return false;
}

//*********************************************************************
//**							rtcPwrOutagr()
//**			CALCULATE THE POWER OUTAGE TIME
//**
//**	Calculate the power outage time based on the up and down values in the
//**	PWRFAIL structure pointed to, and current year.  Normally these values
//**	will be read from the RTC, however for testing the structure can be filled
//**	programmatically and the algorithm tested with various data sets.
//**
//**	Since the
//**	power-up/down registers do not include year values, the year must be
//**	inferred.  It is assumed that the current year is the power-up year,
//**  this leaves a small window for error, if the system is powered up just
//**	before midnight on 12/31, and the clock rolls over to 1/1 before this
//**	function is called.  For Power-down, the weekday register value is used,
//**	in conjunction with the month and date values, to determine the first
//**	possible year, working backwards from the current year.  This should
//**	give accurate power-fail intervals in excess of 4 years.
//**
//**
//**	INPUTS:  ppf - pointer to Power-Fail structure
//**				year - current year value, from RTC
//**
//**	OUTPUTS: Returns true if Power Outage value is valid, false if invalid
//**				Power-Fail structure is updated with Power Outage time
//**
//*********************************************************************
bool rtcPwrOutage(PWRFAIL *ppf, unsigned char year)
{
	int y;
	long deltaMinutes;

	rtcSetYear(&ppf->up, year);															// Set Power-up Year
	rtcSetYear(&ppf->down, year);															// Set Power-down Year, adjust until weekday matches

	if (ppf->up.year < ppf->down.year) {												// If power-up year is less than power-down year
		return false;																			// we have an error with the input data
	}
	
	rtcMinutesIntoYear(&ppf->down);														// calculate minutes into year for power-down
	rtcMinutesIntoYear(&ppf->up);															// calculate minutes into year for power-up
																									// compute power fail time
	deltaMinutes = ppf->up.minutesIntoYear - ppf->down.minutesIntoYear;		// subtract minutes

	for (y = ppf->down.year; y != ppf->up.year; ++y) {								// for each year from power-down to power-up
		 deltaMinutes += (DaysInYear(y) * MINUTES_IN_DAY);							// add in minutes for that year
	}

	ppf->day = deltaMinutes / MINUTES_IN_DAY;											// calculate power-fail days
	ppf->hour = (deltaMinutes % MINUTES_IN_DAY) / MINUTES_IN_HOUR;				// calculate power-fail hours
	ppf->minute = deltaMinutes % MINUTES_IN_HOUR;									// calculate power-fail minutes
	
	return true;
}

//**************************************************************************************
//**							rtcCalcDeltaTime()
//**			DETERMINE INTERVAL DURATION BETWEEEN 2 TIMES USING RTC
//**
//**	The RTC will be used to determine how long it has been between
//**	cleaning cycles. Currently we request (not force) the consumer
//**	to clean the appliance every 30 days. After 30 days, a cleaning
//**	message stays on the display during IDLE until the appliance is
//**	cleaned.
//**
//**	The intent is that this function should be called periodically,
//**	i.e. once every 15 minutes.
//**
//**	INPUTS:  TIME OlderTime - pointer to time when appliance last cleaned
//**				TIME NewerTime - pointer to Current Time
//**
//**	OUTPUTS: Returns the Delta Days between last cleaning and now
//**
//**************************************************************************************
unsigned short rtcCalcDeltaTime(TIME *OlderTime, TIME *NewerTime)
{
	PWR_TIME	t0;																						// OlderTime Extended with extra variables for calculation
	PWR_TIME	t1;																						// NewerTime Extended with extra variables for calculation
	
	int y;
	unsigned short DeltaDays;																		// Amount of Days that have passed between two Date/Times
	signed long deltaMinutes;																		// Delta Minutes that have passed between two Date/Times
	
	t0.year = OlderTime->year;																		// Transfer to t0
	t0.month = OlderTime->month;
	t0.date = OlderTime->date;
	t0.hour = OlderTime->hour;
	t0.minute = OlderTime->minute;

	t1.year = NewerTime->year;																		// Transfer to t1
	t1.month = NewerTime->month;
	t1.date = NewerTime->date;
	t1.hour = NewerTime->hour;
	t1.minute = NewerTime->minute;
	
	if (t1.year < t0.year) {																		// IFF the Older year is higher than the newer one (i.e. 2090 and 2120 - 90 & 20)
		t1.year += 100;																				// Add 100 to compensate
	}
		
	rtcMinutesIntoYear(&t0);																		// calculate cumulative minutes to date/time in the current year of the Last Cleaning Time
	rtcMinutesIntoYear(&t1);																		// calculate cumulative minutes to date/time in the current year of Current Time

	deltaMinutes = t1.minutesIntoYear - t0.minutesIntoYear;								// subtract minutes (i.e. delta minutes within 1 year ONLY)

	for (y = t0.year; y != t1.year; ++y) {														// for each year between last cleaning and current time...
		 deltaMinutes += (DaysInYear(y) * MINUTES_IN_DAY);									// add in minutes for that year (i.e. up to 3 more years of minutes added if "off time" from prior year)
	}

	DeltaDays = deltaMinutes / MINUTES_IN_DAY;												// calculate Delta days
																											// Write RTCWKDAY to clear PWRFAIL flag (PWRFAIL bit cannot be written to a '1')
	return DeltaDays;
}

#endif

/**
 * @brief	Convert Time Keep register values to POSIX tm
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

//*********************************************************************
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

	/* Read RTC Timekeeping registers */
	if( 0 == rtc_read( RTC_I2C_ADDR, RTC_REG_BLOCK, ( uint8_t * )&rtc_tkregs, RTC_TIMEKEEP_SIZE ) )		// Read the 9 Timekeep Registers
	{
		/* convert register values into standard POSIX tm struct */
		convert_tk2tm( &rtc_tkregs, &gm );

		/* convert broken-down time into time since the Epoch */
		rtcTime = mktime( &gm );
	}
	return rtcTime;
}

//*********************************************************************
//**							rtcSetTime()
//**				SET REAL TIME CLOCK
//**
//**	Set Real Time Clock Time keeping registers, and enable device.
//**
//**	http://ww1.microchip.com/downloads/en/DeviceDoc/MCP7940N-Family-Silicon-Errata-80000611D.pdf
//**
//**	Documented Device Errata:
//**		When writing a different value to the month register, RTCMTH (0x05),
//**		it may change the value of the date register, RTCDATE (0x04).
//**	Work around:
//**		After writing to the RTCMTH register, verify the RTCDATE value
//**		is correct or write the correct RTCDATE value again.
//**
//**	Documented Device Errata:
//**		If the RTCWKDAY register is written while the
//**		oscillator is stopped, it is possible that the value
//**		will read back as a different value after the
//**		oscillator is started.
//**	Work around:
//**		Writing the RTCWKDAY register twice, with a delay of 1.5ms between
//**		writes, and the RTC oscillator re-enabled for the delay period.
//**		Also, value is verified after being re-written.
//**
//**	Both of the above issues are addressed together, with the two registers
//**	RTCWKDAY and RTCDATE being written a second time after a 1ms delay.
//**
//**	INPUTS:  Pointer to TIME structure containing time value to be set
//**
//**	OUTPUTS: 
//**				true - RTC write completed without error
//**				false - RTC write/verification error
//**
//*********************************************************************
void MCP7940N_time_set( time_t time )
{
	
	_RTC_TIMEKEEP_t	rtc_tkregs;
//	struct tm timestamp;
//	uint8_t wday;
	time_t	readBack;

	/* Convert time value to POSIX stuct tm */
	struct tm *gm = gmtime( &time );

	rtc_tkregs.Buff[0] = 0;																							// Clean timekeep structure
	rtc_tkregs.Buff[1] = 0;
	rtc_tkregs.Buff[2] = 0;
	IotLogInfo( "rtc_SetTime" );
	
	if( rtc_StopClock() )																							// Stop RTC clock, before trying to update timekeeping registers
	{
//		IotLogInfo( "  1st Stop Clock" );
		/* Convert POSIX tm values to RTC register values (BCD) */
		convert_tm2tk( gm, &rtc_tkregs );
//		rtc_tkregs.YEAR = bin2bcd( ( gm->tm_year - 2000 ) );														// Convert Year to BCD
//		rtc_tkregs.MONTH = bin2bcd( gm->tm_mon );																	// Convert Month to BCD
//		rtc_tkregs.DATE = bin2bcd(gm->tm_mday);																		// Convert Date to BCD
//		wday = ( gm->tm_wday ? gm->tm_wday : 7 );																	// Convert POSIX Sunday (0) to RTC Sunday (7)
//		rtc_tkregs.WKDAY = bin2bcd( wday );																			// Convert Weekday to BCD
//		rtc_tkregs.HOURS = bin2bcd( gm->tm_hour );																	// Convert Hour to BCD
//		rtc_tkregs.MINUTES = bin2bcd( gm->tm_min );																	// Convert Minute to BCD
//		rtc_tkregs.SECONDS = bin2bcd( gm->tm_sec );																	// Convert Second to BCD,

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
//			IotLogInfo( "  1st Write TK" );
			rtc_tkregs.ST = 1;																						// Enable Oscillator after writing the time registers (otherwise registers may get corrupted)
			if( 0 == rtc_write_byte( RTC_I2C_ADDR, RTC_REG_BLOCK, rtc_tkregs.RTCSEC ) )								// This write Enables Oscillator
			{
//				IotLogInfo( "  1st Start Clock" );

				vTaskDelay( 10 / portTICK_PERIOD_MS );																// Delay between writes (NOTE: due to Microchip issue with RTC Chip on certain days)

				/* Write time-keeping register a second time */
				if( rtc_StopClock() )																				// Stop RTC clock, before trying to update timekeeping registers
				{
//					IotLogInfo( "  2nd Stop Clock" );
					rtc_tkregs.ST = 0;																				// Keep Oscillator Disabled after writing the time registers (otherwise registers may get corrupted)
					if( 0 == rtc_write( RTC_I2C_ADDR, RTC_REG_BLOCK, ( uint8_t * )&rtc_tkregs, RTC_TIMEKEEP_SIZE ) )// Write the 9 Timekeep Registers
					{
//						IotLogInfo( "  2nd Write TK" );
						rtc_tkregs.ST = 1;																			// Re-enable Oscillator after writing the time registers
						if( 0 == rtc_write_byte( RTC_I2C_ADDR, RTC_REG_BLOCK, rtc_tkregs.RTCSEC ) )					// This write Enables Oscillator
						{
//							IotLogInfo( "  2nd Start Clock" );
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
		  
//*********************************************************************
//**							rtcIsConfigured()
//**			DETERMINE WHETHER THE RTC HAS BEEN CONFIGURED YET
//**
//**
//**	By default the chip comes up unconfigured. i.e. clock and
//**	oscillator is not running and battery input is not enabled.
//**	After configuration is completed (in rtcSetTime() ), the
//**	configuration remains SET until/unless the battery is removed
//**	or dies (i.e. maintained only in internal RAM). Once configured,
//**	this routine lets us know that the configuration has occurred.
//**
//**	The MCP7940N does not have any indication if a battery is present
//**	other than the VBATEN and OSCRUN bits will be clear. So cannot
//**	distinguish between a device that has never been configured vs
//**	one where the battery has died, by just looking at the device.
//**	Configuration status could be saved in non-volatile storage (flash)
//**
//**	INPUTS:  ppf - pointer to Power-Fail structure
//**
//**	OUTPUTS:
//**			RTCStatus_Configured		0												// RTC was found to be already configured on system power-up (VBATEN and OSCRUN bits set)
//**			RTCStatus_Initialized	1												// RTC was not configured on system power-up, but was initialized with a default date/time
//**			RTCStatus_ComError		2												// RTC Communication Error
//**
//**				0 = OSCRUN and VBATEN are set, i.e. RTC is configured
//**				1 = OSCRUN and/or VBATEN are not set
//**				2 = Read Error
//*********************************************************************
_rtcStatus_t rtc_IsConfigured(void)
{
	_RTC_TIMEKEEP_t	rtc_tkregs;
	_rtcStatus_t status;
	struct tm gm;																					// Time struct, if we need to set the date/time
	time_t	rtcTime = -1;

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
			IotLogInfo( "rtc is configured: %4u-%02u-%02uT%02u:%02u:%02uZ",
					( 1900 + gm.tm_year ), gm.tm_mon, gm.tm_mday, gm.tm_hour, gm.tm_min, gm.tm_sec );
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

//*********************************************************************
//**							rtc_init()
//**				INITIALIZE REAL TIME CLOCK
//**
//**	Perform hardware initialization of interface to RTC.  This routine
//**	configures the GPIO pins.
//**
//**	INPUTS:  None
//**
//**	OUTPUTS:
//**			RTCStatus_Configured		0												// RTC Properly Configured during AC Power-up:  (VBATEN and OSCRUN bits set)
//**			RTCStatus_Initialized	1												// RTC was not configured on system power-up: Was instead initialized with a default date/time
//**			RTCStatus_ComError		2												// RTC Communication Error: No Configuration occurred
//**
//**				0 = OSCRUN and VBATEN are set, i.e. RTC is configured
//**				1 = OSCRUN and/or VBATEN are not set
//**				2 = Read Error
//**
//*********************************************************************
void	MCP7940N_init(void)
{
//	I2C_InitPort(I2C_RTCC);																	// Initialize I2C port pins
	rtc_IsConfigured();															// determine if RTC is configured
}


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

#ifdef	LATER
//*********************************************************************
//**							rtcMinutesIntoYear()
//**				CALCULATE MINUTES INTO YEAR FOR POWER-TIME
//**
//**	Calculate the number of minutes into the year for the Power-Time
//**	structure.
//**
//**	INPUTS:  ppt - Pointer to PWR_TIME structure
//**
//**	OUTPUTS: The PWR_TIME structure is updated with minutesIntoYear
//**
//*********************************************************************
void rtcMinutesIntoYear(PWR_TIME *ppt)
{
	ppt->daysIntoYear = monthDays[ppt->month] + ppt->date;								// compute days into year, assuming a non-leap year
	if(((ppt->year % 4) == 0) && (ppt->year != 0)) {										// Is this a leap year
		if(ppt->month > 2) {																			// and month is March or later
			++ppt->daysIntoYear;																		// add in the extra day
		}
	}
	ppt->hoursIntoYear = (ppt->daysIntoYear * HOUR_IN_DAY) + ppt->hour;				// compute hours into year
	ppt->minutesIntoYear = (ppt->hoursIntoYear * MINUTES_IN_HOUR) + ppt->minute;	// compute minutes into year
}



//*********************************************************************
//**							rtcSetYear()
//**		SET YEAR VALUE IN PWR_TIME
//**
//**	Calculate the first possible year value for the date information
//**	in the PWR_TIME structure, starting from the given year.
//**	The year value produce the correct weekday for the month-date combination.
//**
//**	INPUTS:  ppt - Pointer to PWR_TIME structure
//**				year - starting year (0 - 99)
//**
//**	OUTPUTS: The PWR_TIME structure is updated with correct year value
//**
//*********************************************************************
void rtcSetYear(PWR_TIME *ppt, int year)
{
	int dow;																						// Day-of-week
	ppt->year = 2000 + year + 1;															// convert year to full value (4-digits), plus one
	
	do	{
		--ppt->year;																			// decrement year, first time thru year is starting value
		dow = DayOfWeek(ppt->year, ppt->month, ppt->date);							// calculate day-of-week, for month-date, in current year
	} while (dow != ppt->weekday);														// continue, decrementing year, until weekday matches dow
}



//*********************************************************************
//**							DayOfWeek()
//**				CALCULATE DAY OF WEEK FOR ANY GIVEN DATE
//**
//**	Calculate the day-of-week for any given date, for years after 1752.
//**	Credit:	Tomohiko Sakamoto on the comp.lang.c Usenet newsgroup in 1992, 
//**				it is accurate for any Gregorian date.
//**
//**	INPUTS:  y - year (> 1752)
//**				m - month (1 - 12)
//**				d - day (1-31)
//**
//**	OUTPUTS: returns Day-of-Week (1 = Monday, 2 = Tuesday .... 6 = Saturday, 7 = Sunday)
//**	OUTPUTS: returns Day-of-Week (1 = Sunday, 2 = Monday .... 7 = Saturday)
//**
//*********************************************************************
int DayOfWeek(int y, int m, int d)
{	
	int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	y -= (m < 3) ? 1 : 0;
	return ((y + (y / 4) - (y / 100) + (y / 400) + t[m - 1] + d - 1) % 7) + 1;
}



//*********************************************************************
//**							DaysInYear()
//**				RETURN NUMBER OF DAYS IN GIVEN YEAR
//**
//**	Return the number of days in the given year, accounting for leap years.
//**
//**	INPUTS:  y - year
//**
//**	OUTPUTS: 365 for standard years, 366 for leap years
//**
//*********************************************************************
int DaysInYear(int year)
{
	return (((year % 4) == 0) && (year != 0)) ? 366 : 365;
}



//*********************************************************************
//**							ValidateTime()
//**
//**	Checks the given TIME structure has a valid Date/Time, including
//		year, month, day, weekday, hour, minute and second
//**
//**	INPUTS:  TIME *tp   pointer to TIME structure
//**
//**	OUTPUTS: returns true if date/time is valid, false if invalid
//**
//*********************************************************************
bool ValidateTime(TIME *tp)
{
	if (!ValidateDate(tp->year, tp->month, tp->date)) {									// check the date
		return false;
	}
	if (tp->weekday != DayOfWeek(tp->year, tp->month, tp->date)) {						// check the day-of-week
		return false;
	}
	if ((tp->hour > 23) || (tp->minute > 59) || (tp->second > 59)) {					// check the time
		return false;
	}
	return true;
}



//*********************************************************************
//**							ValidateDate()
//**				Validate Date (y,m,d)
//**
//**	Checks the given year,month,day for a valid date
//**
//**	INPUTS:  year (0-99), month(1-12), day(1-31)
//**
//**	OUTPUTS: returns true if date is valid, false if invalid
//**
//*********************************************************************
bool ValidateDate(unsigned short year, unsigned short month, unsigned short day)
{
	
	if (!month || month > 12 || !day || year > 99) {
		return false;
	} 
	else if (year%4 == 0 && month == 2 && day == 29) {												// February 29th, Leap year
		return true;
	}
	else if (day > daysInMonth[month]) {
		return false;
	}
	return true;
}







// New Functions 4/3/2019
//*********************************************************************
//**							RTC_SetTimeTask()
//**
//**	Task to set the RTC time asynchronously.  This routine is designed to
//**	allow the RTC time to be set via Bluetooth, and adjust the time values,
//**	associated with cleaning, saved in NVMEM.
//**
//**	Note: If saved time values are invalid (i.e. month == 0) then time value is not adjusted
//**
//**	To handle edge condition where power fails after updating RTC, but before NVMEM is updated,
//**	saved time values are first invalidated (by setting month value to zero).
//**	If power fails before RTC is updated, then saved dates will be invalid, which recovery code already handles
//**
//**	INPUTS:  Global variables
//**				bool	NewRtcTimeAvailable		- set to true when a new RTC time is available
//**				TIME	NewRtcTime					- new RTC time value
//**
//**	OUTPUTS:
//**				rtcSetTimeState updated
//**
//*********************************************************************
void RTC_SetTimeTask(void)
{
	int delta;
	static unsigned char LastCleanMonth;
	static unsigned char SwitchedOffMonth;

	switch (rtcSetTimeState){
		default:
		case RTC_SET_TIME_IDLE:
			if (NewRtcTimeAvailable) {																						// If New RTC Time is available
				NewRtcTimeAvailable = false;																				// clear flag
				LastCleanMonth = NVM.LastCleaning.month;																// Save LastCleaning.month before invalidating
				NVM.LastCleaning.month = 0;																				// Invalidate Last Cleaning Time
				SwitchedOffMonth = NVM.SwitchedOffTime.month;														// Save SwitchedOff.month before invalidating
				NVM.SwitchedOffTime.month = 0;																			// Invalidate Switched Off Time
				UpdateNVMEM = true;																							// Request NVM update
				rtcSetTimeState = RTC_SET_TIME_UPDATE;
			}
			break;
			
		case RTC_SET_TIME_UPDATE:
			if (UpdateNVMEM == false) {																					// Once NVMEM update has completed
				delta =  rtcToLinearT(&NewRtcTime) - rtcToLinearT(&CurrentTime);								// compute time difference, in minutes
#ifdef	DEBUG_BLE
				printf("BLE Set RTC\n");
				printf("Changing RTC by %d minutes\n", delta);

				NVM.LastCleaning.month = LastCleanMonth;																// Restore LastCleaning.month

				TIME AdjustedLastCleaning;
				printf("Last Cleaning: %d/%d/%d %02d:%02d:%02d -> ", NVM.LastCleaning.month, NVM.LastCleaning.date, NVM.LastCleaning.year,
																				NVM.LastCleaning.hour, NVM.LastCleaning.minute, NVM.LastCleaning.second);
				AdjustedLastCleaning = rtcFromLinearT(rtcToLinearT(&NVM.LastCleaning)+delta);
				printf("%d/%d/%d %02d:%02d:%02d\n", AdjustedLastCleaning.month, AdjustedLastCleaning.date, AdjustedLastCleaning.year,
																				AdjustedLastCleaning.hour, AdjustedLastCleaning.minute, AdjustedLastCleaning.second);
#endif							

				if (rtcSetTime(&NewRtcTime)) {																			// set RTC with new value, if successful
					if (RTC_Init_Status) {																					// RTC error: i.e. RTC previous time was invalid before App changed it
						RTC_Init_Status = 0;																					// clear status, RTC value is now valid
						NVM.LastCleaning = NewRtcTime;																	// Set LastCleaning time to the new current time
						DaysSinceLastClng = 0;																				//	Set to 0 since we have reset the Last Cleaning timer		
						BleUpdateLinkedCharacter(LastCleanCycLidx);													// Update linked Bluetooth characteristic
						UpdateNVMEM = true;

						if(State == Standby) {																				// If brewer is in Standby (Power Switch OFF)
							NVM.SwitchedOffTime = NewRtcTime;															// Set Switched Off Time to new current time
							DaysUnpowered = 0;																				//	Set to 0 since we have reset the Switched Off timer
						}
						else{																										// Power Switch ON
							ClearSwOffTime();																					// Clear Switched off time
						}
					}
					
					else {																										// ** Previous RTC was valid before App Changed it **
						if (LastCleanMonth) {																				// Only restore and adjust LastCleaning time if it was previously valid (month != 0)
							NVM.LastCleaning.month = LastCleanMonth;													// Restore LastCleaning.month
							NVM.LastCleaning = rtcFromLinearT(rtcToLinearT(&NVM.LastCleaning) + delta);	// Adjust Last Cleaning Time
							UpdateNVMEM = true;
							BleUpdateLinkedCharacter(LastCleanCycLidx);												// Update linked Bluetooth characteristic
						}
						else {																									// RTC error, i.e. RTC previous time was invalid
							NVM.LastCleaning = NewRtcTime;																// Set LastCleaning time to the new current time
							DaysSinceLastClng = 0;																			//	Set to 0 since we have reset the Last Cleaning timer		
							BleUpdateLinkedCharacter(LastCleanCycLidx);												// Update linked Bluetooth characteristic
							UpdateNVMEM = true;
						}
						
						if (SwitchedOffMonth) {																						// Only restore and adjust SwitchedOff time if it was previously valid  (month != 0)
																																			// NOTE: Will ONLY be valid if Appliance Switched OFF
							NVM.SwitchedOffTime.month = SwitchedOffMonth;													// Restore SwitchedOff.month
							NVM.SwitchedOffTime = rtcFromLinearT(rtcToLinearT(&NVM.SwitchedOffTime) + delta);	// Adjust Switched Off Time
							UpdateNVMEM = true;
						}
						else {																									// IFF SwitchedOffMonth invalid...
							ClearSwOffTime();																					// Clear Switched off time
						}
						ClngTestMinuteTmr = 15;																				// Reset the 15 Minute Timer, so adjusting the seconds is not necessary
					}
#ifdef	DEBUG_BLE
					printf("SetRTC success\n");
#endif
				} 
				
				else {																											// ** ERROR: RTC - rtcSetTime() **
#ifdef	DEBUG_BLE
					printf("SetRTC failed\n");
#endif
					DaysSinceLastClng = 0;																					// 0 since we can't assess
					DaysUnpowered = 0;																						//	0 since we can't assess
				}

				rtcSetTimeState = RTC_SET_TIME_IDLE;																	// Update is complete
			}
			break;
	}
}

#endif


#ifdef	RTC_LINEART_TEST
void linearTimeTest(int year, int month, int date, int hour, int minute);
bool CompareTimes(TIME *t1, TIME *t2);

//*********************************************************************
//**							rtcLinearTest()
//**
//**	Test the LinearTime time conversion functions.
//**	Explicitly test every minute value from 1/1/2018 00:00 to 12/31/2020 23:59
//**	verify that converting to LinearTime and back again results in original TIME value
//**
//**	INPUTS:  None
//**
//**	OUTPUTS:	Conversion output to serial port
//**
//*********************************************************************
void rtcLinearTest(void)
{
	int y, m, d, maxd, h, mm;
	linearTimeTest(0, 1, 1, 0, 0);
	linearTimeTest(0, 1, 1, 0, 1);
	linearTimeTest(0, 1, 1, 1, 0);
	linearTimeTest(0, 2, 1, 0, 0);
	linearTimeTest(18, 1, 1, 0, 0);
	linearTimeTest(18, 1, 1, 0, 1);
	linearTimeTest(18, 8, 1, 23, 59);
	linearTimeTest(18, 12, 31, 23, 59);
	linearTimeTest(19, 1, 1, 0, 0);
	linearTimeTest(19, 1, 1, 0, 1);
	linearTimeTest(19, 12, 31, 23, 59);
	linearTimeTest(20, 1, 1, 0, 0);
	linearTimeTest(20, 1, 1, 0, 1);
	linearTimeTest(20, 12, 31, 23, 59);
	linearTimeTest(21, 12, 31, 23, 59);
	linearTimeTest(22, 12, 31, 23, 59);
	linearTimeTest(23, 12, 31, 23, 59);
	linearTimeTest(24, 12, 31, 23, 59);
	linearTimeTest(99, 12, 31, 23, 59);


	printf("+=+=+=+=+=+=\n");

	
	for (y=18; y<=20; ++y) {
		for(m=1; m<13;++m){
			maxd = daysInMonth[m];
			if ((m == 2) && (y%4 == 0))			// February for leap years
				++maxd;									// has an extra day
			for (d=1; d <= maxd; ++d){
				for (h =0; h <= 23; ++h) {
					mm=0;
//					for (mm = 0; mm <= 59; ++mm) {
						linearTimeTest(y, m, d, h, mm);
//					}
				}
				
			}
		}
	}
}

//*********************************************************************
//**							linearTimeTest()
//**
//**	Test the LinearTime time conversion functions for a given date/time value
//**	Verify that converting to LinearTime and back again results in original TIME value
//**
//**	INPUTS:  None
//**
//**	OUTPUTS:	Conversion output to serial port
//**
//*********************************************************************
void linearTimeTest(int year, int month, int date, int hour, int minute)
{
	TIME t1, t2;
	int lt;
	
	t1.year = year;
	t1.month = month;
	t1.date = date;
	t1.hour = hour;
	t1.minute = minute;
	t1.second = 0;
	
	lt = rtcToLinearT(&t1);				// convert to linear
	printf("%02d/%02d/%02d %02d:%02d:%02d -> %08X", t1.month, t1.date, t1.year, t1.hour, t1.minute, t1.second, lt);
	t2 = rtcFromLinearT(lt);			// convert back
	printf(" -> %02d/%02d/%02d %02d:%02d:%02d ", t2.month, t2.date, t2.year, t2.hour, t2.minute, t2.second);
	if(CompareTimes(&t1, &t2)) {
		printf("OK\n");
	}
	else {
		printf("Error\n");
	}
}

//*********************************************************************
//**							CompareTimes()
//**
//**	Compare two TIME structures
//**
//**	INPUTS:
//*		TIME	t1
//*		TIME	t2
//**
//**	OUTPUTS:
//*		true - if t1 and t2 are identical
//*		false - if t1 and t2 differ
//**
//*********************************************************************
bool CompareTimes(TIME *t1, TIME *t2)
{
	if(t1->year != t2->year) return false;
	if(t1->month != t2->month) return false;
	if(t1->date != t2->date) return false;
	if(t1->hour != t2->hour) return false;
	if(t1->minute != t2->minute) return false;
	if(t1->second != t2->second) return false;
	return true;
	
}
#endif		// RTC_LINEART_TEST

#ifdef	LATER
//*********************************************************************
//**							rtcToLinearT()
//**				Convert TIME structure to a Linear Time value
//**
//**	Linear Time is number of minutes since 1/1/2000 00:00
//**
//**	INPUTS:  pt - Pointer to TIME structure
//**
//**	OUTPUTS:
//**		signed int LinearTime  - number of minutes since 1/1/2000 00:00
//**
//*********************************************************************
int rtcToLinearT(TIME *pt)
{
	int LinearTime;																					// minutes since 1/1/2000 00:00
	
	// compute days from 1/1/2000 to 1/1/year
	LinearTime = pt->year * 365;																	// Days in years
	if (pt->year > 0) {
		LinearTime += ((pt->year - 1)/ 4) + 1;													// Adjust for leap years
	}

	LinearTime += monthDays[pt->month] + pt->date - 1;										// Add days into year, assuming a non-leap year, date is 1-based
	if(((pt->year % 4) == 0) && (pt->year != 0)) {											// Is this a leap year
		if(pt->month > 2) {																			// and month is March or later
			++LinearTime;																				// add in the extra day
//			printf(" >LY ");
		}
	}
	LinearTime = (LinearTime * HOUR_IN_DAY) + pt->hour;									// compute hours since 1/1/2000 00:00
	LinearTime = (LinearTime * MINUTES_IN_HOUR) + pt->minute;							// compute minutes  since 1/1/2000 00:00
	
	return LinearTime;
}


//*********************************************************************
//**							rtcFromLinearT()
//**				Convert Linear Time value to a TIME structure
//**
//**	Linear Time is number of minutes since 1/1/2000 00:00
//**
//**	INPUTS:
//**		signed int LinearTime  - number of minutes since 1/1/2000 00:00
//**
//**	OUTPUTS:
//**		TIME t - TIME structure
//*********************************************************************
TIME rtcFromLinearT(int LinearTime)
{
	TIME time;
	int YearType;																						// 0 = standard, 1 = Leap year
	
	// Calculate Year
	time.year = 0;																							// Starting in year 2000
	while (LinearTime >= MinutesInYear[time.year%4]) {											// If remaining LinearTime is greater than, or equal to, minutes in this year
		LinearTime -= MinutesInYear[time.year%4];													// subtract minutes in this year
		++time.year;																						// and increment year
	}
	YearType = (time.year % 4 == 0) ? 1 : 0;														// set YearType

	// Calculate Month
	time.month = 1;																						// Starting in January
	while (LinearTime >= (daysInMonthExt[YearType][time.month] * MINUTES_IN_DAY)) {	// If remaining LinearTime is greater than, or equal to, minutes in this month
		LinearTime -= (daysInMonthExt[YearType][time.month] * MINUTES_IN_DAY);			// subtract minutes in this month
		++time.month;																						// and increment month
	}
	
	// Calculate Date
	time.date = 1;																							// Starting with the 1st
	while (LinearTime >= MINUTES_IN_DAY) {															// If remaining LinearTime is greater than, or equal to, minutes in day
		LinearTime -= MINUTES_IN_DAY;																	// subtract minutes in day
		++time.date;																						// and increment date
	}
	
	// Calculate Day of Week
	time.weekday = DayOfWeek(time.year, time.month, time.date);

	
	// Calculate Hours
	time.hour = 0;																							// Starting with hour 0
	while (LinearTime >= MINUTES_IN_HOUR) {														// If remaining LinearTime is greater than, or equal to, minutes in hour
		LinearTime -= MINUTES_IN_HOUR;																// subtract minutes in hour
		++time.hour;																						// and increment hour
	}
	
	time.minute = LinearTime;																			// remaining LinearTime is minutes
	time.second = 0;																						// set seconds to zero
	
	return time;
}

#endif
