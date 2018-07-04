/**
 *******************************************************************************
 * @file    : tcd1304.c
 * @author  : Dung Do Dang
 * @version : V1.0.0
 * @date    : 2018-06-21
 * @brief   : Driver API for the TCD1304 CCD sensor chip from Toshiba
 *
 * The software module is split into two part.
 * 1) Generic API to implement the necessary functionality to control the sensor
 * 2) Portable layer to configure and control the hardware platform the sensor
 * is connected to. In this way only (2) is needed to be changed if the hardware
 * platform is needed to be replaced.
 *
 *******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2018 Dung Do Dang
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************
 */

/**
 ***************************** Revision History ********************************
 * revision 0:
 *
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "tcd1304.h"

/* Private typedef -----------------------------------------------------------*/
typedef struct
{
    TCD_DATA_t data;
    uint8_t readyToRun;
    volatile uint8_t dataReady;
    volatile uint32_t counter;
    uint64_t totalSpectrumsAcquired;
} TCD_PCB_t;

/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static TCD_CONFIG_t *TCD_config;
static TCD_PCB_t TCD_pcb;

/* Private function prototypes -----------------------------------------------*/
static TCD_ERR_t TCD_FM_Init(void);
static TCD_ERR_t TCD_ICG_Init(void);
static TCD_ERR_t TCD_SH_Init(void);
static TCD_ERR_t TCD_ADC_Init(void);

/* External functions --------------------------------------------------------*/

/**
 *******************************************************************************
 *                        PUBLIC IMPLEMENTATION SECTION
 *******************************************************************************
 */

/*******************************************************************************
 * @Brief   Init MCU timers and ADC + DMA to start acquisition of sensor data
 * @param   config, TCD_CONFIG_t: Struct holding configuration for the TCD1304.
 * @retval  TCD_OK on success or TCD_ERR_t error codes.
 *
 ******************************************************************************/
TCD_ERR_t TCD_Init(TCD_CONFIG_t *config)
{
    TCD_ERR_t err;

    if ( config == NULL )
    {
        return TCD_ERR_NULL_POINTER;
    }
    else
    {
        TCD_config = config;
    }

    /* Configure and start the ADC + DMA */
    err = TCD_ADC_Init();
    if ( err != TCD_OK )
    {
        return err;
    }

    /* Configure the master clock timer */
    err = TCD_FM_Init();
    if ( err != TCD_OK )
    {
        return err;
    }

    /* Configure the ICG pulse timer */
    err = TCD_ICG_Init();
    if ( err != TCD_OK )
    {
        return err;
    }

    /* Configure the electronic shutter timer */
    err = TCD_SH_Init();
    if ( err != TCD_OK )
    {
        return err;
    }

    /* Set the process controll block to initial values */
    TCD_pcb.counter = 0U;
    TCD_pcb.totalSpectrumsAcquired = 0U;
    TCD_pcb.readyToRun = 1U;
    TCD_pcb.dataReady = 0U;

    return err;
}

/*******************************************************************************
 * @brief   Start the timers and data acquisition with ADC+DMA
 * @param   None
 * @retval  TCD_OK on success or TCD_ERR_t code
 *
 ******************************************************************************/
TCD_ERR_t TCD_Start(void)
{
    if ( TCD_pcb.readyToRun == 1U )
    {
        /* Start to generate ICG and SH pulses */
        TCD_PORT_Run();
        return TCD_OK;
    }
    else
    {
        return TCD_ERR_NOT_INITIALIZED;
    }
}

/*******************************************************************************
 * @brief   Stop the timers to stop the data acquisition
 * @param   None
 * @retval  TCD_OK on success or TCD_ERR_t code
 *
 ******************************************************************************/
TCD_ERR_t TCD_Stop(void)
{
    if ( TCD_pcb.readyToRun == 1U )
    {
        /* Stop to generate ICG and SH pulses */
        TCD_PORT_Stop();
        return TCD_OK;
    }
    else
    {
        return TCD_ERR_NOT_INITIALIZED;
    }
}

/*******************************************************************************
 * @Brief   Set electronic shutter period in microseconds
 * @param   config, TCD_CONFIG_t: Struct holding configuration for the TCD1304.
 * @retval  TCD_OK on success or TCD_ERR_t error codes.
 *
 * Make sure that the ICG and SH pulses overlap.
 ******************************************************************************/
TCD_ERR_t TCD_SetIntTime(TCD_CONFIG_t *config)
{
    if ( TCD_pcb.readyToRun == 1U )
    {
        TCD_PORT_Stop();

        /* Find the first valid integration time */
        uint32_t remainder;
        uint32_t t_int_us = config->t_int_us;

        do
        {
            remainder = config->t_icg_us % t_int_us;
            t_int_us++;
        }
        while ( (remainder != 0U) && (t_int_us <= config->t_icg_us) );

        if ( config->t_int_us <= config->t_icg_us )
        {
            config->t_int_us = t_int_us - 1U;

            TCD_SH_Init();
            TCD_PORT_Run();

            return TCD_OK;
        }
        else
        {
            return TCD_ERR_PARAM_OUT_OF_RANGE;
        }
    }
    else
    {
        return TCD_ERR_NOT_INITIALIZED;
    }
}

/*******************************************************************************
 * @brief   Handle sensor data when the ADC+DMA has samples all pixels.
 * @param   None
 * @retval  None
 *
 * This function is called from the ADC DMA transfer complete interrupt handler.
 * The DMA is configured to circular (ring buffer) mode. The interrupt request
 * flag is generated just before the tranfer counter is re-set to the programmed
 * value.
 *
 * NOTE: This function is called from the portable layer in interrupt context.
 ******************************************************************************/
void TCD_ReadCompletedCallback(void)
{
    TCD_pcb.totalSpectrumsAcquired++;
    TCD_pcb.counter++;

    /* Accumulate the spectrum data vector */
    for ( uint32_t i = 0U; i < CFG_CCD_NUM_PIXELS; i++ )
    {
        TCD_pcb.data.SensorDataAccu[ i ] += TCD_pcb.data.SensorData[ i ];
    }

    /* Calculate average data vector */
    if ( TCD_pcb.counter == TCD_config->avg )
    {
        for ( uint32_t i = 0U; i < CFG_CCD_NUM_PIXELS; i++ )
        {
            TCD_pcb.data.SensorDataAvg[ i ] = (uint16_t) (TCD_pcb.data.SensorDataAccu[ i ] / TCD_config->avg);
            TCD_pcb.data.SensorDataAccu[ i ] = 0U;
        }

        TCD_pcb.counter = 0U;
        TCD_pcb.dataReady = 1U;
    }
}

/*******************************************************************************
 * @brief   Get the Sensor data structure
 * @param   None
 * @retval  Pointer to the local TCD_DATA_t in RAM
 *
 * The user can access raw data or avereaged data in the structure.
 ******************************************************************************/
TCD_DATA_t* TCD_GetSensorData(void)
{
    return &TCD_pcb.data;
}

/*******************************************************************************
 * @brief   Check if new data is ready
 * @param   None
 * @retval  1U on ready and 0U on not ready
 *
 ******************************************************************************/
uint8_t TCD_IsDataReady(void)
{
    return TCD_pcb.dataReady;
}

/*******************************************************************************
 * @brief   Clear the data ready flag
 * @param   None
 * @retval  None
 *
 ******************************************************************************/
void TCD_ClearDataReadyFlag(void)
{
    TCD_pcb.dataReady = 0U;
}

/*******************************************************************************
 * @brief   Get the total of spectrum collected since the start
 * @param   None
 * @retval  None
 *
 ******************************************************************************/
uint64_t TCD_GetNumOfSpectrumsAcquired(void)
{
    return TCD_pcb.totalSpectrumsAcquired;
}

/**
 *******************************************************************************
 *                        PRIVATE IMPLEMENTATION SECTION
 *******************************************************************************
 */

/*******************************************************************************
 * @Brief   Generate the Master Clock for the CCD sensor
 * @param   None
 * @retval  None
 * Check that the master clock is within the limits of the sensor; 0.8 - 4 MHz.
 ******************************************************************************/
static TCD_ERR_t TCD_FM_Init(void)
{
    TCD_ERR_t err = TCD_OK;

    if ( (TCD_config->f_master <= 4000000U) && (TCD_config->f_master >= 800000U) )
    {
        TCD_PORT_FM_ConfigClock( TCD_config->f_master );
    }
    else
    {
        TCD_PORT_FM_ConfigClock( CFG_FM_FREQUENCY_HZ );
        err = TCD_WARN_FM;
    }

    return err;
}

/*******************************************************************************
 * @Brief   Generate the ICG pulses
 * @param   None
 * @retval  None
 *
 ******************************************************************************/
static TCD_ERR_t TCD_ICG_Init(void)
{
    TCD_ERR_t err = TCD_OK;

    if ( (TCD_config->t_icg_us > 0U) && (TCD_config->t_icg_us <= CFG_ICG_MAX_PERIOD_US) )
    {
        TCD_PORT_ICG_ConfigClock( TCD_config->t_icg_us );
    }
    else
    {
        TCD_PORT_ICG_ConfigClock( CFG_ICG_DEFAULT_PERIOD_US );
        err = TCD_WARN_ICG;
    }

    return err;
}

/*******************************************************************************
 * @Brief   Generate the electronic shutter (SH) pulses
 * @param   None
 * @retval  None
 *
 * Check that the SH period is within the limits. 10 microseconds to ICG period.
 * In addition the SH and the ICG pulses MUST overlap. This is fullfilled if
 * this relationship is held:
 * P_ICG = N x P_SH, where N is an integer.
 ******************************************************************************/
static TCD_ERR_t TCD_SH_Init(void)
{
    TCD_ERR_t err = TCD_OK;

    /* Check that P_ICG = N x P_SH, where N is an integer */
    if ( TCD_config->t_icg_us % TCD_config->t_int_us )
    {
        return TCD_ERR_SH_INIT;
    }

    if ( (TCD_config->t_int_us >= 10U) && (TCD_config->t_int_us <= TCD_config->t_icg_us) )
    {
        TCD_PORT_SH_ConfigClock( TCD_config->t_int_us );
    }
    else
    {
        TCD_PORT_SH_ConfigClock( CFG_SH_DEFAULT_PERIOD_US );
        err = TCD_WARN_SH;
    }

    return err;
}

/*******************************************************************************
 * @Brief   Generate the ADC trigger signal
 * @param   None
 * @retval  None
 ******************************************************************************/
static TCD_ERR_t TCD_ADC_Init(void)
{
    /* Initialize the ADC hardware and DMA */
    if ( TCD_PORT_ADC_Init() != 0 )
    {
        return TCD_ERR_ADC_INIT;
    }

    /* Check that the master clock is dividable by 4 */
    if ( TCD_config->f_master % 4U )
    {
        return TCD_ERR_ADC_INIT;
    }
    /* Initialize the timer used to trigger AD conversion with f_ADC = f_MCLK / 4 */
    TCD_PORT_ADC_ConfigTrigger( TCD_config->f_master / 4U );

    /* Start the DMA transfer */
    if ( TCD_PORT_ADC_Start( TCD_pcb.data.SensorData ) == 0 )
    {
        return TCD_OK;
    }
    else
    {
        return TCD_ERR_ADC_NOT_STARTED;
    }
    /**
     * Start the DMA to move data from ADC to RAM.
     * From now on the AD conversion is controlled by hardware.
     */
}

/****************************** END OF FILE ***********************************/
