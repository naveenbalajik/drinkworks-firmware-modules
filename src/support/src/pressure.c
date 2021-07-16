/**
 * @file	pressure.c
 */

#include	<stdint.h>

/**
 * @brief	convertPressure
 *
 * Convert ADC to Pressure value in PSI
 *
 * @param[in]	ADC		Pressure reading in ADC count
 * @return		Pressure in PSI
 */
double convertPressure( uint16_t ADC)
{
	double PeakPressure = 0;

    if (ADC != 0)
    {
        PeakPressure = (((double)(ADC)) * 0.04328896) - 15.95;    // convert from ADC to PSI
    }

    return PeakPressure;
}

