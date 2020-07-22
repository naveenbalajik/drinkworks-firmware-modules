/*
 * image_processing.h
 *
 *  Created on: Jul 21, 2020
 *      Author: Nicholas.Weber
 */

#ifndef IMAGE_PROCESSING_H_
#define IMAGE_PROCESSING_H_

#include "esp_camera.h"

#define IMG_PROCES_OK		0
#define IMG_PROCES_FAIL		-1

#define NOT_INITIALIZED						-1
#define VGA_WIDTH 							640
#define	VGA_HEIGHT							480
#define	ROW_AVG_START_COL					200
#define	ROW_AVG_END_COL						440


// NOTE: Set a default macro for img initialization. Img region average buffers set to NULL
#define VGA_IMG_DEFAULT_INITIALIZATION  {	\
										.fb = {NULL, VGA_WIDTH*VGA_HEIGHT, VGA_WIDTH, VGA_HEIGHT, PIXFORMAT_GRAYSCALE, {0,0}}, \
										.rowAvg = {{{ROW_AVG_START_COL, 0}, {ROW_AVG_END_COL, VGA_HEIGHT}}, ROW_SCAN, NULL, VGA_HEIGHT}, \
										.colAvg = {{{0, 0}, {0, 0}}, COL_SCAN, NULL, 0}, \
										.barcode1 = {{{{0, 0}, {0, 0}}, COL_SCAN, NULL, 0}, NULL, {0, 0, 0, 0, 0}, NOT_INITIALIZED}, \
										.barcode2 = {{{{0, 0}, {0, 0}}, COL_SCAN, NULL, 0}, NULL, {0, 0, 0, 0, 0}, NOT_INITIALIZED}, \
										.trustmark = {NULL, 0, 0, 0, 0, 0, 0, {{0,0},{0,0}}, 0}, \
										.result = {NOT_INITIALIZED, IMG_PROCES_FAIL_NOT_INITIALIZED}, \
										}

typedef int32_t		img_proces_err_t;

typedef enum {
	IMG_PROCES_FAIL_NOT_INITIALIZED = 0,
	IMG_PROCES_FAIL_NO_FAILURE,
	IMG_PROCES_FAIL_RECOGNITION,
	IMG_PROCES_FAIL_AUTHENTICATION,
	IMG_PROCES_FAIL_AUTHENTICATION_RECOGNITION,
} Img_Proces_Failure_t;

typedef struct {
	uint32_t	x;
	uint32_t 	y;
}ImgPoint_t;

typedef struct {
	ImgPoint_t	startPoint;
	ImgPoint_t	endPoint;
}Image_Region_t;

typedef enum {
	ROW_SCAN,
	COL_SCAN,
}Scan_Direction_t;

typedef struct {
	Image_Region_t		imgRegion;
	Scan_Direction_t	scanDirection;
	uint32_t*			avgBuf;
	uint32_t			len;
}Image_Region_Avg_t;

typedef struct {
	uint8_t*		buf;
	uint32_t		width;
	uint32_t		height;
	uint32_t		length;
	uint32_t		startCol;
	uint32_t		endCol;
	uint32_t		bwThres;
	Image_Region_t	isolatedTrustmark;
	int32_t			trustmarkDiff;
}Trustmark_t;

typedef struct {
	int8_t				bitResult;
	uint32_t			startCol;
	uint32_t			endCol;
	uint32_t			sum;
	int32_t				threshold;
}Single_Bit_t;

typedef struct {
	Image_Region_Avg_t	regionAvg;
	uint32_t*			thresholdAvg;
	Single_Bit_t		singleBit;
	int32_t				barcodeResult;
}BarcodeRegion_t;

typedef struct {
	int8_t		podDetected;
	Img_Proces_Failure_t fail;
}Image_Decode_Result_t;

typedef struct {
	camera_fb_t				fb;
	Image_Region_Avg_t		rowAvg;			// Row Average Data
	Image_Region_Avg_t		colAvg;			// Column Average Data
	BarcodeRegion_t			barcode1;
	BarcodeRegion_t			barcode2;
	Trustmark_t				trustmark;
	Image_Decode_Result_t	result;
}Image_Proces_Frame_t;

img_proces_err_t 	imageProces_DecodeDWBarcode(Image_Proces_Frame_t* img);
void 				imageProces_CleanupFrame(Image_Proces_Frame_t* img);

#endif /* IMAGE_PROCESSING_H_ */
