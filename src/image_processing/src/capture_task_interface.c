/*
 * capture_task_interface.c
 *
 *  Created on: January 8, 2020
 *      Author: Nicholas.Weber
 */

#include <stdio.h>
#include <stdlib.h>
#include "image_processing.h"
#include "sensor.h"
#include "esp_timer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <string.h>
#include <math.h>

/* Debug Logging */
#include "image_processing_logging.h"

#include "driver/ledc.h"
#include "driver/i2c.h"

#include "capture_task_interface.h"


#define IMG_CAPTURE_STACK_SIZE		4096
#define IMG_CAPTURE_PRIORITY		12

#define NOT_INITIALIZED						-1

static LED_setup_t	_camLED = {NOT_INITIALIZED, 0};

typedef enum
{
	eResetSensor,
	eCaptureImage,
	eCamLED_ON,
	eCamLED_OFF
}imgCapture_Command_t;


typedef struct
{
	imgCapture_Command_t			command;
	imgCaptureCommandCallback_t		callback;
}imgProces_QueueItem_t;


static TaskHandle_t _captureTaskHandle;


QueueHandle_t	imgProces_Queue = NULL;

void _setLEDLevel(eCamLED_ONOFF_t	level)
{
	// Ensure that the camera LED has been initialized
	if(_camLED.pin >= 0){
		// Set the LED level based on the LED settings
		if(level == eCAM_LED_OFF){
			gpio_set_level(_camLED.pin, !(_camLED.onLogicLevel));
		}
		else{
			gpio_set_level(_camLED.pin, _camLED.onLogicLevel);
		}
	}
	else{
		IotLogError("Error setting camera LEDs. LED pin not initialized. Initialize with imgCapture_init" );
	}
}

esp_err_t _xclk_timer_conf(int ledc_timer, int xclk_freq_hz)
{
    ledc_timer_config_t timer_conf;
    timer_conf.duty_resolution = 2;
    timer_conf.freq_hz = xclk_freq_hz;
    timer_conf.speed_mode = LEDC_HIGH_SPEED_MODE;
#if ESP_IDF_VERSION_MAJOR >= 4
    timer_conf.clk_cfg = LEDC_AUTO_CLK;
#endif
    timer_conf.timer_num = (ledc_timer_t)ledc_timer;
    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) {
    	IotLogError( "ledc_timer_config failed for freq %d, rc=%x", xclk_freq_hz, err);
    }
    return err;
}

esp_err_t _xclk_timer_set_duty(const camera_config_t * camConfig, int dutyPercent)
{
	// Resolution of duty is only 2 bits, so take duty percentage and divide by 25 get setting
	int duty = dutyPercent / 25;
    esp_err_t err = ledc_set_duty(LEDC_HIGH_SPEED_MODE, camConfig->ledc_channel, duty);
    if (err != ESP_OK) {
    	IotLogError( "ledc_set_duty failed for duty %d, rc=%x", dutyPercent, err);
    }

    err = ledc_update_duty(LEDC_HIGH_SPEED_MODE, camConfig->ledc_channel);
        if (err != ESP_OK) {
        	IotLogError( "ledc_update_duty failed for rc=%x", err);
        }

    return err;
}


static void _reset_sensor(camera_setup_t * cam_setup)
{
	esp_err_t err = ESP_OK;

	// Pull reset pin
    gpio_config_t conf = { 0 };
    conf.pin_bit_mask = 1LL << cam_setup->camConfig->pin_reset;
    conf.mode = GPIO_MODE_OUTPUT;
    gpio_config(&conf);
    gpio_matrix_out(cam_setup->camConfig->pin_reset, SIG_GPIO_OUT_IDX, true, false);             /* Invert signal */

    gpio_set_level(cam_setup->camConfig->pin_reset, 0);
    vTaskDelay(30 / portTICK_PERIOD_MS);
    gpio_set_level(cam_setup->camConfig->pin_reset, 1);
    vTaskDelay(10 / portTICK_PERIOD_MS);


	// Set camera freq to I2C speed
	_xclk_timer_conf(cam_setup->camConfig->ledc_timer, cam_setup->i2cSpeed);

	// Start the MCLK
	_xclk_timer_set_duty(cam_setup->camConfig, 50);

	// Set registers over I2C
	const addr_val_list* currentRegVal = cam_setup->addrVals;
	i2c_cmd_handle_t cmd;
	while(!(currentRegVal->reg_addr == 0xff && currentRegVal->reg_val == 0xff)){
		cmd = i2c_cmd_link_create();
		i2c_master_start(cmd);
		i2c_master_write_byte(cmd, cam_setup->i2cAddr, 1);
		i2c_master_write_byte(cmd, currentRegVal->reg_addr, 1);
		i2c_master_write_byte(cmd, currentRegVal->reg_val, 1);
		i2c_master_stop(cmd);
		err = i2c_master_cmd_begin(I2C_NUM_1, cmd, 1000);
		i2c_cmd_link_delete(cmd);
		if(err != ESP_OK){
			IotLogError("Failed setting camera register settings. Err");
		}
		currentRegVal++;
	}

	vTaskDelay(100 / portTICK_PERIOD_MS);

	// Set the camera freq back to the initial value
	_xclk_timer_conf(cam_setup->camConfig->ledc_timer, cam_setup->runtimeSpeed);

	// Stop the MCLK
	_xclk_timer_set_duty(cam_setup->camConfig, 0);

}


static void _initCamLEDs(const LED_setup_t *	LED){
	// Store the LED pin
	memcpy(&_camLED, LED, sizeof(LED_setup_t));

	// Configure the LED pin as input/output
	gpio_config_t conf = { 0 };
	conf.intr_type = GPIO_INTR_DISABLE;
	conf.pin_bit_mask = 1<<LED->pin;
	conf.mode = GPIO_MODE_OUTPUT;
	conf.pull_down_en = 0;
	conf.pull_up_en = 0;
	gpio_config(&conf);

	// Turn off the LED
	imgCapture_setCamLEDs(eCAM_LED_OFF);
}



static void _captureTask( void * arg)
{
	// Receive queue capable of handling 9 messages
	imgProces_Queue = xQueueCreate(9, sizeof(imgProces_QueueItem_t));
	imgProces_QueueItem_t	currentCmd;

	// Parse input commands
	camera_setup_t * cam_setup = (camera_setup_t *) arg;

	for( ;; )
	{
		// Poll the queue for receive messages
		if(xQueueReceive(imgProces_Queue, &currentCmd, 5000/portTICK_PERIOD_MS) == pdPASS){
			switch(currentCmd.command)
			{
				case eResetSensor:
					_reset_sensor(cam_setup);
					break;

				case eCaptureImage:
					// Turn ON LEDs and MCLK for capture
					_setLEDLevel(eCAM_LED_ON);
					_xclk_timer_set_duty(cam_setup->camConfig, 50);
					// Capture image
					imageProces_CaptureAndDecodeImg(currentCmd.callback);
					// Turn OFF LEDs and MCLK after capture is complete
					_setLEDLevel(eCAM_LED_OFF);
					_xclk_timer_set_duty(cam_setup->camConfig, 0);
					break;

				case eCamLED_ON:
					_setLEDLevel(eCAM_LED_ON);
					break;

				case eCamLED_OFF:
					_setLEDLevel(eCAM_LED_OFF);
					break;
			}
		}
	}
}

int32_t _sendToQueue(imgCapture_Command_t	command, imgCaptureCommandCallback_t	callback)
{
	img_proces_err_t err = IMG_PROCES_OK;
	bool sentToQueue = 0;
	imgProces_QueueItem_t	queueCmd = {command, callback};

	IotLogDebug("_sendToQueue: %d", command);

	if(imgProces_Queue != NULL){
		sentToQueue = xQueueSendToBack(imgProces_Queue, (void *)&queueCmd, 0);
	}
	else{
		IotLogError("Error: Image processing queue == NULL");
		err = IMG_PROCES_FAIL;
	}

	if(!sentToQueue){
		IotLogError("Error: Queue Full");
		err = IMG_PROCES_FAIL;
	}

	return err;
}

int32_t imgCapture_ResetSensor(void){
	return _sendToQueue(eResetSensor, NULL);
}

int32_t imgCapture_CaptureAndDecode(imgCaptureCommandCallback_t cb){
	return _sendToQueue(eCaptureImage, cb);
}


int32_t imgCapture_setCamLEDs(eCamLED_ONOFF_t	level)
{
	int32_t err = IMG_PROCES_OK;

	switch(level){

		case eCAM_LED_OFF:
			err = _sendToQueue(eCamLED_OFF, NULL);
			break;

		case eCAM_LED_ON:
			err = _sendToQueue(eCamLED_ON, NULL);
			break;
	}

	return err;
}


int32_t imgCapture_init(const camera_setup_t * camSetup)
{
	int err = IMG_PROCES_OK;

	// Initialize camera in esp camera component.
	err = esp_camera_init(camSetup->camConfig);
	// Stop the MCLK after initialize
	_xclk_timer_set_duty(camSetup->camConfig, 0);

	if (err != ESP_OK) {
		IotLogError("Camera init failed with error 0x%x", err);
		return err;
	}

	if(err == ESP_OK)
	{
		// Create the image capture task
		xTaskCreate(_captureTask, "capture_task", IMG_CAPTURE_STACK_SIZE, (void*) camSetup, IMG_CAPTURE_PRIORITY, &_captureTaskHandle );
	}

	if(_captureTaskHandle == NULL)
	{
		err = IMG_PROCES_FAIL;
		IotLogError("Error: Capture task could not be created");
	}

	// Camera LED init
	_initCamLEDs(camSetup->LED);

	return err;
}
