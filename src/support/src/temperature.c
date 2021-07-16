/**
 * @file	temperature.c
 */

#include	<stdint.h>

/**
 * @brief	convertTemperature
 *
 * Convert ADC Temperature value to Celcius
 * Equations for Calculating Temperature in °C from A/D value(ADC = x):
 * 		<= 17°C:	°C = 0.000002383038x^2 + 0.012865539727x - 33.744428921424
 * 		>  17°C:	°C = 0.00000000000467700355x^4 - 0.00000004668271047472x^3 + 0.000180445916138172x^2 - 0.294237511718683x + 167.820530124722
 *
 * @param[in]	adc		Temperature value in ADC count
 * @return		Temperature in degrees Celcius
 */
double convertTemperature( uint16_t adc)
{
	const double a1 = -33.744428921424;
	const double b1 = 0.012865539727;
	const double c1 = 0.000002383038;

	const double a2 = 167.820530124722;
	const double b2 = -0.294237511718683;
	const double c2 = 0.000180445916138172;
	const double d2 = -0.0000000466827104747;
	const double e2 = 0.00000000000467700355;

	double celcius;

	if (adc < 1059) celcius = -20.0;                                    // Min permitted
	else if (adc > 3693) celcius = 63.0;                                // Max permitted
	else if (adc <= 2642)                                               // <= 17°C
	{
		celcius = (c1 * adc * adc) + (b1 * adc) + a1;
	}
	else                                                                //  >  17°C
	{
		celcius = (e2 * adc * adc * adc * adc) + (d2 * adc * adc * adc) + (c2 * adc * adc) + (b2 * adc) + a2;
	}
	return celcius;
}
