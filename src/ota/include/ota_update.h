/*
 * ota_update.h
 *
 *  Created on: Jun 2, 2020
 *      Author: ian.whitehead
 */

#ifndef _OTA_UPDATE_H_
#define _OTA_UPDATE_H_

#include "aws_iot_ota_agent.h"

/*--------------- Secondary Processor OTA callbacks --------------------------*/

/**
 * @ingroup ota_datatypes_functionpointers
 * @brief OTA update complete callback function typedef.
 *
 * The user may register a callback function when initializing the OTA Agent. This
 * callback is used to notify the main application when the OTA update job is complete.
 * Typically, it is used to reset the device after a successful update by calling
 * @ref ota_function_activatenewimage and may also be used to kick off user specified self tests
 * during the Self Test phase. If the user does not supply a custom callback function,
 * a default callback handler is used that automatically calls @ref ota_function_activatenewimage
 * after a successful update.
 *
 * The callback function is called with one of the following arguments:
 *
 *      eOTA_JobEvent_Activate      OTA update is authenticated and ready to activate.
 *      eOTA_JobEvent_Fail          OTA update failed. Unable to use this update.
 *      eOTA_JobEvent_StartTest     OTA job is now ready for optional user self tests.
 *
 * When eOTA_JobEvent_Activate is received, the job status details have been updated with
 * the state as ready for Self Test. After reboot, the new firmware will (normally) be
 * notified that it is in the Self Test phase via the callback and the application may
 * then optionally run its own tests before committing the new image.
 *
 * If the callback function is called with a result of eOTA_JobEvent_Fail, the OTA update
 * job has failed in some way and should be rejected.
 *
 * @param[in] eEvent An OTA update event from the OTA_JobEvent_t enum.
 */
typedef void (* pxOTACompleteCallback_t)( OTA_JobEvent_t eEvent );

/**
 * @ingroup ota_datatypes_functionpointers
 * @brief OTA abort callback function typedef.
 *
 * The user may register a callback function when initializing the OTA Agent. This
 * callback is used to override the behavior of how a job is aborted.
 *
 * @param[in] C File context of the job being aborted
 */
typedef OTA_Err_t (* pxAltOTAPALAbort_t)( OTA_FileContext_t * const C );

/**
 * @ingroup ota_datatypes_functionpointers
 * @brief OTA new image received callback function typedef.
 *
 * The user may register a callback function when initializing the OTA Agent. This
 * callback is used to override the behavior of what happens when a new image is
 * activated.
 */
typedef OTA_Err_t (* pxAltOTAPALActivateNewImage_t)( void );

/**
 * @ingroup ota_datatypes_functionpointers
 * @brief OTA close file callback function typedef.
 *
 * The user may register a callback function when initializing the OTA Agent. This
 * callback is used to override the behavior of what happens when a file is closed.
 *
 * @param[in] C File context of the job being aborted
 */
typedef OTA_Err_t (* pxAltOTAPALCloseFile_t)( OTA_FileContext_t * const C );

/**
 * @ingroup ota_datatypes_functionpointers
 * @brief OTA create file to store received data callback function typedef.
 *
 * The user may register a callback function when initializing the OTA Agent. This
 * callback is used to override the behavior of how a new file is created.
 *
 * @param[in] C File context of the job being aborted
 * @param[in] pParam Pointer to parameters (e.g. Partition Descriptor)
 */
typedef OTA_Err_t (* pxAltOTAPALCreateFileForRx_t)( OTA_FileContext_t * const C, void * pParam );

/**
 * @ingroup ota_datatypes_functionpointers
 * @brief OTA Get Platform Image State callback function typedef.
 *
 * The user may register a callback function when initializing the OTA Agent. This
 * callback is used to override the behavior of returning the platform image state.
 */
typedef OTA_PAL_ImageState_t (* pxAltOTAPALGetPlatformImageState_t)( void );

/**
 * @ingroup ota_datatypes_functionpointers
 * @brief OTA Reset Device callback function typedef.
 *
 * The user may register a callback function when initializing the OTA Agent. This
 * callback is used to override the behavior of what happens when the OTA agent resets the device.
 */
typedef OTA_Err_t (* pxAltOTAPALResetDevice_t)( void );

/**
 * @ingroup ota_datatypes_functionpointers
 * @brief OTA Set Platform Image State callback function typedef.
 *
 * The user may register a callback function when initializing the OTA Agent. This
 * callback is used to override the behavior of how a platform image state is stored.
 *
 * @param[in] eState Platform Image State to be state
 */
typedef OTA_Err_t (* pxAltOTAPALSetPlatformImageState_t)( OTA_ImageState_t eState );

/**
 * @ingroup ota_datatypes_functionpointers
 * @brief OTA Write Block callback function typedef.
 *
 * The user may register a callback function when initializing the OTA Agent. This
 * callback is used to override the behavior of how a block is written to a file.
 *
 * @param[in] C File context of the job being aborted
 * @param[in] iOffset Offset into the file to write the data
 * @param[in] pacData Data to be written at the offset
 * @param[in] iBlocksize Block size of the data to be written
 */
typedef int16_t (* pxAltOTAPALWriteBlock_t)( OTA_FileContext_t * const C,
                                                  uint32_t iOffset,
                                                  uint8_t * const pacData,
                                                  uint32_t iBlockSize );

/**
 * @ingroup ota_datatypes_functionpointers
 * @brief Custom Job callback function typedef.
 *
 * The user may register a callback function when initializing the OTA Agent. This
 * callback will be called when the OTA agent cannot parse a job document.
 *
 * @param[in] pcJSON Pointer to the json document received by the OTA agent
 * @param[in] ulMsgLen Length of the json document received by the agent
 */
typedef OTA_JobParseErr_t (* pxOTACustomJobCallback_t)( const char * pcJSON,
                                                        uint32_t ulMsgLen );


/*--------------------------- OTA structs ----------------------------*/
/**
 * @ingroup ota_datatypes_structs
 * @brief OTA PAL Alternate Processor function table */
typedef struct
{
    pxAltOTAPALAbort_t xAbort;                                 /* OTA Abort callback pointer */
    pxAltOTAPALActivateNewImage_t xActivateNewImage;           /* OTA Activate New Image callback pointer */
    pxAltOTAPALCloseFile_t xCloseFile;                         /* OTA Close File callback pointer */
    pxAltOTAPALCreateFileForRx_t xCreateFileForRx;             /* OTA Create File for Receive callback pointer */
    pxAltOTAPALGetPlatformImageState_t xGetImageState;         /* OTA Get Platform Image State callback pointer */
    pxAltOTAPALResetDevice_t xResetDevice;                     /* OTA Reset Device callback pointer */
    pxAltOTAPALSetPlatformImageState_t xSetImageState;         /* OTA Set Platform Image State callback pointer */
    pxAltOTAPALWriteBlock_t xWriteBlock;                       /* OTA Write Block callback pointer */
//    pxOTACompleteCallback_t xCompleteCallback;                      /* OTA Job Completed callback pointer */
//    pxOTACustomJobCallback_t xCustomJobCallback;                    /* OTA Custom Job callback pointer */
} AltProcessor_Functions_t;

/**
 * @brief Type definition of function to call to get Host Ota Update module's status
 */
typedef bool (* hostOtaPendUpdateCallback_t)( void );

/**
 * @brief
 */
int OTAUpdate_init( 	const char * pIdentifier,
                            void * pNetworkCredentialInfo,
                            const IotNetworkInterface_t * pNetworkInterface,
							IotSemaphore_t *pSemaphore,
							hostOtaPendUpdateCallback_t function,
							const AltProcessor_Functions_t * altProcessorFunctions );


#endif /* _OTA_UPDATE_H_ */
