/**
 *******************************************************************************
 * @file    : main.c
 * @author  : Dung Do Dang
 * @version : V1.0.0
 * @date    : 2018-06-21
 * @brief   : Main program entry for the CCD sensor chip from Toshiba
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

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cli.h"
#include "stm32f7xx_hal.h"
#include "tcd1304.h"
#include "string.h"

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
volatile uint8_t requestToSendFlag = 0;
const char HEADER[] =
"--------------------------------------\r\n"
"          STM32F746 Discovery         \r\n"
"          TCD1304 Linear CCD          \r\n"
"                                      \r\n"
"          2018 Dung Do Dang           \r\n"
"--------------------------------------\r\n";

/*******************************************************************************
 *                      TCD1304 SENSOR CONFIGURATION
 *******************************************************************************
 *
 * Configuration for the TCD1304 CCD sensor.
 * Readout period must be dividable by integration time, i.e.
 * t_icg_us = N x t_int_us, where N is an integer.
 *
 * The sensor data readout time is determined by the data clock frequency and
 * the number of pixels that must be output.
 * At 4 MHz master clock it takes 3694 x (4 / 4 MHz) = 3.694 ms
 * The ICG period (readout period) is therefore set to 3.8 ms, given 100 us
 * time for the ICG pulse and interrupt reaction time. This is plenty of time.
 *
 * In high light levels, the integration time is needed to be set low to avoid
 * over-exposure of the CCD photodiodes.
 *
 * This firmware is collecting all available sensor data at the maximum readout
 * frequency. The data from each readout is accumulated in
 * TCD_ReadCompletedCallback() in tcd1304.c
 *
 * At every avg x t_icg_us = 1.52 seconds averaged data is available for readout.
 *
 ******************************************************************************/
TCD_CONFIG_t sensor_config =
{
    .avg = 400,             /* Averaging:        400    */
    .f_master = 4000000,    /* Master clock:     4 MHz  */
    .t_icg_us = 3800,       /* Readout period:   3.8 ms */
    .t_int_us = 10,         /* Integration time: 10 us  */
};

/* Private function prototypes -----------------------------------------------*/
static void SystemClock_Config(void);
static void MX_USART1_UART_Init(void);
static void MCU_Init(void);

int main(void)
{
    /* Initialize the MCU and all configured peripherals */
    MCU_Init();

    /* Display some welcome text to the user */
    HAL_UART_Transmit( &huart1, (uint8_t *) HEADER, strlen(HEADER), 1000U );

    /* Initialize the command line interface (CLI) */
    if ( CLI_Init( &huart1 ) != CLI_OK )
    {
        _Error_Handler( __FILE__, __LINE__ );
    }

    /* Initialize and start the TCD1304 CCD sensor */
    if ( TCD_Init( &sensor_config ) != TCD_OK )
    {
        _Error_Handler( __FILE__, __LINE__ );
    }
    if ( TCD_Start() != TCD_OK )
    {
        _Error_Handler( __FILE__, __LINE__ );
    }

    while ( 1 )
    {
        if ( (TCD_IsDataReady() == 1U) && (requestToSendFlag == 1U) )
        {
            /* Clear the flags */
            TCD_ClearDataReadyFlag();
            requestToSendFlag = 0U;

            TCD_DATA_t *data = TCD_GetSensorData();
            HAL_UART_Transmit_DMA( &huart1, (uint8_t *) data->SensorDataAvg, 2U * CFG_CCD_NUM_PIXELS );
        }

        CLI_CheckInputBuffer();
    }
}

/**
 * @brief   Initialize the MCU system
 * @retval  None
 */
static void MCU_Init(void)
{
    /* Enable I-Cache-------------------------------------------------------------*/
    SCB_EnableICache();

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* Configure the system clock */
    SystemClock_Config();

    /* Initialize all configured peripherals */
    MX_USART1_UART_Init();
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{

    RCC_OscInitTypeDef RCC_OscInitStruct;
    RCC_ClkInitTypeDef RCC_ClkInitStruct;
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct;

    /* Configure the main internal regulator output voltage */
    __HAL_RCC_PWR_CLK_ENABLE();

    __HAL_PWR_VOLTAGESCALING_CONFIG( PWR_REGULATOR_VOLTAGE_SCALE1 );

    /* Initializes the CPU, AHB and APB busses clocks */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 12;
    RCC_OscInitStruct.PLL.PLLN = 216;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 2;
    if ( HAL_RCC_OscConfig( &RCC_OscInitStruct ) != HAL_OK )
    {
        _Error_Handler( __FILE__, __LINE__ );
    }

    /* Activate the Over-Drive mode */
    if ( HAL_PWREx_EnableOverDrive() != HAL_OK )
    {
        _Error_Handler( __FILE__, __LINE__ );
    }

    /* Initializes the CPU, AHB and APB busses clocks */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
            | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if ( HAL_RCC_ClockConfig( &RCC_ClkInitStruct, FLASH_LATENCY_7 ) != HAL_OK )
    {
        _Error_Handler( __FILE__, __LINE__ );
    }

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    PeriphClkInitStruct.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
    if ( HAL_RCCEx_PeriphCLKConfig( &PeriphClkInitStruct ) != HAL_OK )
    {
        _Error_Handler( __FILE__, __LINE__ );
    }

    /* Configure the Systick interrupt time */
    HAL_SYSTICK_Config( HAL_RCC_GetHCLKFreq() / 1000 );

    /* Configure the Systick */
    HAL_SYSTICK_CLKSourceConfig( SYSTICK_CLKSOURCE_HCLK );

    /* SysTick_IRQn interrupt configuration */
    HAL_NVIC_SetPriority( SysTick_IRQn, 0, 0 );
}

/* USART1 init function */
static void MX_USART1_UART_Init(void)
{

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if ( HAL_UART_Init( &huart1 ) != HAL_OK )
    {
        _Error_Handler( __FILE__, __LINE__ );
    }

}

/**
 * @brief  This function is executed in case of error occurrence.
 * @param  file: The file name as string.
 * @param  line: The line in file as a number.
 * @retval None
 */
void _Error_Handler(char *file, int line)
{
    while ( 1 )
    {
        __BKPT( 0 );
    }
}

#ifdef  USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t* file, uint32_t line)
{
    /**
     * User can add his own implementation to report the file name and line number,
     * tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) 
     */
}
#endif /* USE_FULL_ASSERT */

/**
 * @}
 */

/**
 * @}
 */

/****************************** END OF FILE ***********************************/
