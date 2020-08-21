/*
 * image_processing.c
 *
 *  Created on: Jun 4, 2020
 *      Author: Nicholas.Weber
 */

#include <stdio.h>
#include <stdlib.h>
#include "image_processing.h"
#include "sensor.h"
#include "esp_timer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <math.h>

/* Debug Logging */
#include "image_processing_logging.h"

#define TRANSITION_NOT_FOUND	-1
#define	TRANSITION_FOUND		1

#define BITS_PER_PIXEL						8
#define MEDIAN_FILTER_SIZE					7
#define ROW_CONSIS_CHECK_JUMP				40
#define ROW_CC_OFFSET_START					20
#define ROW_BCODE_CC_OFFSET					10
#define CC_LARGEST_SQ_DIFF					500
#define COL_DROP_THRES						28
#define COL_DOP_SCAN_WINDOW					75
#define COL_SIZE_JUMP						235
#define TMARK_AREA_WIDTH					100
#define TMARK_AREA_HEIGHT					100
#define TMARK_THRES_RELATIVITY_CRITERIA		60
#define TMARK_ROW_AVG_THRES					245
#define TMARK_COL_AVG_THRES					240
#define	DRINKWORKS_TEMPLATE_TMARK_HEIGHT	61
#define	DRINKWORKS_TEMPLATE_TMARK_WIDTH		61
#define	BOOL_WHITE							1
#define	BOOL_BLACK							0
#define WHITE								255
#define BLACK								0
#define BCODE_SCAN_OFFSET					30
#define SINGLE_BIT_OFFSET_1					5
#define SINGLE_BIT_OFFSET_2					30
#define MAX_TRANS_LOCATIONS	 				40
#define BARCODE_BITS						12
#define MAX_BCODE_ID						1024
#define MIN_POS_HT							20
#define MIN_BAR_DISTANCE					10
#define DRINKWORKS_TRUSTMARK_THRESHOLD		900

#define ONE_BAR_THRES				0.125
#define TWO_BAR_THRES				0.208
#define THREE_BAR_THRES				0.292
#define FOUR_BAR_THRES				0.375
#define FIVE_BAR_THRES				0.458
#define SIX_BAR_THRES				0.542
#define SEVEN_BAR_THRES				0.625
#define EIGHT_BAR_THRES				0.708
#define NINE_BAR_THRES				0.792
#define TEN_BAR_THRES				0.875
#define ELEVEN_BAR_THRES			0.958

typedef enum{
	RISING_TRANSITION = 0,
	FALLING_TRANSITION
}Img_Transition_Type;

#define WHITESPACE_THRES			150
#define	REQUIRED_TRANSITION_DIFF	10


static const bool						masterDrinkworksTrademark[DRINKWORKS_TEMPLATE_TMARK_HEIGHT][DRINKWORKS_TEMPLATE_TMARK_WIDTH] =
{
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1},
	{1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1},
	{1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
	{1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
	{1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
	{1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
	{1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
	{1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
	{1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1},
	{1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1},
	{1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1},
	{1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1},
	{1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1},
	{1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1},
	{1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1},
	{1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
};

Image_Proces_Frame_t img = VGA_IMG_DEFAULT_INITIALIZATION;

static img_proces_err_t _calcImgRegionAvg(Image_Region_Avg_t* imgRegionAvg, camera_fb_t* fb){
	img_proces_err_t err = IMG_PROCES_OK;

	// Allocate memory for the buffer if it has not yet been allocated
	if(imgRegionAvg->avgBuf == NULL){
		imgRegionAvg->avgBuf = (uint32_t*)calloc(imgRegionAvg->len, sizeof(uint32_t));
		if(imgRegionAvg->avgBuf == NULL){
			err = IMG_PROCES_FAIL;
			IotLogError("Error: Failed to allocate memory for image region average");
			return err;
		}
	}

	int32_t y, x;
	int32_t startRow = imgRegionAvg->imgRegion.startPoint.y;
	int32_t endRow = imgRegionAvg->imgRegion.endPoint.y;
	int32_t startCol = imgRegionAvg->imgRegion.startPoint.x;
	int32_t endCol = imgRegionAvg->imgRegion.endPoint.x;

	// Loop through image to average requested region
	for(y = startRow; y< endRow; y++){
		for(x = startCol; x<endCol; x++){
			// Do some boundary checking to ensure that the requested region is not outside of the limits of the picture
			if(y >=0 && y < fb->height && x>=0 && x<fb->width){
				// Fill the average array
				if(imgRegionAvg->scanDirection == ROW_SCAN){
					imgRegionAvg->avgBuf[y-startRow] += fb->buf[y*(fb->width) + x];
				}
				else{
					imgRegionAvg->avgBuf[x-startCol] += fb->buf[y*(fb->width) + x];
				}
			}
			else{
				IotLogError("Error: Requested region of image to calculate is outside of image");
				err = IMG_PROCES_FAIL;
			}
		}
	}

	return err;
}

//*********************************************************************************************************************
//* medianFind																																						//`PP70 renamed function
//*
//* Description: This function sorts an array with size arrSize and returns the median of the dataset
//*		unsigned int unsortedArr[]: Array to be sorted
//*		unsigned int arrSize: Array Size
//*
//* Parameters:
//*		None
//*
//* Returns:
//*		Middle position of the sorted array
//*********************************************************************************************************************
static uint32_t _medianFindUINT32(uint32_t *unsortedArr, uint32_t size){
	// First perform selection sort in order to sort array
	int32_t i = 0;
	int32_t j = 0;
	for( i = 0; i < size - 1; i++)
	{
		int32_t min = i;
		for (j = i+1; j < size - 1; j++){
			if (unsortedArr[j] < unsortedArr[min]){
				min = j;
			}
		}
		int32_t temp = unsortedArr[i];
		unsortedArr[i] = unsortedArr[min];
		unsortedArr[min] = temp;
	}

	// return median of sorted array
	return unsortedArr[size/2];
}


// Performs median filter on input array. Will replace input filter
static img_proces_err_t _medianFilterUINT32(uint32_t* buf, uint32_t bufSize, uint32_t medianSize){
	img_proces_err_t err = IMG_PROCES_OK;
	int32_t i;

	// Create a copy of the input array to run median searches on
	uint32_t* bufCopy = NULL;
	bufCopy = (uint32_t*)malloc(bufSize*sizeof(uint32_t));
	if(bufCopy == NULL){
		err = IMG_PROCES_FAIL;
		IotLogError("Error (Median Filter): Failed to allocate memory for median filter copy");
	}
	if(err == IMG_PROCES_OK){
		memcpy(bufCopy, buf, bufSize*sizeof(uint32_t));
	}

	// Find median and set array
	uint32_t* medianFilterArray = (uint32_t*)malloc(medianSize * sizeof(uint32_t));
	if(err == IMG_PROCES_OK){
		for(i = medianSize/2; i<bufSize - medianSize/2; i++){
			memcpy(medianFilterArray, &bufCopy[i - medianSize/2], medianSize*sizeof(uint32_t));
			buf[i] = _medianFindUINT32(medianFilterArray, medianSize);
		}
	}

	if (medianFilterArray != NULL) {
		free(medianFilterArray);
	}

	if(bufCopy != NULL){
		free(bufCopy);
	}

	return err;
}


static int8_t	_checkForRowAvgTransition(Image_Region_Avg_t* rowAvg, uint32_t testRow, Img_Transition_Type transitionType){
	if(transitionType == RISING_TRANSITION){
		if(rowAvg->avgBuf[testRow + (rowAvg->len)/48] > WHITESPACE_THRES && rowAvg->avgBuf[testRow + (rowAvg->len)/48 + 1] > WHITESPACE_THRES && rowAvg->avgBuf[testRow + (rowAvg->len)/48 + 2] > WHITESPACE_THRES && \
				(int32_t)rowAvg->avgBuf[testRow + (rowAvg->len)/48] - (int32_t)rowAvg->avgBuf[testRow] > REQUIRED_TRANSITION_DIFF && (int32_t)rowAvg->avgBuf[testRow + (rowAvg->len)/48 + 1] - (int32_t)rowAvg->avgBuf[testRow + 1] > REQUIRED_TRANSITION_DIFF && (int32_t)rowAvg->avgBuf[testRow + (rowAvg->len)/48 + 2] - (int32_t)rowAvg->avgBuf[testRow + 2]  > REQUIRED_TRANSITION_DIFF){

			return TRANSITION_FOUND;
		}

	}
	else if(transitionType == FALLING_TRANSITION){
		if(rowAvg->avgBuf[testRow] > WHITESPACE_THRES && rowAvg->avgBuf[testRow+1] > WHITESPACE_THRES && rowAvg->avgBuf[testRow+2] > WHITESPACE_THRES && \
				(int32_t)rowAvg->avgBuf[testRow] - (int32_t)rowAvg->avgBuf[testRow + (rowAvg->len)/48] > REQUIRED_TRANSITION_DIFF && (int32_t)rowAvg->avgBuf[testRow + 1] - (int32_t)rowAvg->avgBuf[testRow + (rowAvg->len)/48 + 1] > REQUIRED_TRANSITION_DIFF && (int32_t)rowAvg->avgBuf[testRow + 2] - (int32_t)rowAvg->avgBuf[testRow + (rowAvg->len)/48 + 2] > REQUIRED_TRANSITION_DIFF){

			return TRANSITION_FOUND;
		}
	}

	return TRANSITION_NOT_FOUND;
}


//******************************************************************************************************************
//* consistencyCheck
//*
//* Description: Function scans an area and determines that the range of values is consistent. Often used to ensure
//*						that there is whitespace to the left/right and above/below the barcode
//*
//* Inputs: int medArray[]: array of data to be analyzed
//*			int StartLoc: Start location of consistency check
//*			int endLoc: End location of consistency check
//*
//* Outputs: unsigned char: returns 1 if array is consistent, returns 0 if there are large deviations
//*
//* Remarks:
//*		This function will catch instances when the beginning of the white space window is considered the start of the barcode
//*******************************************************************************************************************
uint8_t _consistencyCheck(uint32_t* buf, int32_t startLoc, int32_t endLoc) {
	uint32_t largestSquaredDiff = 0;
	int32_t i, squaredDiff;
	// Loop through the array
	for (i = startLoc; i < endLoc - 3; i++) {
		// At every location, check the difference between the current value and 3 pixels away
		squaredDiff = ((int32_t)buf[i] - (int32_t)buf[i + 3]) * ((int32_t)buf[i] - (int32_t)buf[i + 3]);
		// Record the largest drop during the scan
		if (squaredDiff > largestSquaredDiff) {
			largestSquaredDiff = squaredDiff;
		}
	}
	// If the array is consistency, there will be no large drops. This indicates white space and a passing score
	if (largestSquaredDiff < CC_LARGEST_SQ_DIFF) {
		return 1;
	}
	// If there are large drops, there can be objects other than white space, and the current location should be ignored
	else {
		return 0;
	}
}

static img_proces_err_t _determineStartStopRow(Image_Proces_Frame_t* img, uint32_t* currentScanRow){

	Image_Region_t tempBarcode1Region = {0};
	Image_Region_t tempBarcode2Region = {0};

	uint32_t startRow, testRow;
	// Loop through the row averages data
	for(startRow=*currentScanRow; startRow < img->rowAvg.len; startRow++){
		// First Check for a falling transition
		if(_checkForRowAvgTransition(&(img->rowAvg), startRow, FALLING_TRANSITION) == TRANSITION_FOUND){
			tempBarcode1Region.startPoint.y = startRow + (img->rowAvg.len)/48;
			IotLogDebug("Barcode1 Start Row Found at row: %d", startRow + (img->rowAvg.len)/48);
		}
		// If no falling transition found at the current row, test the next row
		else{
			continue;
		}

		// If a falling transition is found, jump forward and check for a rising transition
		for(testRow=tempBarcode1Region.startPoint.y + 25; testRow < tempBarcode1Region.startPoint.y + 75 && testRow + 22 < img->fb.height; testRow++){
			if(_checkForRowAvgTransition(&(img->rowAvg), testRow, RISING_TRANSITION) == TRANSITION_FOUND){
				tempBarcode1Region.endPoint.y = testRow + (img->rowAvg.len)/48;
				IotLogDebug("Barcode1 End Row Found at row: %d", testRow + (img->rowAvg.len)/48);
				break;
			}
		}
		// If a rising transition is not found, check the next row
		if(tempBarcode1Region.endPoint.y == 0){
			continue;
		}

		// Limit checking
		uint32_t scanStart = 0;
		if(tempBarcode1Region.endPoint.y + 125 >= img->rowAvg.len){
			scanStart = img->rowAvg.len - 1;
		}
		else{
			scanStart = tempBarcode1Region.endPoint.y + 120;
		}

		for(testRow = scanStart; testRow > scanStart - 55; testRow--){
			if(_checkForRowAvgTransition(&(img->rowAvg), testRow, FALLING_TRANSITION) == TRANSITION_FOUND){
				tempBarcode2Region.startPoint.y = testRow;
				IotLogDebug("Barcode2 Start Row Found at row: %d", testRow);
				break;
			}
		}
		// If a rising transition is not found, check the next row
		if(tempBarcode2Region.startPoint.y == 0){
			continue;
		}

		for(testRow = tempBarcode2Region.startPoint.y + 30; testRow < tempBarcode2Region.startPoint.y + 80 && testRow < img->rowAvg.len - 12; testRow++){
			if(_checkForRowAvgTransition(&(img->rowAvg), testRow, RISING_TRANSITION) == TRANSITION_FOUND){
				tempBarcode2Region.endPoint.y = testRow + (img->rowAvg.len)/48;
				IotLogDebug("Barcode2 End Row Found at row: %d", testRow + (img->rowAvg.len)/48);
				break;
			}
		}
		// If a rising transition is not found, check the next row
		if(tempBarcode2Region.endPoint.y == 0){
			continue;
		}

		// Do final check for Column Consistency
		int32_t startRowConsistCheck = MEDIAN_FILTER_SIZE/2;
		int32_t endRowConsistCheck = img->rowAvg.len - MEDIAN_FILTER_SIZE/2;
		if(tempBarcode1Region.startPoint.y - ROW_CONSIS_CHECK_JUMP > MEDIAN_FILTER_SIZE/2){
			startRowConsistCheck = tempBarcode1Region.startPoint.y - ROW_CONSIS_CHECK_JUMP;
		}
		if(tempBarcode2Region.endPoint.y + ROW_CONSIS_CHECK_JUMP < img->rowAvg.len - MEDIAN_FILTER_SIZE/2){
			endRowConsistCheck = tempBarcode2Region.endPoint.y + ROW_CONSIS_CHECK_JUMP;
		}
		if(_consistencyCheck(img->rowAvg.avgBuf, startRowConsistCheck, tempBarcode1Region.startPoint.y - ROW_CC_OFFSET_START) && _consistencyCheck(img->rowAvg.avgBuf, tempBarcode2Region.endPoint.y + ROW_CC_OFFSET_START, endRowConsistCheck) && \
				_consistencyCheck(img->rowAvg.avgBuf, tempBarcode1Region.startPoint.y + ROW_BCODE_CC_OFFSET, tempBarcode1Region.endPoint.y - ROW_BCODE_CC_OFFSET) && _consistencyCheck(img->rowAvg.avgBuf, tempBarcode2Region.startPoint.y + ROW_BCODE_CC_OFFSET, tempBarcode2Region.endPoint.y - ROW_BCODE_CC_OFFSET)){
			// At this point, we have confirmed the signature of the DW image, and the start/stop rows can be stored in the img
			memcpy(&(img->barcode1.regionAvg.imgRegion), &tempBarcode1Region, sizeof(Image_Region_t));
			memcpy(&(img->barcode2.regionAvg.imgRegion), &tempBarcode2Region, sizeof(Image_Region_t));
			*currentScanRow = startRow;
			IotLogDebug("Start/Stop Rows Found");
			return IMG_PROCES_OK;
		}
	}

	IotLogError("Error: Could not find stop/start rows in image");
	return IMG_PROCES_FAIL;
}

void _scaleBufferUINT32(uint32_t* buf, uint32_t len, uint32_t maxVal){
	uint32_t i, maxFound = 0;
	for(i=0; i<len; i++){
		if(buf[i] > maxFound){
			maxFound = buf[i];
		}
	}

	for(i=0; i<len; i++){
		buf[i] = (buf[i] * maxVal) / maxFound;
	}

}


static int8_t _checkForColAvgTransition(Image_Proces_Frame_t* img, uint32_t testCol){
	uint32_t	x, i, endScanCol;
	int32_t* colAvgBuf = (int32_t*)img->colAvg.avgBuf;

	x = testCol;
	if(colAvgBuf[x] - colAvgBuf[x + 10] > COL_DROP_THRES && colAvgBuf[x + 1] - colAvgBuf[x + 11] > COL_DROP_THRES && colAvgBuf[x + 2] - colAvgBuf[x + 12] > COL_DROP_THRES && colAvgBuf[x] > 50 && colAvgBuf[x + 1] > 50 && colAvgBuf[x + 2] > 50){
		if (x + COL_SIZE_JUMP + COL_DOP_SCAN_WINDOW >= img->fb.width) {
			endScanCol = img->fb.width - 1;
		}
		else{
			endScanCol = x + COL_SIZE_JUMP + COL_DOP_SCAN_WINDOW;
		}
		for (i = endScanCol; i >= endScanCol - COL_DOP_SCAN_WINDOW; i--) {				// Scan left from the endScanCol location to determine white to black transition location which indicates end of barcode
			if (colAvgBuf[i] - colAvgBuf[i - 10] > COL_DROP_THRES && colAvgBuf[i - 1] - colAvgBuf[i - 11] > COL_DROP_THRES && colAvgBuf[i - 2] - colAvgBuf[i - 12] > COL_DROP_THRES && colAvgBuf[i] > 50 && colAvgBuf[i + 1] > 50 && colAvgBuf[i + 2] > 50) {			// Three column filter to determine rise. A rise indicates the end of the barcode
				// Perform final check to ensure that there is white space to the left and right of the assumed barcode location
				int16_t startScan = x - 60;																// Scan left 60 pixels to confirm there is only whitespace to the left of the barcode start col
				int16_t endScan = i + 60;																	// Scan right 60 pixels to confirm there is only whitespace to the right of the barcode start col
				if (startScan < 0) {																		// If the scan start is outside of the array
					startScan = 0;																			// Set to 0
				}
				if (endScan > img->fb.width) {																	// If end scan is outside of array
					endScan = img->fb.width;																		// Set to max array
				}
				if (_consistencyCheck(colAvgBuf, startScan, x) && _consistencyCheck(colAvgBuf, i, endScan)) {			// Use consistencyCheck function to ensure there is whitespace to the right and left of the barcode.
					img->barcode1.regionAvg.imgRegion.startPoint.x = x;																// If whitespace to the right and left of the barcode, store barcode startCol location
					img->barcode2.regionAvg.imgRegion.startPoint.x = x;
					img->barcode1.regionAvg.imgRegion.endPoint.x = i - 10;															// If whitespace to the right and left of the barcode, store barcode endCol location
					img->barcode2.regionAvg.imgRegion.endPoint.x = i - 10;
					IotLogDebug("Start/Stop Col Found: %d, %d", x, i- 10);
					return 1;
				}
			}
		}
	}

	return 0;
}


static img_proces_err_t _determineStartStopCol(Image_Proces_Frame_t* img){
	img_proces_err_t err = IMG_PROCES_OK;

	img->colAvg.imgRegion.startPoint.x = 0;
	img->colAvg.imgRegion.startPoint.y = img->barcode1.regionAvg.imgRegion.startPoint.y;

	img->colAvg.imgRegion.endPoint.x = img->fb.width;
	img->colAvg.imgRegion.endPoint.y = img->barcode2.regionAvg.imgRegion.endPoint.y;

	img->colAvg.len = img->colAvg.imgRegion.endPoint.x - img->colAvg.imgRegion.startPoint.x;

	err = _calcImgRegionAvg(&(img->colAvg), &(img->fb));

	if(err == IMG_PROCES_OK){
		err = _medianFilterUINT32(img->colAvg.avgBuf, img->colAvg.len, MEDIAN_FILTER_SIZE);
	}

	if(err == IMG_PROCES_OK){
		_scaleBufferUINT32(img->colAvg.avgBuf, img->colAvg.len, 255);
	}

	int i;
	for(i = img->colAvg.len / 2; i >=0; i--){
		if(_checkForColAvgTransition(img, i)){
			return err;
		}
	}
	for(i = img->colAvg.len / 2; i < img->colAvg.len - 12; i++){
		if(_checkForColAvgTransition(img, i)){
			return err;
		}
	}

	return IMG_PROCES_FAIL;
}

static void	_gaussianAverage(Trustmark_t* trustmark) {
	uint32_t	x,y;

	// Box filter on each pixel (3x3 box)
	for (y = 1; y < trustmark->fb.height - 1; y++) {															// Loop through rows
		for (x = 1; x < trustmark->fb.width - 1; x++) {											// Loop through columns
			trustmark->fb.buf[y*trustmark->fb.width + x] = (trustmark->fb.buf[(y - 1)*trustmark->fb.width + x - 1] + trustmark->fb.buf[(y)*trustmark->fb.width + x - 1] + trustmark->fb.buf[(y + 1)*trustmark->fb.width + x - 1] + trustmark->fb.buf[(y - 1)*trustmark->fb.width + x] + trustmark->fb.buf[(y)*trustmark->fb.width + x] + trustmark->fb.buf[(y + 1)*trustmark->fb.width + x] + trustmark->fb.buf[(y - 1)*trustmark->fb.width + x + 1] + trustmark->fb.buf[(y)*trustmark->fb.width + x + 1] + trustmark->fb.buf[(y + 1)*trustmark->fb.width + x + 1]) / 9;			// Set the current pixel value to the average value of the 3x3 box
		}
	}

	// **** Due to the nature of the 3x3 box filter, edges of the trademark cannot be included in the loop. Below code fills the edges with their nearest filtered neighbor. Edges include first/last rows and columns
	// Replace first/last rows/columns with second rows/columns
	for (x = 1; x < trustmark->fb.width - 1; x++) {
		trustmark->fb.buf[x] = trustmark->fb.buf[trustmark->fb.width + x];
		trustmark->fb.buf[(trustmark->fb.height - 1)*trustmark->fb.width + x] = trustmark->fb.buf[(trustmark->fb.height - 2)*trustmark->fb.width + x];
	}
	for (y = 1; y < trustmark->fb.height - 1; y++) {
		trustmark->fb.buf[(y)*trustmark->fb.width] = trustmark->fb.buf[(y)*trustmark->fb.width + 1];
		trustmark->fb.buf[(y)*trustmark->fb.width + trustmark->fb.width - 1] = trustmark->fb.buf[(y)*trustmark->fb.width + trustmark->fb.width - 2];
	}

	//Replace corners
	trustmark->fb.buf[0] = trustmark->fb.buf[trustmark->fb.width + 1];
	trustmark->fb.buf[(trustmark->fb.height - 1)*trustmark->fb.width] = trustmark->fb.buf[(trustmark->fb.height - 2)*trustmark->fb.width + 1];
	trustmark->fb.buf[trustmark->fb.width - 1] = trustmark->fb.buf[trustmark->fb.width + trustmark->fb.width - 2];
	trustmark->fb.buf[(trustmark->fb.height - 1)*trustmark->fb.width + trustmark->fb.width - 1] = trustmark->fb.buf[(trustmark->fb.height - 2)*trustmark->fb.width + trustmark->fb.width - 2];
}


static void _defineBWThreshold(Image_Proces_Frame_t* img){
	uint32_t leftThreshold = 0;
	uint32_t rightThreshold = 0;
	uint32_t finalThreshold = 0;
	uint32_t x, y, leftThresCount = 0, rightThresCount = 0;

	for(y = (img->barcode2.regionAvg.imgRegion.startPoint.y + img->barcode1.regionAvg.imgRegion.endPoint.y)/2 - 5; y < (img->barcode2.regionAvg.imgRegion.startPoint.y + img->barcode1.regionAvg.imgRegion.endPoint.y)/2 + 5 && y<img->fb.height; y++){
		for(x = img->trustmark.startCol - 10; x < img->trustmark.startCol; x++){
			leftThreshold += img->fb.buf[(y*img->fb.width)+(x)];
			leftThresCount++;
		}
		for(x = img->trustmark.startCol + img->trustmark.fb.width; x<=img->fb.width && x < img->trustmark.startCol + img->trustmark.fb.width + 10; x++){
			rightThreshold += img->fb.buf[(y*img->fb.width)+(x)];
			rightThresCount++;
		}
	}

	if(leftThresCount && rightThresCount){
		leftThreshold /= leftThresCount;
		rightThreshold /= rightThresCount;

		if (abs(leftThreshold - rightThreshold) < TMARK_THRES_RELATIVITY_CRITERIA) {	// If the left and right threshold have similar absolute values
			finalThreshold = (leftThreshold + rightThreshold) / 2;			// Take the average of their values and set that as the trademark B/W threshold
		}
		else if (leftThreshold <= rightThreshold) {												// Otherwise if the left is substantially lower than the right
			finalThreshold = leftThreshold;																	// Use the left average as the absolute B/W criteria
		}
		else {																																					// if the right threshold is substantially lower than the left
			finalThreshold = rightThreshold;																	// Use the left average as the absolute B/W criteria
		}
	}

	finalThreshold *= 0.6;

	img->trustmark.bwThres = finalThreshold;
}

static void _bwThreshold(Trustmark_t* trustmark){
	uint32_t i;

	for(i=0; i<trustmark->fb.len; i++){
		if(trustmark->fb.buf[i] < trustmark->bwThres){
			trustmark->fb.buf[i] = 0;
		}
		else{
			trustmark->fb.buf[i] = 255;
		}
	}
}

static img_proces_err_t _findTrustmark(Image_Proces_Frame_t* img){
	img_proces_err_t err = IMG_PROCES_OK;
	Image_Region_Avg_t tmarkRegionAvg;
	memset(&tmarkRegionAvg, 0, sizeof(Image_Region_Avg_t));

	tmarkRegionAvg.imgRegion.endPoint.x = img->trustmark.fb.width;
	tmarkRegionAvg.imgRegion.endPoint.y = img->trustmark.fb.height;
	tmarkRegionAvg.scanDirection = ROW_SCAN;
	tmarkRegionAvg.len = img->trustmark.fb.height;

	err = _calcImgRegionAvg(&tmarkRegionAvg, &(img->trustmark.fb));

	if(err != IMG_PROCES_OK){
		return err;
	}

	_scaleBufferUINT32(tmarkRegionAvg.avgBuf, tmarkRegionAvg.len, 255);

	uint32_t y, x;
	// Go backwards in row averages array to find start row of trademark
	for(y=img->trustmark.fb.height / 2; y>0; y--){
		if(tmarkRegionAvg.avgBuf[y] > TMARK_ROW_AVG_THRES && ((int32_t)tmarkRegionAvg.avgBuf[y] - (int32_t)tmarkRegionAvg.avgBuf[y + 5]) > 75){
			img->trustmark.isolatedTrustmark.startPoint.y = y;
			break;
		}
	}

	// Jump forward in row averages array and find end row of trademark
	for(y=img->trustmark.fb.height / 2; y<img->trustmark.fb.height; y++){
		if(tmarkRegionAvg.avgBuf[y] > TMARK_ROW_AVG_THRES && ((int32_t)tmarkRegionAvg.avgBuf[y] - (int32_t)tmarkRegionAvg.avgBuf[y - 5]) > 75){
			img->trustmark.isolatedTrustmark.endPoint.y = y;
			break;
		}
	}

	if (!img->trustmark.isolatedTrustmark.endPoint.y || (img->trustmark.isolatedTrustmark.endPoint.y - img->trustmark.isolatedTrustmark.startPoint.y == 0)) {														// If the trademark start or end row was not found
		img->trustmark.isolatedTrustmark.endPoint.y = img->trustmark.fb.height;																								// Save the end row as the last row of the trademark
	}

	// Reset the region average
	if(NULL != tmarkRegionAvg.avgBuf){
		free(tmarkRegionAvg.avgBuf);
		tmarkRegionAvg.avgBuf = NULL;
	}

	// Calculate the column average for the region
	tmarkRegionAvg.imgRegion.startPoint.y = img->trustmark.isolatedTrustmark.startPoint.y;
	tmarkRegionAvg.imgRegion.endPoint.y = img->trustmark.isolatedTrustmark.endPoint.y;
	tmarkRegionAvg.scanDirection = COL_SCAN;
	tmarkRegionAvg.len = img->trustmark.fb.width;

	err = _calcImgRegionAvg(&tmarkRegionAvg, &(img->trustmark.fb));

	if(err != IMG_PROCES_OK){
		return err;
	}

	_scaleBufferUINT32(tmarkRegionAvg.avgBuf, tmarkRegionAvg.len, 255);

	// Go backwards to find start col of trademark
	for(x = img->trustmark.fb.width/2; x>0; x--){
		if(tmarkRegionAvg.avgBuf[x] > TMARK_COL_AVG_THRES){
			img->trustmark.isolatedTrustmark.startPoint.x = x;
			break;
		}
	}
	// Jump forward in columns and find end col of trademark
	for(x = img->trustmark.fb.width/2; x<= img->trustmark.fb.width; x++){
		if(tmarkRegionAvg.avgBuf[x] > TMARK_COL_AVG_THRES){
			img->trustmark.isolatedTrustmark.endPoint.x = x;
			break;
		}
	}

	// Free memory from stack variable
	if(NULL != tmarkRegionAvg.avgBuf){
		free(tmarkRegionAvg.avgBuf);
	}

	return err;

}


static void _differenceCalc(Image_Proces_Frame_t* img){
	uint8_t* resizedTrademark = NULL;
	// Create a new array for the resized trustmark
	resizedTrademark = (uint8_t*)malloc(DRINKWORKS_TEMPLATE_TMARK_HEIGHT * DRINKWORKS_TEMPLATE_TMARK_WIDTH * sizeof(uint8_t));
	if(resizedTrademark == NULL){
		IotLogError("Error (differenceCalc): Failed to allocate memory for resized trustmark");
		return;
	}

	// Due to the curvature of the pod and variations in the PM, the acquired trustmark size may be different then the template trustmark size.
	// To normalize the captured trustmark for analysis, nearest neighbor resizing is done to resize trademark for comparison to template.
	// Comparison trademark size is y:DRINKWORKS_TEMPLATE_TMARK_HEIGHT, x:DRINKWORKS_TEMPLATE_TMARK_WIDTH
	float xResizeRatio = ((float) img->trustmark.isolatedTrustmark.endPoint.x - (float) img->trustmark.isolatedTrustmark.startPoint.x) / DRINKWORKS_TEMPLATE_TMARK_WIDTH;													// Create Ratio in x direction that will be used to determine what the nearest neighbor should be
	float yResizeRatio = ((float)img->trustmark.isolatedTrustmark.endPoint.y - (float) img->trustmark.isolatedTrustmark.startPoint.y) / DRINKWORKS_TEMPLATE_TMARK_HEIGHT;													// Create Ratio in y direction that will be used to determine what the nearest neighbor should be
	int32_t xNearestNeighbor = 0;
	int32_t yNearestNeighbor = 0;
	int16_t x, y;

	for (y = 0; y<DRINKWORKS_TEMPLATE_TMARK_HEIGHT; y++) {																												// Loop through pixels of resized trademark in order to set them to nearest neighbor values
		for (x = 0; x<DRINKWORKS_TEMPLATE_TMARK_WIDTH; x++) {
			xNearestNeighbor = x*xResizeRatio;																																	// Determine what pixel from the original trademark array will be used for the resized array. Based on the x ratio
			yNearestNeighbor = y*yResizeRatio;																																	// Determine what pixel from the original trademark array will be used for the resized array. Based on the y ratio
			if(y >= 0 && y < DRINKWORKS_TEMPLATE_TMARK_HEIGHT && x >= 0 && x < DRINKWORKS_TEMPLATE_TMARK_WIDTH && (yNearestNeighbor + img->trustmark.isolatedTrustmark.startPoint.y) < TMARK_AREA_HEIGHT && (xNearestNeighbor + img->trustmark.isolatedTrustmark.startPoint.x) < TMARK_AREA_WIDTH){
				resizedTrademark[(y*DRINKWORKS_TEMPLATE_TMARK_WIDTH)+(x)] = img->trustmark.fb.buf[(yNearestNeighbor + img->trustmark.isolatedTrustmark.startPoint.y)*TMARK_AREA_WIDTH + (xNearestNeighbor + img->trustmark.isolatedTrustmark.startPoint.x)];
			}
		}
	}

	// Calculate the trustmark difference
	img->trustmark.trustmarkDiff = 0;
	for (y = 0; y<DRINKWORKS_TEMPLATE_TMARK_HEIGHT; y++) {																												// Loop through the full resized trademark
		for (x = 0; x<DRINKWORKS_TEMPLATE_TMARK_WIDTH; x++) {
			if (masterDrinkworksTrademark[y][x] == BOOL_WHITE && resizedTrademark[(y*DRINKWORKS_TEMPLATE_TMARK_WIDTH) + x] == BLACK) {															// Compare the resized trademark to the master trademark. If there is a black pixel in the resized trademark that should be white in the master...
				img->trustmark.trustmarkDiff += 1;																																	// Add a 1 point penalty to the total difference value
			}
			else if (masterDrinkworksTrademark[y][x] == BOOL_BLACK && resizedTrademark[(y*DRINKWORKS_TEMPLATE_TMARK_WIDTH) + x] == WHITE) {														// Compare the resized trademark to the master trademark. If there is a white pixel in the resized trademark that should be black in the master...
				img->trustmark.trustmarkDiff += 1;																																	// Add a 1 point penalty to the total difference value
			}
		}
	}

	if(NULL != resizedTrademark){
		free(resizedTrademark);
	}

}


static img_proces_err_t _authenticateTrustmark(Image_Proces_Frame_t* img){
	img_proces_err_t err = IMG_PROCES_OK;

	img->trustmark.fb.width = TMARK_AREA_WIDTH;
	img->trustmark.fb.height = img->barcode2.regionAvg.imgRegion.startPoint.y - img->barcode1.regionAvg.imgRegion.endPoint.y;
	img->trustmark.fb.len = (img->barcode2.regionAvg.imgRegion.startPoint.y - img->barcode1.regionAvg.imgRegion.endPoint.y) * TMARK_AREA_WIDTH;

	if (((int32_t)img->barcode1.regionAvg.imgRegion.endPoint.x + (int32_t)img->barcode1.regionAvg.imgRegion.startPoint.x) / 2 - TMARK_AREA_WIDTH/2 >= 0) {																// If the trademark start location is greater than 0
		img->trustmark.startCol = (img->barcode1.regionAvg.imgRegion.endPoint.x + img->barcode1.regionAvg.imgRegion.startPoint.x) / 2 - TMARK_AREA_WIDTH/2;											// Store the trademark start col location
	}

	if(img->trustmark.startCol + TMARK_AREA_WIDTH < img->fb.width){
		img->trustmark.endCol = img->trustmark.startCol + TMARK_AREA_WIDTH;
	}
	else{
		img->trustmark.endCol = img->fb.width;
	}

	// Create a new array for the trademark
	img->trustmark.fb.buf = (uint8_t*)malloc(img->trustmark.fb.len * sizeof(uint8_t));
	if(img->trustmark.fb.buf == NULL){
		IotLogError("Error (Authenticate Trademark): Failed to allocate memory for trademark");
		return IMG_PROCES_FAIL;
	}

	// Fill the trademark array
	uint32_t x, y;
	for (y = img->barcode1.regionAvg.imgRegion.endPoint.y; y < img->barcode2.regionAvg.imgRegion.startPoint.y && y < img ->fb.height; y++) {			// Fill trademark array with values from expected trademark location based on the start/end rows/cols of the barcodes
		for (x = img->trustmark.startCol; x < img->trustmark.endCol; x++) {
			img->trustmark.fb.buf[(y - img->barcode1.regionAvg.imgRegion.endPoint.y)*TMARK_AREA_WIDTH + x - img->trustmark.startCol] = img->fb.buf[y*img->fb.width + x];
		}
	}

	_gaussianAverage(&(img->trustmark));

	_defineBWThreshold(img);

	_bwThreshold(&(img->trustmark));

	_findTrustmark(img);

	IotLogDebug("trustmark region: (%d,%d), (%d,%d)", img->trustmark.isolatedTrustmark.startPoint.x, img->trustmark.isolatedTrustmark.startPoint.y, img->trustmark.isolatedTrustmark.endPoint.x, img->trustmark.isolatedTrustmark.endPoint.y);

	_differenceCalc(img);

	IotLogInfo("trustmark difference: %d", img->trustmark.trustmarkDiff);

	if(img->trustmark.trustmarkDiff < DRINKWORKS_TRUSTMARK_THRESHOLD){
		img->result.fail = IMG_PROCES_FAIL_NO_FAILURE;
	}
	else{
		img->result.fail |= IMG_PROCES_FAIL_AUTHENTICATION;
		err = IMG_PROCES_FAIL;
	}

	return err;
}

static img_proces_err_t _fillBarcodeAvgRegions(Image_Proces_Frame_t* img){

	img_proces_err_t err = IMG_PROCES_OK;

	// Save the initial start and end points to recall later
	uint32_t	initialStartCol = img->barcode1.regionAvg.imgRegion.startPoint.x;
	uint32_t	initialEndCol = img->barcode1.regionAvg.imgRegion.endPoint.x;

	// Expand barcode region to inclde buffer to left and right of barcode
	if(img->barcode1.regionAvg.imgRegion.startPoint.x >= BCODE_SCAN_OFFSET){
		img->barcode1.regionAvg.imgRegion.startPoint.x -= BCODE_SCAN_OFFSET;
		img->barcode2.regionAvg.imgRegion.startPoint.x -= BCODE_SCAN_OFFSET;
	}
	else {
		img->barcode1.regionAvg.imgRegion.startPoint.x = 0;
		img->barcode2.regionAvg.imgRegion.startPoint.x = 0;
	}

	if(img->barcode1.regionAvg.imgRegion.endPoint.x + BCODE_SCAN_OFFSET <= img->fb.width){
		img->barcode1.regionAvg.imgRegion.endPoint.x += BCODE_SCAN_OFFSET;
		img->barcode2.regionAvg.imgRegion.endPoint.x += BCODE_SCAN_OFFSET;
	}
	else {
		img->barcode1.regionAvg.imgRegion.endPoint.x = img->fb.width - 1;
		img->barcode2.regionAvg.imgRegion.endPoint.x = img->fb.width - 1;
	}

	img->barcode1.regionAvg.len = img->barcode1.regionAvg.imgRegion.endPoint.x - img->barcode1.regionAvg.imgRegion.startPoint.x;

	img->barcode2.regionAvg.len = img->barcode2.regionAvg.imgRegion.endPoint.x - img->barcode2.regionAvg.imgRegion.startPoint.x;

	// Calculate the barcode1 average
	err = _calcImgRegionAvg(&(img->barcode1.regionAvg), &(img->fb));

	if(err == IMG_PROCES_OK){
		_scaleBufferUINT32(img->barcode1.regionAvg.avgBuf, img->barcode1.regionAvg.len, 255);
	}

	// Calculate the barcode2 average
	if(err == IMG_PROCES_OK){
		err = _calcImgRegionAvg(&(img->barcode2.regionAvg), &(img->fb));
	}

	if(err == IMG_PROCES_OK){
		_scaleBufferUINT32(img->barcode2.regionAvg.avgBuf, img->barcode2.regionAvg.len, 255);
	}

	// Recall the start and end columns of the barcode
	img->barcode1.regionAvg.imgRegion.startPoint.x = initialStartCol;
	img->barcode2.regionAvg.imgRegion.startPoint.x = initialStartCol;
	img->barcode1.regionAvg.imgRegion.endPoint.x = initialEndCol;
	img->barcode2.regionAvg.imgRegion.endPoint.x = initialEndCol;

	return err;

}


static img_proces_err_t _thresholdCalc(BarcodeRegion_t* bcode, Image_Proces_Frame_t* img){
	img_proces_err_t err = IMG_PROCES_OK;

	// Allocate memory for top and bottom threshold
	uint32_t* topThres = (uint32_t*)calloc(bcode->regionAvg.len, sizeof(uint32_t));
	if(topThres == NULL){
		err = IMG_PROCES_FAIL;
		IotLogError("Error: Failed to allocate memory for top threshold");
		return err;
	}

	uint32_t* bottomThres = (uint32_t*)calloc(img->barcode2.regionAvg.len, sizeof(uint32_t));
	if(bottomThres == NULL){
		err = IMG_PROCES_FAIL;
		IotLogError("Error: Failed to allocate memory for top threshold");
		return err;
	}

	uint32_t barcodeScanStart = 0;
	if (bcode->regionAvg.imgRegion.startPoint.x >= BCODE_SCAN_OFFSET) {
		barcodeScanStart = bcode->regionAvg.imgRegion.startPoint.x - BCODE_SCAN_OFFSET;
	}

	// Define barcode threshold based on the surrounding whitespace
	uint32_t x, y;
	if(bcode->regionAvg.imgRegion.startPoint.y >= 10 && bcode->regionAvg.imgRegion.endPoint.y <= img->fb.width - 1 - 10){
		for(y = bcode->regionAvg.imgRegion.startPoint.y - 10; y < bcode->regionAvg.imgRegion.startPoint.y - 5; y++){
			for(x = barcodeScanStart; x < barcodeScanStart + bcode->regionAvg.len && x; x++){
				if(img->fb.buf[(y*img->fb.width)+(x)] > 30){
					topThres[x - barcodeScanStart] += img->fb.buf[(y*img->fb.width)+(x)];
				}
				else{
					topThres[x - barcodeScanStart] += 160;
				}
			}
		}

		for(y = bcode->regionAvg.imgRegion.endPoint.y + 5; y < bcode->regionAvg.imgRegion.endPoint.y + 10; y++){
			for (x = barcodeScanStart; x < barcodeScanStart + bcode->regionAvg.len; x++) {
				if(img->fb.buf[(y*img->fb.width)+(x)] > 30){
					bottomThres[x - barcodeScanStart] += img->fb.buf[(y*img->fb.width)+(x)];
				}
				else{
					bottomThres[x - barcodeScanStart] += 160;
				}
			}
		}
	}

	int32_t diff;
	// Normalize values and reduce to provide threshold
	for(x = 0; x < bcode->regionAvg.len; x++){
		diff = (int)topThres[x]/5 - (int)bottomThres[x] / 5;
		if(abs(diff) < 150){
			bcode->thresholdAvg[x] = (topThres[x] / 5 + bottomThres[x] / 5) / 2;
		}
		else{
			if (topThres[x] > bottomThres[x]) {																									// In that case, only use the larger threshold
				bcode->thresholdAvg[x] = topThres[x] / 5;
			}
			else {
				bcode->thresholdAvg[x] = bottomThres[x] / 5;
			}
		}
		bcode->thresholdAvg[x] *= 0.75;
	}

	_medianFilterUINT32(bcode->thresholdAvg, bcode->regionAvg.len, MEDIAN_FILTER_SIZE);

	free(topThres);
	free(bottomThres);

	return err;
}

static img_proces_err_t _defineBcodeThresholds(Image_Proces_Frame_t* img){
	img_proces_err_t err = IMG_PROCES_OK;

	// Allocate memory for the buffers if they have not yet been allocated
	if(img->barcode1.thresholdAvg == NULL){
		img->barcode1.thresholdAvg = (uint32_t*)calloc(img->barcode1.regionAvg.len, sizeof(uint32_t));
		if(img->barcode1.thresholdAvg == NULL){
			err = IMG_PROCES_FAIL;
			IotLogError("Error: Failed to allocate memory for barcode threshold");
			return err;
		}
	}

	if(img->barcode2.thresholdAvg == NULL){
		img->barcode2.thresholdAvg = (uint32_t*)calloc(img->barcode2.regionAvg.len, sizeof(uint32_t));
		if(img->barcode2.thresholdAvg == NULL){
			err = IMG_PROCES_FAIL;
			IotLogError("Error: Failed to allocate memory for barcode threshold");
			return err;
		}
	}


	err = _thresholdCalc(&(img->barcode1), img);

	if(err == IMG_PROCES_OK){
		err = _thresholdCalc(&(img->barcode2), img);
	}


	return err;
}


static img_proces_err_t _eleventhBitCalculation(Image_Proces_Frame_t* img, BarcodeRegion_t* bcode){
	img_proces_err_t err = IMG_PROCES_OK;
	uint32_t y, x, startYScan, scanWidth, thresStartCol, thresEndCol;

	// Check for lower limits
	if((img->barcode1.regionAvg.imgRegion.endPoint.y + img->barcode2.regionAvg.imgRegion.startPoint.y) / 2 < 10 || (img->barcode1.regionAvg.imgRegion.endPoint.y + img->barcode2.regionAvg.imgRegion.startPoint.y) / 2 - 10 > img->fb.height){
		startYScan = 0;
	}
	else{
		startYScan = (img->barcode1.regionAvg.imgRegion.endPoint.y + img->barcode2.regionAvg.imgRegion.startPoint.y) / 2 - 10;
	}

	IotLogDebug("StartYScan: %d", startYScan);
	IotLogDebug("StartCol: %d", bcode->singleBit.startCol);
	IotLogDebug("EndCol: %d", bcode->singleBit.endCol);
	vTaskDelay(10 / portTICK_PERIOD_MS);

	for(y = startYScan; y < startYScan + 20 && y < img->fb.height; y++){
		for(x = bcode->singleBit.startCol; x < bcode->singleBit.endCol && x < img->fb.width; x++){
			bcode->singleBit.sum += img->fb.buf[(y * img->fb.width) + (x)];
		}
	}

	IotLogDebug("SingleBitSum: %d", bcode->singleBit.sum);

	if(bcode->singleBit.endCol > bcode->singleBit.startCol){
		scanWidth = bcode->singleBit.endCol - bcode->singleBit.startCol;
	}
	else{
		err = IMG_PROCES_FAIL;
		IotLogError("Error: Single bit determination scan width");
		return err;
	}

	IotLogDebug("ScanWidth: %d", scanWidth);
	IotLogDebug("TrustmarkStartCol: %d", img->trustmark.startCol);

	// Determine if the bit is to the left or the right of the trademark
	if(bcode->singleBit.startCol > img->trustmark.startCol){
		thresStartCol = bcode->singleBit.startCol - 35;
		thresEndCol = bcode->singleBit.startCol - 15;
	}
	else{
		thresStartCol = bcode->singleBit.endCol + 15;
		thresEndCol = bcode->singleBit.endCol + 35;
	}

	// Use the area around the single bits to set the single bit determination threshold
	// If the area around the single bit is darker, then the threshold will be lower for the single bit
	for(y = startYScan; y < startYScan + 20 && y < img->fb.height; y++){
		for(x = thresStartCol; x < thresEndCol && x < img->fb.width; x++){
			bcode->singleBit.threshold += img->fb.buf[(y * img->fb.width) + (x)];
		}
	}

	IotLogDebug("SingleBitThres: %d", bcode->singleBit.threshold);
	vTaskDelay(10 / portTICK_PERIOD_MS);

	bcode->singleBit.threshold /= 400;

	if(bcode->singleBit.sum / (20 * scanWidth) < (bcode ->singleBit.threshold * 0.5)){
		bcode->singleBit.bitResult = 1;
		IotLogDebug("BitResult=1");
	}
	else{
		bcode->singleBit.bitResult = 0;
		IotLogDebug("BitResult=0");
	}

	return err;
}

static img_proces_err_t _eleventhBitDeterminations(Image_Proces_Frame_t* img){
	img_proces_err_t err = IMG_PROCES_OK;

	img->barcode1.singleBit.startCol = img->barcode1.regionAvg.imgRegion.startPoint.x + SINGLE_BIT_OFFSET_1;
	img->barcode1.singleBit.endCol = img->barcode1.regionAvg.imgRegion.startPoint.x + SINGLE_BIT_OFFSET_2;

	err = _eleventhBitCalculation(img, &(img->barcode1));

	if(err == IMG_PROCES_OK){
		img->barcode2.singleBit.startCol = img->barcode2.regionAvg.imgRegion.endPoint.x - SINGLE_BIT_OFFSET_2;
		img->barcode2.singleBit.endCol = img->barcode2.regionAvg.imgRegion.endPoint.x - SINGLE_BIT_OFFSET_1;
		err = _eleventhBitCalculation(img, &(img->barcode2));
	}

	return err;
}

//******************************************************************************************************************
//* setClosestDistance
//*
//* Description: During the calculation of each barcode region, the gap distance is compared to a bit threshold to determine the number of bits
//*					present in the current gap distance. There are instances where the total number of bits at the end of the calculation is not equal to 12.
//*					In these cases, one of the calculations of bits is incorrect. By determining which calculation was closest to the threshold, we can adjust
//*					the bit numbers and correct for innaccuraies
//*
//* Inputs:
//*		double ratio: current measured ratio for comparison
//*		double thres1: lower threshold for measurement
//*		double thres2: upper threshold for measurement
//*		double *closestDiff: current closest difference value
//*		unsigned char *diffLoc: current closest difference location
//*		unsigned char currLoc: current location
//*
//* Outputs: None, but closest difference and difference location is set by pointers if necessary
//*
//*******************************************************************************************************************
void setClosestDistance(double ratio, double thres1, double thres2, double *closestDiff, unsigned char *diffLoc, unsigned char currLoc) {
	double thresDiff = fabs(ratio - thres1);													// determine the current difference between the ratio and the lower threshold
	if (fabs(ratio - thres2) < thresDiff) {													// If the current ratio is closer to threshold 2 then threshold 1
		thresDiff = fabs(ratio - thres2);														// Overwrite the current difference with the difference between the current ratio and the upper threshold
	}
	if (thresDiff < *closestDiff) {																// If the current threshold difference is lower than the current lowest threshold distance
		*closestDiff = thresDiff;																	// store the current difference as the min threshold difference
		*diffLoc = currLoc;																			// Store the current location for lowest threshold distance
	}
}


//*********************************************************************************************************************
//* CalcID
//*
//* Description: This function determines each barcode ID. It does so using a binary barcode designed specifically for this purpose.
//*
//* Parameters:
//*		int distances[]: Array of bar distances
//*
//* Returns:
//*		10 bit barcode ID
//*
//* Remarks:
//*		For 10bits, or 1024 possible IDs, the barcode only requires 12 equal length segments. For comparison, the
//*		Interleaved 2 of 5 barcode uses 36 individual segments for 1000 possible IDs.
//*********************************************************************************************************************

uint32_t CalcID(int32_t* distances)
{
	double totalDistance = 0;
	uint32_t finalDistancesArray[BARCODE_BITS] = { 0 };
	double barcodeRatio = 0;
	int32_t i = 0;
	uint32_t CalcIntegerID = 0;
	double closestDifference = 100;
	uint8_t closestDiffLocation = 0;

	//****** Calculate the total distance of the barcode based on the input distances ******//
	for (i = 0; distances[i] != 0 && i<BARCODE_BITS; i++) {
		totalDistance += (double)distances[i];
	}

	//****** Determine individual bar distances by ratio comparison to total barcode ******//
	for (i = 0; distances[i] != 0 && i<BARCODE_BITS; i++) {									// For each gap distance
		barcodeRatio = ((double)distances[i]) / totalDistance;								// Divide the gap distance by the total barcode distance
																												// The following if else statements determine the number of bits of the current segment based upon a comparison between the segment ratio and different barcode ratios
		if (barcodeRatio < ONE_BAR_THRES) {
			finalDistancesArray[i] = 1;
			setClosestDistance(barcodeRatio, ONE_BAR_THRES, ONE_BAR_THRES, &closestDifference, &closestDiffLocation, i);
		}
		else if (barcodeRatio < TWO_BAR_THRES) {
			finalDistancesArray[i] = 2;
			setClosestDistance(barcodeRatio, ONE_BAR_THRES, TWO_BAR_THRES, &closestDifference, &closestDiffLocation, i);
		}
		else if (barcodeRatio < THREE_BAR_THRES) {
			finalDistancesArray[i] = 3;
			setClosestDistance(barcodeRatio, TWO_BAR_THRES, THREE_BAR_THRES, &closestDifference, &closestDiffLocation, i);
		}
		else if (barcodeRatio < FOUR_BAR_THRES) {
			finalDistancesArray[i] = 4;
			setClosestDistance(barcodeRatio, THREE_BAR_THRES, FOUR_BAR_THRES, &closestDifference, &closestDiffLocation, i);
		}
		else if (barcodeRatio < FIVE_BAR_THRES) {
			finalDistancesArray[i] = 5;
			setClosestDistance(barcodeRatio, FOUR_BAR_THRES, FIVE_BAR_THRES, &closestDifference, &closestDiffLocation, i);
		}
		else if (barcodeRatio < SIX_BAR_THRES) {
			finalDistancesArray[i] = 6;
			setClosestDistance(barcodeRatio, FIVE_BAR_THRES, SIX_BAR_THRES, &closestDifference, &closestDiffLocation, i);
		}
		else if (barcodeRatio < SEVEN_BAR_THRES) {
			finalDistancesArray[i] = 7;
			setClosestDistance(barcodeRatio, SIX_BAR_THRES, SEVEN_BAR_THRES, &closestDifference, &closestDiffLocation, i);
		}
		else if (barcodeRatio < EIGHT_BAR_THRES) {
			finalDistancesArray[i] = 8;
			setClosestDistance(barcodeRatio, SEVEN_BAR_THRES, EIGHT_BAR_THRES, &closestDifference, &closestDiffLocation, i);
		}
		else if (barcodeRatio < NINE_BAR_THRES) {
			finalDistancesArray[i] = 9;
			setClosestDistance(barcodeRatio, EIGHT_BAR_THRES, NINE_BAR_THRES, &closestDifference, &closestDiffLocation, i);
		}
		else if (barcodeRatio < TEN_BAR_THRES) {
			finalDistancesArray[i] = 10;
			setClosestDistance(barcodeRatio, NINE_BAR_THRES, TEN_BAR_THRES, &closestDifference, &closestDiffLocation, i);
		}
		else if (barcodeRatio < ELEVEN_BAR_THRES) {
			finalDistancesArray[i] = 11;
			setClosestDistance(barcodeRatio, TEN_BAR_THRES, ELEVEN_BAR_THRES, &closestDifference, &closestDiffLocation, i);
		}
		else {
			finalDistancesArray[i] = 12;
		}
	}

	uint8_t distancesTotal = 0;
	for (i = 0; finalDistancesArray[i] != 0 && i < BARCODE_BITS; i++) {
		distancesTotal += finalDistancesArray[i];
	}
	if (distancesTotal < BARCODE_BITS) {														// If the total number of bit is under the expected bit numbers
		finalDistancesArray[closestDiffLocation] += 1;										// add one at the closest difference location
	}
	else if (distancesTotal > BARCODE_BITS) {													// If the total number of bit is over the expected bit numbers
		finalDistancesArray[closestDiffLocation] -= 1;										// subtract one at the closest difference location
	}

	//**************************************
	//* Integer ID is calculated by building a binary number.
	//* Whenever the finalDistanceArray is even, add respected amount of 1's to CalcIntegerID then bitwise shift
	//* When finalDistanceArray is odd, add the respected amount of 0's then bitwise shift
	//* Example: finalDistanceArray is {1, 1, 6, 2, 2}
	//* CalcIntegerID would be built as follows:
	//*      00000000000000000000000000000001
	//*      00000000000000000000000000000010
	//*      00000000000000000000000010111111
	//*      00000000000000000000001011111100
	//*      00000000000000000000101111110011
	//*
	//*      Once all the 1's and 0's have been added, bitwise shift and bitwise AND to remove the start and stop bit
	//***************************************/
	for(i = 0; finalDistancesArray[i] != 0 && i<BARCODE_BITS; i++){
		if(i%2 == 0){
			CalcIntegerID = CalcIntegerID<<finalDistancesArray[i];
			CalcIntegerID += pow(2,finalDistancesArray[i]) - 1;
		}
		else{
			CalcIntegerID = CalcIntegerID<<finalDistancesArray[i];
		}
	}

	// Right bitwise shift to remove stop bit
	CalcIntegerID = CalcIntegerID >>1;
	// Remove start bit
	CalcIntegerID = CalcIntegerID & 0b00000000000000000000001111111111;

	return CalcIntegerID;
}


static img_proces_err_t _decodeBarcode(BarcodeRegion_t* bcode){
	img_proces_err_t err = IMG_PROCES_OK;

	int32_t integerID = 0;																				// Returned ID of barcode
	uint32_t w2bLoc[MAX_TRANS_LOCATIONS] = {0};											// White to Black transition locations array
	uint32_t b2wLoc[MAX_TRANS_LOCATIONS] = {0};											// Black to White transition locations array
	int32_t gapDistance[BARCODE_BITS] = {0};														//	Array of distances between transition locations (gaps)
	int32_t i = 0, j=0, k=0;


	err = _medianFilterUINT32(bcode->regionAvg.avgBuf, bcode->regionAvg.len, MEDIAN_FILTER_SIZE);

	for(i = 0; i<bcode->regionAvg.len; i++){
		if(bcode->regionAvg.avgBuf[i] > bcode->thresholdAvg[i]){
			bcode->regionAvg.avgBuf[i] = WHITE;
		}
		else{
			bcode->regionAvg.avgBuf[i] = BLACK;
		}
	}

	int32_t* barcodeGradient =(int32_t*)calloc(bcode->regionAvg.len, sizeof(uint32_t));
	for(i = 0; i<bcode->regionAvg.len - 2; i++){
		barcodeGradient[i] = (int32_t) bcode->regionAvg.avgBuf[i + 1] - (int32_t) bcode->regionAvg.avgBuf[i];
	}

	uint32_t peakCount = 0;
	for(i = 1; i<bcode->regionAvg.len - 1; i++){																								// Loop through the gradient
		if ((barcodeGradient[i - 1] < barcodeGradient[i]) && (barcodeGradient[i + 1]<=barcodeGradient[i]) && (barcodeGradient[i]>MIN_POS_HT))		// If a peak is detected in the gradient
		{
			if(peakCount < MAX_TRANS_LOCATIONS){																				// If the transition locations are within the limit of the barcode
				w2bLoc[peakCount] = i;																							// Add the transition location to the transition location array
				peakCount++;
			}
		}
	}

	for(i = 1; i<bcode->regionAvg.len; i++){
		barcodeGradient[i] = -barcodeGradient[i];
	}

	peakCount = 0;
	for(i = 1; i<bcode->regionAvg.len - 1; i++)																							// Loop through the gradient
	{
		if ((barcodeGradient[i - 1] < barcodeGradient[i]) && (barcodeGradient[i + 1]<=barcodeGradient[i]) && (barcodeGradient[i]>MIN_POS_HT))		// If a peak is detected in the gradient
		{
			if(peakCount < MAX_TRANS_LOCATIONS){																				// If the transition locations are within the limit of the barcode
				b2wLoc[peakCount] = i;																							// Add the transition location to the transition location array
				peakCount++;
			}
		}
	}

	i=0;
	while (b2wLoc[i] != 0 && k < BARCODE_BITS)										// Calculate White and Black Bar Distances (gaps)) - Added overflow check
	{
		gapDistance[k] = w2bLoc[j] -  b2wLoc [i];
		i++;
		k++;
		gapDistance[k] = b2wLoc[i] - w2bLoc[j];
		j++;
		k++;
	}

	if(k){																						// Added overflow check
		gapDistance[k-1] = 0;																// Remove last gap distance as it is irrelevant to the calculation
	}

	for (i = 0; i < BARCODE_BITS; i++) {												// Loop through the gap distances
		if (gapDistance[i] < MIN_BAR_DISTANCE && gapDistance[i]>0) {			// If the current gap distance exists (>0) and is smaller then the min gap distance
			if (i == 0) {																		// If this is the first bar
				gapDistance[i] += gapDistance[i + 1];									// Combine the first and second gap
				for (j = i; j + 1 < BARCODE_BITS; j++) {								// shift the rest of the gap distances one to the left
					gapDistance[j] = gapDistance[j + 1];
				}
				gapDistance[BARCODE_BITS - 1] = 0;										// replace the last shift with a 0
				i--;
			}
			else if (i > 0 && i < BARCODE_BITS - 1) {									// If the current bar is in the middle of the barcode array
				if (gapDistance[i + 1] != 0) {											// Ensure that this is not the last bar (next segment 0)
					gapDistance[i - 1] += gapDistance[i] + gapDistance[i + 1];	// Combine the current and next gap with the previous gap. Idea being that the current gap is glare and should be corrected
					for (j = i; j < BARCODE_BITS - 2; j++) {							// shift the rest of the gap distances two to the left
						gapDistance[j] = gapDistance[j + 2];
					}
					gapDistance[BARCODE_BITS - 1] = 0;									// replace the last two gap distances
					gapDistance[BARCODE_BITS - 2] = 0;
					i--;
				}
				else {																			// If this is the last bar
					gapDistance[i] = 0;														// Set the current gap distance to 0
					gapDistance[i - 1] = 0;													// set the previous gap distance to 0
				}
			}
			else {																					// if this is the last gap in the array
				gapDistance[BARCODE_BITS - 2] += gapDistance[BARCODE_BITS - 1];	// Combine the current gap with the previous gap
				gapDistance[BARCODE_BITS - 1] = 0;											// Set the current gap distance to 0
			}
		}
	}

	integerID = CalcID(gapDistance);															// Decode barcode based upon gap distances

	if(bcode->singleBit.bitResult == 1){
		bcode->barcodeResult = integerID + 1024;
	}
	else{
		bcode->barcodeResult = integerID;
	}

	IotLogInfo("Barcode: %d", bcode->barcodeResult);

	free(barcodeGradient);

	return err;
}

void _resetBarcodeResults(Image_Proces_Frame_t* img){
	imageProces_CleanupFrame(img);

	img->rowAvg = (Image_Region_Avg_t){{{ROW_AVG_START_COL, 0}, {ROW_AVG_END_COL, VGA_HEIGHT}}, ROW_SCAN, NULL, VGA_HEIGHT};
	img->colAvg = (Image_Region_Avg_t){{{0, 0}, {0, 0}}, COL_SCAN, NULL, 0};
	img->barcode1 = (BarcodeRegion_t){{{{0, 0}, {0, 0}}, COL_SCAN, NULL, 0}, NULL, {0, 0, 0, 0, 0}, NOT_INITIALIZED};
	img->barcode2 = (BarcodeRegion_t){{{{0, 0}, {0, 0}}, COL_SCAN, NULL, 0}, NULL, {0, 0, 0, 0, 0}, NOT_INITIALIZED};
	img->trustmark = (Trustmark_t){ {NULL, 0, 0, 0, PIXFORMAT_GRAYSCALE, {0,0}}, 0, 0, 0, {{0,0},{0,0}}, 0};
	img->result = (Image_Decode_Result_t){NOT_INITIALIZED, IMG_PROCES_FAIL_NOT_INITIALIZED};
}

img_proces_err_t imageProces_DecodeDWBarcode(Image_Proces_Frame_t* img){
	img_proces_err_t err = IMG_PROCES_OK;
	uint32_t currentScanRow = 0;
	_resetBarcodeResults(img);

	// NEED TO ADD THIS FUNCTIONALITY
	//err = _checkForPod(img);

	// Calculate the row average from the frame buffer
	err = _calcImgRegionAvg(&(img->rowAvg), &(img->fb));

	if(err == IMG_PROCES_OK){
		err = _medianFilterUINT32(img->rowAvg.avgBuf, img->rowAvg.len, MEDIAN_FILTER_SIZE);
	}

	if(err == IMG_PROCES_OK){
		_scaleBufferUINT32(img->rowAvg.avgBuf, img->rowAvg.len, 255);
	}

	while(img->result.fail != IMG_PROCES_FAIL_NO_FAILURE && currentScanRow < img->fb.height){
		if(err == IMG_PROCES_OK){
			err = _determineStartStopRow(img, &currentScanRow);
			if(err){
				img->result.fail |= IMG_PROCES_FAIL_RECOGNITION;
				break;
			}
		}

		if(err == IMG_PROCES_OK){
			err = _determineStartStopCol(img);
			if (err) {
				img->result.fail |= IMG_PROCES_FAIL_RECOGNITION;
				break;
			}
		}

		if(err == IMG_PROCES_OK){
			err = _authenticateTrustmark(img);
			if(err){
				err = IMG_PROCES_OK;
				currentScanRow += 10;
			}
		}
	}

	if(err == IMG_PROCES_OK){
		err = _fillBarcodeAvgRegions(img);
	}

	if(err == IMG_PROCES_OK){
		err = _defineBcodeThresholds(img);
	}

	if(err == IMG_PROCES_OK){
		err = _eleventhBitDeterminations(img);
	}

	if(err == IMG_PROCES_OK){
		err = _decodeBarcode(&(img->barcode1));
	}

	if(err == IMG_PROCES_OK){
		err = _decodeBarcode(&(img->barcode2));
	}

	return err;
}


/**
 * @brief Cleanup malloc'd memory
 *
 */
void imageProces_CleanupFrame(Image_Proces_Frame_t* img){

	if(NULL != img->rowAvg.avgBuf){
		free(img->rowAvg.avgBuf);
		img->rowAvg.avgBuf = NULL;
	}

	if(NULL != img->colAvg.avgBuf){
		free(img->colAvg.avgBuf);
		img->colAvg.avgBuf = NULL;
	}

	if(NULL != img->barcode1.thresholdAvg){
		free(img->barcode1.thresholdAvg);
		img->barcode1.thresholdAvg = NULL;
	}

	if(NULL != img->barcode1.regionAvg.avgBuf){
		free(img->barcode1.regionAvg.avgBuf);
		img->barcode1.regionAvg.avgBuf = NULL;
	}

	if(NULL != img->barcode2.thresholdAvg){
		free(img->barcode2.thresholdAvg);
		img->barcode2.thresholdAvg = NULL;
	}

	if(NULL != img->barcode2.regionAvg.avgBuf){
		free(img->barcode2.regionAvg.avgBuf);
		img->barcode2.regionAvg.avgBuf = NULL;
	}

	if(img->trustmark.fb.buf != NULL){
		free(img->trustmark.fb.buf);
	}
}

