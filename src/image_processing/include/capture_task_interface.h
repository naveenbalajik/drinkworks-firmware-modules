/*
 * capture_task.h
 *
 *  Created on: January 8, 2020
 *      Author: Nicholas.Weber
 */

#ifndef CAPTURE_TASK_H_
#define CAPTURE_TASK_H_

#include "image_processing.h"
#include "esp_camera.h"

typedef enum{
	eCAM_LED_OFF = 0,
	eCAM_LED_ON
}eCamLED_ONOFF_t;

typedef struct{
	int32_t		pin;
	bool		onLogicLevel;
}LED_setup_t;

typedef struct{
	uint8_t reg_addr;
	uint8_t reg_val;
}addr_val_list;

typedef struct{
	const camera_config_t* 	camConfig;
	const addr_val_list * 	addrVals;
	const LED_setup_t *		LED;
	const uint8_t			i2cAddr;
	const uint32_t			i2cSpeed;
	const uint32_t			runtimeSpeed;
}camera_setup_t;

int32_t 			imgCapture_init(const camera_setup_t * camSetup);
int32_t 			imgCapture_CaptureAndDecode(imgCaptureCommandCallback_t cb);
int32_t 			imgCapture_ResetSensor(void);
int32_t 			imgCapture_setCamLEDs(eCamLED_ONOFF_t	level);

#endif /* CAPTURE_TASK_H_ */
