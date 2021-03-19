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
	camera_fb_t		fb;
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

typedef void (* imgCaptureCommandCallback_t)(Image_Proces_Frame_t* img);

img_proces_err_t	imageProces_CaptureAndDecodeImg(imgCaptureCommandCallback_t	callback);
img_proces_err_t 	imageProces_DecodeDWBarcode(Image_Proces_Frame_t* img);
void 				imageProces_CleanupFrame(Image_Proces_Frame_t* img);

#endif /* IMAGE_PROCESSING_H_ */
