/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body (Timer trigger ADC with DMA & FreeRTOS)
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h> // for strlen()

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma1;
TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart1;

#define SAMPLE_BUFFER_SIZE  256
uint16_t sample_buffer[SAMPLE_BUFFER_SIZE];

osThreadId_t printTaskHandle;
#define FLAG_HALF_CPLT 0x01
#define FLAG_FULL_CPLT 0x02

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void TIM2_Init(void);
static void ADC1_Init(void);
static void ADC1_DMA1CH1_init(void);

/* USER CODE BEGIN 0 */

// DMA Half Complete Callback
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    osThreadFlagsSet(printTaskHandle, FLAG_HALF_CPLT);
}

// DMA Full Complete Callback
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    osThreadFlagsSet(printTaskHandle, FLAG_FULL_CPLT);
}

// RTOS Print Task
void PrintTask(void *argument)
{
    uint32_t flags;
    char msg[64];

    // 開機存活證明
    sprintf(msg, "=== System Started! Waiting 6.4s for ADC buffer ===\r\n");
    HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);

    for(;;)
    {
        // 等待 DMA 中斷發送 Flag
        flags = osThreadFlagsWait(FLAG_HALF_CPLT | FLAG_FULL_CPLT, osFlagsWaitAny, osWaitForever);

        if (flags & FLAG_HALF_CPLT)
        {
            sprintf(msg, "\r\n[Top Half]\r\n");
            HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);

            // 前半段：0 ~ 127
            for (int i = 0; i < SAMPLE_BUFFER_SIZE / 2; i++) {
                sprintf(msg, "%d ", sample_buffer[i]);
                HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
            }
        }

        if (flags & FLAG_FULL_CPLT)
        {
            sprintf(msg, "\r\n[Bottom Half]\r\n");
            HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);

            // 後半段：128 ~ 255
            for (int i = SAMPLE_BUFFER_SIZE / 2; i < SAMPLE_BUFFER_SIZE; i++) {
                sprintf(msg, "%d ", sample_buffer[i]);
                HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
            }
        }
    }
}

// DMA 中斷處理常式
void DMA1_Channel1_IRQHandler(void)
{
    SET_BIT(hadc1.Instance->CFGR, ADC_CFGR_DMACFG); // 解決 Circular 模式硬體 Bug
    HAL_DMA_IRQHandler(&hdma1);
}

// 初始化 ADC 模組
static void ADC1_Init(void)
{
    __HAL_RCC_ADC_CLK_ENABLE(); // 確保 ADC 時鐘開啟

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
    hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T2_TRGO; // 改由 TIM2 觸發
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
    hadc1.Init.DMAContinuousRequests = ENABLE;
    hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
    hadc1.Init.OversamplingMode = DISABLE;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) { Error_Handler(); }

    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK) { Error_Handler(); }

    multimode.Mode = ADC_MODE_INDEPENDENT;
    if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK) { Error_Handler(); }

    // 設定讀取內部溫度感測器，並加長取樣時間
    sConfig.Channel = ADC_CHANNEL_TEMPSENSOR;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }
}

// 初始化 DMA
static void ADC1_DMA1CH1_init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma1.Instance = DMA1_Channel1;
    hdma1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma1.Init.MemInc = DMA_MINC_ENABLE;
    hdma1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma1.Init.Mode = DMA_CIRCULAR;
    hdma1.Init.Priority = DMA_PRIORITY_HIGH;

    if (HAL_DMA_Init(&hdma1) != HAL_OK) { Error_Handler(); }

    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma1);

    ADC1_Init();

    // 非常重要：優先權設為 5，避免 RTOS 崩潰！
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

// 初始化 TIM2
static void TIM2_Init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE(); // 確保 TIM2 時鐘開啟

    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 4000 - 1;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 1000 - 1; // 產生 20Hz 觸發信號
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK) { Error_Handler(); }

    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK) { Error_Handler(); }

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE; // TRGO 觸發 ADC
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK) { Error_Handler(); }
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

  MX_GPIO_Init();
  MX_USART1_UART_Init();

  // 1. 初始化 Timer 2 和 ADC+DMA
  TIM2_Init();
  ADC1_DMA1CH1_init();

  // 2. 初始化 RTOS 核心
  osKernelInitialize();

  // 3. 建立 PrintTask 任務 (加大 Stack 避免 sprintf 崩潰)
  const osThreadAttr_t printTask_attributes = {
    .name = "printTask",
    .stack_size = 512 * 4,
    .priority = (osPriority_t) osPriorityNormal,
  };
  printTaskHandle = osThreadNew(PrintTask, NULL, &printTask_attributes);

  // 4. 啟動 ADC DMA 轉換與 Timer
  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&sample_buffer[0], SAMPLE_BUFFER_SIZE);
  HAL_TIM_Base_Start(&htim2);

  // 5. 啟動排程器
  osKernelStart();

  while (1)
  {
  }
}

/**
  * @brief System Clock Configuration (來自你原版的 L475 設定)
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

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
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

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
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
  * @brief USART1 Initialization Function (來自你原版的 L475 設定)
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
  * @brief GPIO Initialization Function (來自你原版的 L475 設定)
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOE, M24SR64_Y_RF_DISABLE_Pin|M24SR64_Y_GPO_Pin|ISM43362_RST_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, ARD_D10_Pin|SPBTLE_RF_RST_Pin|ARD_D9_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, ARD_D8_Pin|ISM43362_BOOT0_Pin|ISM43362_WAKEUP_Pin|LED2_Pin
                          |SPSGRF_915_SDN_Pin|ARD_D5_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOD, USB_OTG_FS_PWR_EN_Pin|PMOD_RESET_Pin|STSAFE_A100_RESET_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(SPBTLE_RF_SPI3_CSN_GPIO_Port, SPBTLE_RF_SPI3_CSN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOC, VL53L0X_XSHUT_Pin|LED3_WIFI__LED4_BLE_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(SPSGRF_915_SPI3_CSN_GPIO_Port, SPSGRF_915_SPI3_CSN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ISM43362_SPI3_CSN_GPIO_Port, ISM43362_SPI3_CSN_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = M24SR64_Y_RF_DISABLE_Pin|M24SR64_Y_GPO_Pin|ISM43362_RST_Pin|ISM43362_SPI3_CSN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
