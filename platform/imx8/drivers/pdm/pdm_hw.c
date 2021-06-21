/*
 * The Clear BSD License
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016-2021 NXP
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted (subject to the limitations in the disclaimer below) provided
 * that the following conditions are met:
 *
 * o Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * o Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY THIS LICENSE.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <compiler.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <delay.h>
#include "pdm_hw.h"
/* Component ID definition, used by tools. */
#ifndef FSL_COMPONENT_ID
#define FSL_COMPONENT_ID "platform.drivers.pdm"
#endif

/*******************************************************************************
 * Definitations
 ******************************************************************************/
/*! @brief Typedef for pdm rx interrupt handler. */
typedef void (*pdm_isr_t)(PDM_Type *base, pdm_handle_t *pdmHandle);
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
/*!
 * @brief Get the instance number for PDM.
 *
 * @param base PDM base pointer.
 */
uint32_t PDM_GetInstance(PDM_Type *base);

/*!
 * @brief Get the instance number for PDM.
 *
 * @param channelMask enabled channel.
 * @param qualitymode selected quality mode.
 * @param osr      oversample rate.
 * @param regdiv   register divider.
 */
static status_t PDM_ValidateSrcClockRate(uint32_t channelMask,
                                         pdm_df_quality_mode_t qualityMode,
                                         uint8_t osr,
                                         uint32_t regDiv);


/*******************************************************************************
 * Variables
 ******************************************************************************/
/* Base pointer array */
static PDM_Type *const s_pdmBases[] = PDM_BASE_PTRS;
#if !(defined(FSL_SDK_DISABLE_IRQ) && FSL_SDK_DISABLE_IRQ)
/*!@brief PDM handle pointer */
pdm_handle_t *s_pdmHandle[ARRAY_SIZE(s_pdmBases)];
#endif

#if !(defined(FSL_SDK_DISABLE_DRIVER_CLOCK_CONTROL) && FSL_SDK_DISABLE_DRIVER_CLOCK_CONTROL)
/* Clock name array */
static const clock_ip_name_t s_pdmClock[] = PDM_CLOCKS;
#endif /* FSL_SDK_DISABLE_DRIVER_CLOCK_CONTROL */

#if !(defined(FSL_SDK_DISABLE_IRQ) && FSL_SDK_DISABLE_IRQ)
/*! @brief Pointer to tx IRQ handler for each instance. */
static pdm_isr_t s_pdmIsr;
#endif

/*******************************************************************************
 * Code
 ******************************************************************************/
uint32_t PDM_GetInstance(PDM_Type *base)
{
    uint32_t instance;

    /* Find the instance index from base address mappings. */
    for (instance = 0; instance < ARRAY_SIZE(s_pdmBases); instance++)
    {
        if (s_pdmBases[instance] == base)
        {
            break;
        }
    }

    assert(instance < ARRAY_SIZE(s_pdmBases));

    return instance;
}

/*!
 * brief PDM read data non blocking.
 * So the actually read data byte size in this function is (size * 2 * channelNums).
 * param base PDM base pointer.
 * param startChannel start channel number.
 * param channelNums total enabled channelnums.
 * param buffer received buffer address.
 * param size number of 16bit data to read.
 */
void PDM_ReadNonBlocking(PDM_Type *base, uint32_t startChannel, uint32_t channelNums, int16_t *buffer, size_t size)
{
    uint32_t i = 0, j = 0U;
    uint32_t *startSrcAddr = (uint32_t *)(&base->DATACH[startChannel]);
    for (i = 0U; i < size; i++)
    {
        for (j = 0; j < channelNums; j++)
        {
            *buffer++ = startSrcAddr[j];
        }
    }
}

static status_t PDM_ValidateSrcClockRate(uint32_t channelMask,
                                         pdm_df_quality_mode_t qualityMode,
                                         uint8_t osr,
                                         uint32_t regDiv)
{
    uint32_t enabledChannel = 0U, i = 0U, factor = 0U;

    for (i = 0U; i < FSL_FEATURE_PDM_CHANNEL_NUM; i++)
    {
        if (channelMask >> i)
        {
            enabledChannel++;
        }
    }

    if (qualityMode <= kPDM_QualityModeLow)
    {
        factor = 125U;
    }
    else
    {
        factor = 19U;
    }

    if (regDiv < ((10U + factor * enabledChannel) / (8U * osr)))
    {
        return kStatus_Fail;
    }

    return kStatus_Success;
}

/*!
 * brief PDM set sample rate.
 *
 * note This function is depend on the configuration of the PDM and PDM channel, so the correct call sequence is
 * code
 * PDM_Init(base, pdmConfig)
 * PDM_SetChannelConfig(base, channel, &channelConfig)
 * PDM_SetSampleRateConfig(base, source, sampleRate)
 * endcode
 * param base PDM base pointer
 * param sourceClock_HZ PDM source clock frequency.
 * param sampleRate_HZ PDM sample rate.
 */
status_t PDM_SetSampleRateConfig(PDM_Type *base, uint32_t sourceClock_HZ, uint32_t sampleRate_HZ)
{
    uint32_t osr = (base->CTRL_2 & PDM_CTRL_2_CICOSR_MASK) >> PDM_CTRL_2_CICOSR_SHIFT;
    pdm_df_quality_mode_t qualityMode =
        (pdm_df_quality_mode_t)(uint32_t)((base->CTRL_2 & PDM_CTRL_2_QSEL_MASK) >> PDM_CTRL_2_QSEL_SHIFT);

    uint32_t pdmClockRate       = 0U;
//    uint32_t enabledChannelMask = base->CTRL_1 & (uint32_t)kPDM_EnableChannelAll;
    uint32_t regDiv = 0U;

    switch (qualityMode)
    {
        case kPDM_QualityModeHigh:
            osr          = 16U - osr;
            pdmClockRate = sampleRate_HZ * osr * 8U;
            break;

        case kPDM_QualityModeMedium:
            osr          = 16U - osr;
            pdmClockRate = sampleRate_HZ * osr * 4U;
            break;

        case kPDM_QualityModeLow:
            osr          = 16U - osr;
            pdmClockRate = sampleRate_HZ * osr * 2U;
            break;

        case kPDM_QualityModeVeryLow0:
            osr          = 16U - osr;
            pdmClockRate = sampleRate_HZ * osr * 4U;
            break;

        case kPDM_QualityModeVeryLow1:
            osr          = 16U - osr;
            pdmClockRate = sampleRate_HZ * osr * 2U;
            break;
        case kPDM_QualityModeVeryLow2:
            osr          = 16U - osr;
            pdmClockRate = sampleRate_HZ * osr;
            break;
        default:
            assert(false);
            break;
    }

    /* get divider */
    regDiv = sourceClock_HZ / pdmClockRate / 2;

    if (regDiv > PDM_CTRL_2_CLKDIV_MASK)
    {
        return kStatus_Fail;
    }

    base->CTRL_2 = (base->CTRL_2 & (~PDM_CTRL_2_CLKDIV_MASK)) | PDM_CTRL_2_CLKDIV(regDiv);

    return kStatus_Success;
}

/*!
 * brief PDM set sample rate.
 *
 * param base PDM base pointer
 * param enableChannelMask PDM channel enable mask.
 * param qualityMode quality mode.
 * param osr cic oversample rate
 * param clkDiv clock divider
 * after completing the current frame in debug mode.
 */
status_t PDM_SetSampleRate(
    PDM_Type *base, uint32_t enableChannelMask, pdm_df_quality_mode_t qualityMode, uint8_t osr, uint32_t clkDiv)
{
    uint32_t regDiv = clkDiv >> 1U;

    switch (qualityMode)
    {
        case kPDM_QualityModeHigh:
            regDiv <<= 1U;
            break;
        case kPDM_QualityModeLow:
        case kPDM_QualityModeVeryLow1:
            regDiv >>= 1U;
            break;
        case kPDM_QualityModeVeryLow2:
            regDiv >>= 2U;
            break;
        default:
            break;
    }

#if 0
    uint8_t realOsr = 16U - (osr & (PDM_CTRL_2_CICOSR_MASK >> PDM_CTRL_2_CICOSR_SHIFT));
    if (PDM_ValidateSrcClockRate(enableChannelMask, qualityMode, realOsr, regDiv) == kStatus_Fail)
    {
        return kStatus_Fail;
    }
#endif
    assert(regDiv <= PDM_CTRL_2_CLKDIV_MASK);
    base->CTRL_2 = (base->CTRL_2 & (~PDM_CTRL_2_CLKDIV_MASK)) | PDM_CTRL_2_CLKDIV(regDiv);

    return kStatus_Success;
}

/*!
 * brief Initializes the PDM peripheral.
 *
 * Ungates the PDM clock, resets the module, and configures PDM with a configuration structure.
 * The configuration structure can be custom filled or set with default values by
 * PDM_GetDefaultConfig().
 *
 * note  This API should be called at the beginning of the application to use
 * the PDM driver. Otherwise, accessing the PDM module can cause a hard fault
 * because the clock is not enabled.
 *
 * param base PDM base pointer
 * param config PDM configuration structure.
*/
void PDM_Init(PDM_Type *base, const pdm_config_t *config)
{
    assert(config != NULL);
    assert(config->fifoWatermark <= PDM_FIFO_CTRL_FIFOWMK_MASK);

#if !(defined(FSL_SDK_DISABLE_DRIVER_CLOCK_CONTROL) && FSL_SDK_DISABLE_DRIVER_CLOCK_CONTROL)
    /* Enable the PDM clock */
    CLOCK_EnableClock(s_pdmClock[PDM_GetInstance(base)]);
#endif /* FSL_SDK_DISABLE_DRIVER_CLOCK_CONTROL */

    /* Enable the module and disable the interface/all channel */
    base->CTRL_1 &= ~(PDM_CTRL_1_MDIS_MASK | PDM_CTRL_1_PDMIEN_MASK | PDM_CTRL_1_ERREN_MASK | kPDM_EnableChannelAll);

    /* wait all filter stopped */
    while (base->STAT & PDM_STAT_BSY_FIL_MASK)
    {
    }

    /* Set the configure settings */
    base->CTRL_1 = (base->CTRL_1 & (~PDM_CTRL_1_DOZEN_MASK)) | PDM_CTRL_1_DOZEN(config->enableDoze);

    base->CTRL_2 = (base->CTRL_2 & (~(PDM_CTRL_2_CICOSR_MASK | PDM_CTRL_2_QSEL_MASK))) |
                   PDM_CTRL_2_CICOSR(config->cicOverSampleRate) | PDM_CTRL_2_QSEL(config->qualityMode);

    /* Set the watermark */
    base->FIFO_CTRL = PDM_FIFO_CTRL_FIFOWMK(config->fifoWatermark);

    /* software reset */
    base->CTRL_1 |= PDM_CTRL_1_SRES_MASK;

}

/*!
 * brief De-initializes the PDM peripheral.
 *
 * This API gates the PDM clock. The PDM module can't operate unless PDM_Init
 * is called to enable the clock.
 *
 * param base PDM base pointer
*/
void PDM_Deinit(PDM_Type *base)
{
    /* disable PDM interface */
    PDM_Enable(base, false);

#if !(defined(FSL_SDK_DISABLE_DRIVER_CLOCK_CONTROL) && FSL_SDK_DISABLE_DRIVER_CLOCK_CONTROL)
    CLOCK_DisableClock(s_pdmClock[PDM_GetInstance(base)]);
#endif /* FSL_SDK_DISABLE_DRIVER_CLOCK_CONTROL */
}

/*!
 * brief Enables the PDM interrupt requests.
 *
 * param base PDM base pointer
 * param mask interrupt source
 *     The parameter can be a combination of the following sources if defined.
 *     arg kPDM_ErrorInterruptEnable
 *     arg kPDM_FIFOInterruptEnable
 */
void PDM_EnableInterrupts(PDM_Type *base, uint32_t mask)
{
    if (mask & kPDM_FIFOInterruptEnable)
    {
        base->CTRL_1 = (base->CTRL_1 & (~PDM_CTRL_1_DISEL_MASK)) | kPDM_FIFOInterruptEnable;
    }
    if (mask & kPDM_ErrorInterruptEnable)
    {
        base->CTRL_1 = (base->CTRL_1 & (~PDM_CTRL_1_ERREN_MASK)) | kPDM_ErrorInterruptEnable;
    }
}

/*!
 * brief PDM one channel configurations.
 *
 * param base PDM base pointer
 * param config PDM channel configurations.
 * param channel channel number.
 * after completing the current frame in debug mode.
 */
void PDM_SetChannelConfig(PDM_Type *base, uint32_t channel, const pdm_channel_config_t *config)
{
    assert(config != NULL);
    assert(channel <= FSL_FEATURE_PDM_CHANNEL_NUM);

    uint32_t dcCtrl = base->DC_CTRL;
    uint32_t outCtrl = base->OUT_CTRL;

    /* configure gain and cut off freq */
    dcCtrl &= ~(PDM_DC_CTRL_DCCONFIG0_MASK << (channel << 1U));
    dcCtrl |= (uint32_t)config->cutOffFreq << (channel << 1U);
    outCtrl &= ~((uint32_t)PDM_OUT_CTRL_OUTGAIN0_MASK << (channel << 2U));
    outCtrl |= (uint32_t)config->gain << (channel << 2U);

    base->DC_CTRL = dcCtrl;
    base->OUT_CTRL = outCtrl;

    /* enable channel */
    base->CTRL_1 |= 1U << channel;
}

#if !(defined(FSL_SDK_DISABLE_IRQ) && FSL_SDK_DISABLE_IRQ)
/*!
 * brief Initializes the PDM handle.
 *
 * This function initializes the handle for the PDM transactional APIs. Call
 * this function once to get the handle initialized.
 *
 * param base PDM base pointer.
 * param handle PDM handle pointer.
 * param callback Pointer to the user callback function.
 * param userData User parameter passed to the callback function.
 */
void PDM_TransferCreateHandle(PDM_Type *base, pdm_handle_t *handle, pdm_transfer_callback_t callback, void *userData)
{
    assert(handle);

    uint32_t val = 0, i = 0;

    /* Zero the handle */
    memset(handle, 0, sizeof(*handle));

    s_pdmHandle[PDM_GetInstance(base)] = handle;

    handle->callback = callback;
    handle->userData = userData;
    handle->watermark = base->FIFO_CTRL & PDM_FIFO_CTRL_FIFOWMK_MASK;

    /* Compute enabled channel number */
    val = base->CTRL_1 & 0xFFU;
    for (i = 0U; i < FSL_FEATURE_PDM_CHANNEL_NUM; i++)
    {
        if (val & ((uint32_t)1 << i))
        {
            handle->startChannel = i;
            handle->channelNums++;
        }
    }
    /* get start channel number, current value in startchannel is end channel number actually */
    handle->startChannel = handle->startChannel - (handle->channelNums - 1U);

    /* Set the isr pointer */
    s_pdmIsr = PDM_TransferHandleIRQ;

    /* Enable RX event IRQ */
    EnableIRQ(PDM_EVENT_IRQn);
    /* Enable FIFO error IRQ */
    EnableIRQ(PDM_ERROR_IRQn);
}
#endif
/*!
 * brief Performs an interrupt non-blocking receive transfer on PDM.
 *
 * note This API returns immediately after the transfer initiates.
 * Call the PDM_RxGetTransferStatusIRQ to poll the transfer status and check whether
 * the transfer is finished. If the return status is not kStatus_PDM_Busy, the transfer
 * is finished.
 *
 * param base PDM base pointer
 * param handle Pointer to the pdm_handle_t structure which stores the transfer state.
 * param xfer Pointer to the pdm_transfer_t structure.
 * retval kStatus_Success Successfully started the data receive.
 * retval kStatus_PDM_Busy Previous receive still not finished.
 */
status_t PDM_TransferReceiveNonBlocking(PDM_Type *base, pdm_handle_t *handle, pdm_transfer_t *xfer)
{
    assert(handle);
#if !(defined(FSL_SDK_DISABLE_IRQ) && FSL_SDK_DISABLE_IRQ)
    /* Check if the queue is full */
    if (handle->pdmQueue[handle->queueUser].data)
    {
        return kStatus_PDM_QueueFull;
    }

    /* Add into queue */
    handle->transferSize[handle->queueUser] = xfer->dataSize;
    handle->pdmQueue[handle->queueUser].data = xfer->data;
    handle->pdmQueue[handle->queueUser].dataSize = xfer->dataSize;
    handle->queueUser = (handle->queueUser + 1) % PDM_XFER_QUEUE_SIZE;

    /* Set state to busy */
    handle->state = kStatus_PDM_Busy;
#endif

    /* Enable interrupt */
    PDM_EnableInterrupts(base, kPDM_FIFOInterruptEnable);
    PDM_Enable(base, true);

    return kStatus_Success;
}

/*!
 * brief Aborts the current IRQ receive.
 *
 * note This API can be called when an interrupt non-blocking transfer initiates
 * to abort the transfer early.
 *
 * param base PDM base pointer
 * param handle Pointer to the pdm_handle_t structure which stores the transfer state.
 */
void PDM_TransferAbortReceive(PDM_Type *base, pdm_handle_t *handle)
{
    assert(handle);

    /* Use FIFO request interrupt and fifo error */
    PDM_DisableInterrupts(base, kPDM_FIFOInterruptEnable | kPDM_ErrorInterruptEnable);
    PDM_Enable(base, false);
    handle->state = kStatus_PDM_Idle;
    /* Clear the queue */
#if !(defined(FSL_SDK_DISABLE_IRQ) && FSL_SDK_DISABLE_IRQ)
    memset(handle->pdmQueue, 0, sizeof(pdm_transfer_t) * PDM_XFER_QUEUE_SIZE);
    handle->queueDriver = 0;
    handle->queueUser = 0;
#endif
}

#if !(defined(FSL_SDK_DISABLE_IRQ) && FSL_SDK_DISABLE_IRQ)
/*!
 * brief Tx interrupt handler.
 *
 * param base PDM base pointer.
 * param handle Pointer to the pdm_handle_t structure.
 */
void PDM_TransferHandleIRQ(PDM_Type *base, pdm_handle_t *handle)
{
    assert(handle);

    int16_t *buffer = (int16_t *)handle->pdmQueue[handle->queueDriver].data;

    /* Handle transfer */
    if ((base->STAT & 0xFFU) && (handle->channelNums) &&
        ((base->CTRL_1 & PDM_CTRL_1_DISEL_MASK) == (0x2U << PDM_CTRL_1_DISEL_SHIFT)))
    {
        PDM_ClearStatus(base, 0xFFU);
        /* Judge if the data need to transmit is less than space */
        uint8_t size =
            MIN((handle->pdmQueue[handle->queueDriver].dataSize), (handle->watermark * handle->channelNums * 2U));

        PDM_ReadNonBlocking(base, handle->startChannel, handle->channelNums, buffer,
                            (size / handle->channelNums) >> 1U);
        /* Update the internal counter */
        handle->pdmQueue[handle->queueDriver].dataSize -= size;
        handle->pdmQueue[handle->queueDriver].data += size;
    }

    /* If finished a block, call the callback function */
    if (handle->pdmQueue[handle->queueDriver].dataSize == 0U)
    {
        handle->pdmQueue[handle->queueDriver].data = NULL;
        handle->queueDriver = (handle->queueDriver + 1) % PDM_XFER_QUEUE_SIZE;
        if (handle->callback)
        {
            (handle->callback)(base, handle, kStatus_PDM_Idle, handle->userData);
        }
    }

    /* If all data finished, just stop the transfer */
    if (handle->pdmQueue[handle->queueDriver].data == NULL)
    {
        PDM_TransferAbortReceive(base, handle);
    }
}
#endif
/*!
 * brief Configure voice activity detector.
 *
 * param base PDM base pointer
 * param config Voice activity detector configure structure pointer .
 */
void PDM_SetHwvadConfig(PDM_Type *base, const pdm_hwvad_config_t *config)
{
    assert(config != NULL);

    uint32_t ctrl1 = base->VAD0_CTRL_1;

    /* Configure VAD0_CTRL_1 register */
    ctrl1 &= ~(PDM_VAD0_CTRL_1_VADCHSEL_MASK | PDM_VAD0_CTRL_1_VADCICOSR_MASK | PDM_VAD0_CTRL_1_VADINITT_MASK);
    ctrl1 |= (PDM_VAD0_CTRL_1_VADCHSEL(config->channel) | PDM_VAD0_CTRL_1_VADCICOSR(config->cicOverSampleRate) |
              PDM_VAD0_CTRL_1_VADINITT(config->initializeTime));
    base->VAD0_CTRL_1 = ctrl1;

    /* Configure VAD0_CTRL_2 register */
    base->VAD0_CTRL_2 =
        (PDM_VAD0_CTRL_2_VADFRENDIS((config->enableFrameEnergy == true) ? 0U : 1U) |
         PDM_VAD0_CTRL_2_VADPREFEN(config->enablePreFilter) | PDM_VAD0_CTRL_2_VADFRAMET(config->frameTime) |
         PDM_VAD0_CTRL_2_VADINPGAIN(config->inputGain) | PDM_VAD0_CTRL_2_VADHPF(config->cutOffFreq));
}

/*!
 * brief Configure voice activity detector signal filter.
 *
 * param base PDM base pointer
 * param enableMaxBlock If signal maximum block enabled.
 * param signalGain Gain value for the signal energy.
 */
void PDM_SetHwvadSignalFilterConfig(PDM_Type *base, bool enableMaxBlock, uint32_t signalGain)
{
    uint32_t signalConfig = base->VAD0_SCONFIG;

    signalConfig &= ~(PDM_VAD0_SCONFIG_VADSMAXEN_MASK | PDM_VAD0_SCONFIG_VADSGAIN_MASK);
    signalConfig |= (PDM_VAD0_SCONFIG_VADSMAXEN(enableMaxBlock) | PDM_VAD0_SCONFIG_VADSGAIN(signalGain)) |
                    PDM_VAD0_SCONFIG_VADSFILEN_MASK;
    base->VAD0_SCONFIG = signalConfig;
}

/*!
 * brief Configure voice activity detector noise filter.
 *
 * param base PDM base pointer
 * param config Voice activity detector noise filter configure structure pointer .
 */
void PDM_SetHwvadNoiseFilterConfig(PDM_Type *base, const pdm_hwvad_noise_filter_t *config)
{
    assert(config != NULL);

    base->VAD0_NCONFIG =
        (PDM_VAD0_NCONFIG_VADNFILAUTO(config->enableAutoNoiseFilter) |
         PDM_VAD0_NCONFIG_VADNOREN(config->enableNoiseDetectOR) | PDM_VAD0_NCONFIG_VADNMINEN(config->enableNoiseMin) |
         PDM_VAD0_NCONFIG_VADNDECEN(config->enableNoiseDecimation) |
         PDM_VAD0_NCONFIG_VADNFILADJ(config->noiseFilterAdjustment) | PDM_VAD0_NCONFIG_VADNGAIN(config->noiseGain));
}

/*!
 * brief Configure voice activity detector zero cross detector.
 *
 * param base PDM base pointer
 * param config Voice activity detector zero cross detector configure structure pointer .
 */
void PDM_SetHwvadZeroCrossDetectorConfig(PDM_Type *base, const pdm_hwvad_zero_cross_detector_t *config)
{
    assert(config != NULL);

    uint32_t zcd = (base->VAD0_ZCD & (~(PDM_VAD0_ZCD_VADZCDTH_MASK | PDM_VAD0_ZCD_VADZCDADJ_MASK |
                                        PDM_VAD0_ZCD_VADZCDAUTO_MASK | PDM_VAD0_ZCD_VADZCDAND_MASK)));

    zcd |= (PDM_VAD0_ZCD_VADZCDTH(config->threshold) | PDM_VAD0_ZCD_VADZCDADJ(config->adjustmentThreshold) |
            PDM_VAD0_ZCD_VADZCDAUTO(config->enableAutoThreshold) | PDM_VAD0_ZCD_VADZCDAND(config->zcdAnd)) |
           PDM_VAD0_ZCD_VADZCDEN_MASK;

    base->VAD0_ZCD = zcd;
}

#if !(defined(FSL_SDK_DISABLE_IRQ) && FSL_SDK_DISABLE_IRQ)
#if defined(PDM)
void PDM_EVENT_DriverIRQHandler(void)
{
    assert(s_pdmHandle[0]);
    s_pdmIsr(PDM, s_pdmHandle[0]);
/* Add for ARM errata 838869, affects Cortex-M4, Cortex-M4F Store immediate overlapping
  exception return operation might vector to incorrect interrupt */
#if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
#endif
}
#endif
#endif

/*!
 * @brief Dumps PDM register.
 *
 * @param base PDM base pointer.
 */
void PDM_Dump(PDM_Type *base)
{
    printf("PDM base address: %p\n", base);
    printf("\tCTRL_1: %#08x\n", base->CTRL_1);
    printf("\tCTRL_2: %#08x\n", base->CTRL_2);
    printf("\tFIFO_CTRL: %#08x\n", base->FIFO_CTRL);
    printf("\t:DC_CTRL %#08x\n", base->DC_CTRL);
    printf("\t:OUT_CTRL %#08x\n", base->OUT_CTRL);
}
