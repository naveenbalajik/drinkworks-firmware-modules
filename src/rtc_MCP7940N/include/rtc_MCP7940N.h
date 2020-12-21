//***************************************************************************
//*
//*										rtc.h
//*
//*	Support for Microchip MCP7940N Real Time Clock
//*
//*	Author: I Whitehead
//*	Created on October 3, 2017
//*
//*	Copyright © 2016-2020 Drinkworks. All rights reserved.
//*
//***************************************************************************
#ifndef _RTC_H_
#define	_RTC_H_

#include <stdbool.h>
#include <time.h>


// RTC Timekeeping Register Mapping
//
//	Timekeeping register, including the Control and OscTim registers, occupy 9 bytes.
// This union will pad out to 12 bytes because of the bit-fields being assigned in 32-bit increments
	
typedef union {
	struct {
		unsigned SECONE : 4;
		unsigned SECTEN : 3;
		unsigned ST     : 1;
		
		unsigned MINONE : 4;
		unsigned MINTEN : 3;
		unsigned : 1;
		
		unsigned HOURONE : 4;
		unsigned HOURTEN : 2;
		unsigned FMT12 : 1;
		unsigned : 1;
		
		unsigned WKDAY : 3;
		unsigned VBATEN : 1;
		unsigned PWRFAIL : 1;
		unsigned OSCRUN : 1;
		unsigned : 2;
		
		unsigned DATEONE : 4;
		unsigned DATETEN : 2;
		unsigned : 2;
		
		unsigned MTHONE : 4;
		unsigned MTHTEN : 1;
		unsigned LPYR : 1;
		unsigned : 2;
		
		unsigned YEARONE : 4;
		unsigned YEARTEN : 4;
		
		unsigned SQWFS : 2;
		unsigned CRSTRIM : 1;
		unsigned EXTOSC : 1;
		unsigned ALM0EN : 1;
		unsigned ALM1EN : 1;
		unsigned SWQEN : 1;
		unsigned OUT : 1;
		
		unsigned TRIMVAL : 7;
		unsigned SIGN : 1;
		
		unsigned : 24;
  };
	struct {
		unsigned SECONDS : 7;
		unsigned : 1;
		
		unsigned MINUTES : 7;
		unsigned : 1;
		
		unsigned HOURS : 6;
		unsigned : 2;
		
		unsigned : 8;
		
		unsigned DATE : 6;
		unsigned : 2;
		
		unsigned MONTH : 5;
		unsigned : 3;
		
		unsigned YEAR : 8;
		
		unsigned CONTROL : 8;
		unsigned OSCTRIM : 8;
		
		unsigned : 24;
  };
  struct {															// byte-wise register definitions
	  unsigned char RTCSEC;
	  unsigned char RTCMIN;
	  unsigned char RTCHOUR;
	  unsigned char RTCWKDAY;
	  unsigned char RTCDATE;
	  unsigned char RTCMTH;
	  unsigned char RTCYEAR;
	  unsigned char RTCCONTROL;
	  unsigned char RTCOSCTRIM;
	  unsigned char Reserved[3];
  };
  unsigned int	Buff[3];											// 3 * 4 bytes, for efficient clearing of structure
} _RTC_TIMEKEEP_t;




#define	RTC_TIMEKEEP_SIZE	9

//
//	RTC Power Register Mapping
//
//	PWRDN and PWRUP register have the same relative mapping
//
typedef union {
	struct {
		unsigned MINONE : 4;
		unsigned MINTEN : 3;
		unsigned : 1;
		
		unsigned HOURONE : 4;
		unsigned HOURTEN : 2;
		unsigned FMT12 : 1;
		unsigned : 1;
		
		unsigned DATEONE : 4;
		unsigned DATETEN : 2;
		unsigned : 2;
		
		unsigned MTHONE : 4;
		unsigned MTHTEN : 1;
		unsigned WKDAY : 3;
	};
	
	struct {
		unsigned MINUTE : 7;
		unsigned : 1;
		unsigned HOUR : 6;
		unsigned : 2;
		unsigned DATE : 6;
		unsigned : 2;
		unsigned MONTH : 5;
		unsigned : 3;
  };
} _RTC_PWR_t;

typedef struct {
	_RTC_PWR_t	PWRDN;
	_RTC_PWR_t	PWRUP;
}_RTC_PWR_REGS_t;

/*
typedef struct
{
	uint8_t		year;																//	0 - 99
	uint8_t		month;															//	1 - 12
	uint8_t		date;																// 1 - 31
	uint8_t		weekday;															// 1 - 7
	uint8_t		hour;																// 00 - 23
	uint8_t		minute;															// 00 - 59
	uint8_t		second;															// 00 - 59
} _rtcTime_t;


typedef struct   __attribute__ ((packed)) {
	unsigned short	year;																//	2000 - 2099
	unsigned char	month;															//	1 - 12
	unsigned char	date;																// 1 - 31
	unsigned char	weekday;															// 1 - 7
	unsigned char	hour;																// 00 - 23
	unsigned char	minute;															// 00 - 59
	unsigned char	second;															// 00 - 59
} TIME_YYYY;


typedef	struct {
	unsigned char	minute;															// 00 - 59
	unsigned char	hour;																// 00 - 23
	unsigned char	date;																// 1 - 31
	unsigned char	month;															//	1 - 12
	unsigned char	weekday;															// 1 - 7
	unsigned int	year;																// 2000 - 2099
	unsigned int	daysIntoYear;													// number of days into the year
	unsigned int	hoursIntoYear;													// number of hours into the year
	unsigned long	minutesIntoYear;												// number of minutes into the year
} PWR_TIME;

typedef struct {
	PWR_TIME			up;																// Power-up time
	PWR_TIME			down;																// Power-down time
	unsigned char	minute;															// Power-fail minutes
	unsigned char	hour;																// Power-fail hours
	unsigned int	day;																// Power-fail days
} PWRFAIL;
*/

// I2C Addresses
//#define	RTC_I2C_ADDR		0xDE
#define	RTC_I2C_ADDR		0x6F													// 7-bit I2C Address for MCP7940N

// Register Block Addresses
#define	RTC_REG_BLOCK		0x00													// RTC, Alarms, Power-Fail 00h - 01Fh
#define	RTC_SEC_REG			0x00
#define	RTC_WKDAY_REG		0x03
#define	RTC_DATE_REG		0x04
#define	RTC_YEAR_REG		0x06
#define	RTC_PWR_REG			0x18													// RTC Power registers

#define	RTC_VBATEN_BIT		0x08
#define	RTC_PWRFAIL_BIT		0x10
#define	RTC_OSCRUN_BIT		0x20

#define	SRAM_BLOCK			0x20													// SRAM Block 20h - 5Fh

#define	RTC_STOP_RETRY		8														// Maximum number of time to read WKDAY Register, to determine if clock is stopped

// Common constants
#define	HOUR_IN_DAY			24
#define	MINUTES_IN_HOUR	60
#define	MINUTES_IN_DAY		(HOUR_IN_DAY * MINUTES_IN_HOUR)
#define	DAYS_IN_YEAR		365													// non-leap year
#define	MINUTES_IN_YEAR	(DAYS_IN_YEAR * MINUTES_IN_DAY)

typedef	enum
{
	eRtcStatus_Configured,												// RTC was configured on system power-up (VBATEN and OSCRUN bits set)
	eRtcStatus_Initialized,												// RTC was not configured on system power-up, but was initialized with a default date/time
	eRtcStatus_ComError,												// RTC Communication Error
} _rtcStatus_t;

// Define Days-of-Week, per Bluetooth standard
enum DayOfWeek {
	Monday = 1,
	Tuesday,
	Wednesday,
	Thursday,
	Friday,
	Saturday,
	Sunday
};


// Global Variables
// extern unsigned char	rtcConfigStatus;											// RTC status, see defined states above

//	Function prototypes
//_rtcStatus_t	rtc_init_MCP7940N( void );
//_rtcStatus_t 	rtc_IsConfigured( void );
//bool			rtc_SetTime( struct tm *gm );
//bool			rtc_GetTime( struct tm *gm );
const rtc_hal_t	* MCP7940N_getHAL( void );

/*
bool	rtcSetTime(TIME *tp);
void	rtcUpdateTime(void);															// Accesses/Stores current time every second (called in Kernel.c)
bool	rtcGetTime(TIME *tp);
bool	rtcPwrFail(PWRFAIL *ppf, TIME *pdt);									// Power fail structure AND TIME structure for ONLY the calculated PowerDown Time/Date
bool	rtcPwrOutage(PWRFAIL *ppf, unsigned char year);
*/

#ifdef	RTC_PWROUT_TEST
void	rtc_testPowerOutage(unsigned char dnMonth, unsigned char dnDate, unsigned char dnHour, unsigned char dnMinute, unsigned char dnDay,
#endif

#ifdef	LATER
unsigned short rtcCalcDeltaTime(TIME *OlderTime, TIME *NewerTime);	// `MP65 was implicit
void	rtcMinutesIntoYear(PWR_TIME *ppt);
bool	rtcStopClock(void);
void	rtcSetYear(PWR_TIME *ppt, int year);
int	DayOfWeek(int y, int m, int d);
int	DaysInYear(int year);
bool	ValidateDate(unsigned short year, unsigned short month, unsigned short day);
bool	ValidateTime(TIME *tp);
unsigned char bin2bcd(unsigned char bin);
unsigned char bcd2bin(unsigned char bcd);

void RTC_SetTimeTask(void);


// ***** External Globals ******
extern TIME	CurrentTime;															// Current Date/Time - Used for determining when "Cleaning Required" messages put up
extern TIME	PwrDownTime;															// Records the Time when the AC power went down (used for comparison for 7-day cleaning message)
extern PWRFAIL PwrDownCalc;														// Used to review Power Up/Down registers and calculate a down time

extern unsigned char	RTCValid;													// used in rtcUpdateTime() to indicate if real time clock read was valid

extern bool	NewRtcTimeAvailable;													// Flag set to true when a new RTC time value is available
extern TIME	NewRtcTime;																// New RTC Time value

extern bool rtcPwrFailStatus;														// rtcPwrFail() status (called at System Initialization)

#endif

#endif	/* _RTC_H_ */

