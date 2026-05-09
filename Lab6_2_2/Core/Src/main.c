/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdio.h>

/* Private variables ---------------------------------------------------------*/
DFSDM_Channel_HandleTypeDef hdfsdm1_channel1;

I2C_HandleTypeDef hi2c2;

QSPI_HandleTypeDef hqspi;

SPI_HandleTypeDef hspi3;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* USER CODE BEGIN PV */
TIM_HandleTypeDef htim1;
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

#define SAMPLE_BUFFER_SIZE  256

uint16_t sample_buffer[SAMPLE_BUFFER_SIZE];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DFSDM1_Init(void);
static void MX_I2C2_Init(void);
static void MX_QUADSPI_Init(void);
static void MX_SPI3_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USB_OTG_FS_PCD_Init(void);

/* USER CODE BEGIN PFP */
void ADC1_Init(void);
void ADC1_DMA1CH1_init(void);
static void TIM1_Init(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

/* ADC MSP Init */
//void HAL_ADC_MspInit(ADC_HandleTypeDef* hadc)
//{
//    if (hadc->Instance == ADC1)
//    {
//        __HAL_RCC_ADC_CLK_ENABLE();
//    }
//}
//
///* ADC MSP DeInit */
//void HAL_ADC_MspDeInit(ADC_HandleTypeDef* hadc)
//{
//    if (hadc->Instance == ADC1)
//    {
//        __HAL_RCC_ADC_CLK_DISABLE();
//    }
//}

/* ADC1 Init: TIM1 TRGO trigger + internal temperature sensor */
void ADC1_Init(void)
{
    ADC_MultiModeTypeDef multimode = {0};
    ADC_ChannelConfTypeDef sConfig = {0};

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;

    /* ADC triggered by TIM1 TRGO */
    hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T1_TRGO;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;

    /* IMPORTANT: keep DMA requests enabled */
    hadc1.Init.DMAContinuousRequests = ENABLE;

    hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
    hadc1.Init.OversamplingMode = DISABLE;

    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
    {
        Error_Handler();
    }

    multimode.Mode = ADC_MODE_INDEPENDENT;

    if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
    {
        Error_Handler();
    }

    /* Internal temperature sensor channel */
    sConfig.Channel = ADC_CHANNEL_TEMPSENSOR;
    sConfig.Rank = ADC_REGULAR_RANK_1;

    /*
     * Internal temperature sensor needs longer sampling time.
     * 2.5 cycles is usually too short.
     */
    sConfig.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;

    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }
}

/* Called when bottom half of buffer is filled */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        printf("\r\n--- [Bottom Half Ready] ---\r\n");

        for (int i = SAMPLE_BUFFER_SIZE / 2; i < SAMPLE_BUFFER_SIZE; ++i)
        {
            printf("%d ", sample_buffer[i]);
        }

        printf("\r\n");
    }
}

/* Called when top half of buffer is filled */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        printf("\r\n--- [Top Half Ready] ---\r\n");

        for (int i = 0; i < SAMPLE_BUFFER_SIZE / 2; ++i)
        {
            printf("%d ", sample_buffer[i]);
        }

        printf("\r\n");
    }
}

/* DMA interrupt handler */
void DMA1_Channel1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_adc1);
}

/* DMA1 Channel1 Init for ADC1 */
void ADC1_DMA1CH1_init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_adc1.Instance = DMA1_Channel1;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;

    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

    ADC1_Init();

    /*
     * If your startup file already points DMA1_Channel1_IRQn
     * to DMA1_Channel1_IRQHandler, this line is not necessary.
     * It is kept here because your original version used it.
     */
    NVIC_SetVector(DMA1_Channel1_IRQn, (uint32_t)&DMA1_Channel1_IRQHandler);

    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

/* TIM1 MSP Init */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* htim_base)
{
    if (htim_base->Instance == TIM1)
    {
        __HAL_RCC_TIM1_CLK_ENABLE();
    }
}

/* TIM1 MSP DeInit */
void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef* htim_base)
{
    if (htim_base->Instance == TIM1)
    {
        __HAL_RCC_TIM1_CLK_DISABLE();
    }
}

/* TIM1 Init: generates TRGO update event */
static void TIM1_Init(void)
{
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    htim1.Instance = TIM1;

    /*
     * System clock in this project is 80 MHz.
     * TIM1 clock = 80 MHz.
     * Sampling frequency = 80 MHz / (4000 * 1000) = 20 Hz.
     */
    htim1.Init.Prescaler = 4000 - 1;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = 1000 - 1;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
    {
        Error_Handler();
    }

    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;

    if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /*
     * TIM1 update event becomes TRGO,
     * and ADC1 is triggered by TIM1 TRGO.
     */
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;

    if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
    HAL_Init();

    SystemClock_Config();

    /*
     * For this homework, USART1 is needed for printf.
     * Other peripherals are not required for ADC + TIM + DMA.
     */
    MX_USART1_UART_Init();

    /* USER CODE BEGIN 2 */

    TIM1_Init();
    ADC1_DMA1CH1_init();

    /*
     * IMPORTANT:
     * Set ADC DMA circular mode before starting ADC DMA.
     * This is the register-level part needed to keep ADC DMA requests circular.
     */
    SET_BIT(hadc1.Instance->CFGR, ADC_CFGR_DMACFG);

    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&sample_buffer[0], SAMPLE_BUFFER_SIZE) != HAL_OK)
    {
        Error_Handler();
    }

    /*
     * Start TIM1 only.
     * No timer interrupt is needed.
     * TIM1 update event triggers ADC through TRGO.
     */
    if (HAL_TIM_Base_Start(&htim1) != HAL_OK)
    {
        Error_Handler();
    }

    printf("\r\nADC DMA timer trigger start.\r\n");

    /* USER CODE END 2 */

    while (1)
    {
    }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
    {
        Error_Handler();
    }

    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE | RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
    RCC_OscInitStruct.MSIState = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = 0;
    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 40;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
                                  RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 |
                                  RCC_CLOCKTYPE_PCLK2;

    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }

    HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief DFSDM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DFSDM1_Init(void)
{
    hdfsdm1_channel1.Instance = DFSDM1_Channel1;
    hdfsdm1_channel1.Init.OutputClock.Activation = ENABLE;
    hdfsdm1_channel1.Init.OutputClock.Selection = DFSDM_CHANNEL_OUTPUT_CLOCK_SYSTEM;
    hdfsdm1_channel1.Init.OutputClock.Divider = 2;
    hdfsdm1_channel1.Init.Input.Multiplexer = DFSDM_CHANNEL_EXTERNAL_INPUTS;
    hdfsdm1_channel1.Init.Input.DataPacking = DFSDM_CHANNEL_STANDARD_MODE;
    hdfsdm1_channel1.Init.Input.Pins = DFSDM_CHANNEL_FOLLOWING_CHANNEL_PINS;
    hdfsdm1_channel1.Init.SerialInterface.Type = DFSDM_CHANNEL_SPI_RISING;
    hdfsdm1_channel1.Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
    hdfsdm1_channel1.Init.Awd.FilterOrder = DFSDM_CHANNEL_FASTSINC_ORDER;
    hdfsdm1_channel1.Init.Awd.Oversampling = 1;
    hdfsdm1_channel1.Init.Offset = 0;
    hdfsdm1_channel1.Init.RightBitShift = 0x00;

    if (HAL_DFSDM_ChannelInit(&hdfsdm1_channel1) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{
    hi2c2.Instance = I2C2;
    hi2c2.Init.Timing = 0x00000E14;
    hi2c2.Init.OwnAddress1 = 0;
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2 = 0;
    hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c2) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief QUADSPI Initialization Function
  * @param None
  * @retval None
  */
static void MX_QUADSPI_Init(void)
{
    hqspi.Instance = QUADSPI;
    hqspi.Init.ClockPrescaler = 2;
    hqspi.Init.FifoThreshold = 4;
    hqspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
    hqspi.Init.FlashSize = 23;
    hqspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_1_CYCLE;
    hqspi.Init.ClockMode = QSPI_CLOCK_MODE_0;

    if (HAL_QSPI_Init(&hqspi) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief SPI3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI3_Init(void)
{
    hspi3.Instance = SPI3;
    hspi3.Init.Mode = SPI_MODE_MASTER;
    hspi3.Init.Direction = SPI_DIRECTION_2LINES;
    hspi3.Init.DataSize = SPI_DATASIZE_4BIT;
    hspi3.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi3.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi3.Init.NSS = SPI_NSS_SOFT;
    hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi3.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi3.Init.CRCPolynomial = 7;
    hspi3.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
    hspi3.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;

    if (HAL_SPI_Init(&hspi3) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
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

    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 115200;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1;
    huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart3) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief USB_OTG_FS Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_OTG_FS_PCD_Init(void)
{
    hpcd_USB_OTG_FS.Instance = USB_OTG_FS;
    hpcd_USB_OTG_FS.Init.dev_endpoints = 6;
    hpcd_USB_OTG_FS.Init.speed = PCD_SPEED_FULL;
    hpcd_USB_OTG_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
    hpcd_USB_OTG_FS.Init.Sof_enable = DISABLE;
    hpcd_USB_OTG_FS.Init.low_power_enable = DISABLE;
    hpcd_USB_OTG_FS.Init.lpm_enable = DISABLE;
    hpcd_USB_OTG_FS.Init.battery_charging_enable = DISABLE;
    hpcd_USB_OTG_FS.Init.use_dedicated_ep1 = DISABLE;
    hpcd_USB_OTG_FS.Init.vbus_sensing_enable = DISABLE;

    if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOE, M24SR64_Y_RF_DISABLE_Pin | M24SR64_Y_GPO_Pin | ISM43362_RST_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(GPIOA, ARD_D10_Pin | SPBTLE_RF_RST_Pin | ARD_D9_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(GPIOB, ARD_D8_Pin | ISM43362_BOOT0_Pin | ISM43362_WAKEUP_Pin | LED2_Pin
                          | SPSGRF_915_SDN_Pin | ARD_D5_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(GPIOD, USB_OTG_FS_PWR_EN_Pin | PMOD_RESET_Pin | STSAFE_A100_RESET_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(SPBTLE_RF_SPI3_CSN_GPIO_Port, SPBTLE_RF_SPI3_CSN_Pin, GPIO_PIN_SET);

    HAL_GPIO_WritePin(GPIOC, VL53L0X_XSHUT_Pin | LED3_WIFI__LED4_BLE_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(SPSGRF_915_SPI3_CSN_GPIO_Port, SPSGRF_915_SPI3_CSN_Pin, GPIO_PIN_SET);

    HAL_GPIO_WritePin(ISM43362_SPI3_CSN_GPIO_Port, ISM43362_SPI3_CSN_Pin, GPIO_PIN_SET);

    GPIO_InitStruct.Pin = M24SR64_Y_RF_DISABLE_Pin | M24SR64_Y_GPO_Pin | ISM43362_RST_Pin | ISM43362_SPI3_CSN_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = USB_OTG_FS_OVRCR_EXTI3_Pin | SPSGRF_915_GPIO3_EXTI5_Pin | SPBTLE_RF_IRQ_EXTI6_Pin | ISM43362_DRDY_EXTI1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = BUTTON_EXTI13_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BUTTON_EXTI13_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ARD_A5_Pin | ARD_A4_Pin | ARD_A3_Pin | ARD_A2_Pin
                         | ARD_A1_Pin | ARD_A0_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ARD_D1_Pin | ARD_D0_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ARD_D10_Pin | SPBTLE_RF_RST_Pin | ARD_D9_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ARD_D4_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(ARD_D4_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ARD_D7_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ARD_D7_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ARD_D13_Pin | ARD_D12_Pin | ARD_D11_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ARD_D3_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ARD_D3_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ARD_D6_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ARD_D6_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ARD_D8_Pin | ISM43362_BOOT0_Pin | ISM43362_WAKEUP_Pin | LED2_Pin
                         | SPSGRF_915_SDN_Pin | ARD_D5_Pin | SPSGRF_915_SPI3_CSN_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = LPS22HB_INT_DRDY_EXTI0_Pin | LSM6DSL_INT1_EXTI11_Pin | ARD_D2_Pin | HTS221_DRDY_EXTI15_Pin
                         | PMOD_IRQ_EXTI12_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = USB_OTG_FS_PWR_EN_Pin | SPBTLE_RF_SPI3_CSN_Pin | PMOD_RESET_Pin | STSAFE_A100_RESET_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = VL53L0X_XSHUT_Pin | LED3_WIFI__LED4_BLE_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = VL53L0X_GPIO1_EXTI7_Pin | LSM3MDL_DRDY_EXTI8_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = PMOD_SPI2_SCK_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(PMOD_SPI2_SCK_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = PMOD_UART2_CTS_Pin | PMOD_UART2_RTS_Pin | PMOD_UART2_TX_Pin | PMOD_UART2_RX_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ARD_D15_Pin | ARD_D14_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif
