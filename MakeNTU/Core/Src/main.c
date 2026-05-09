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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "stm32l4xx_hal_adc_ex.h"
#include <stdio.h>
#include <string.h>
#if APP_USE_WIFI
#include "wifi.h"
#endif
#include "app_ws2812.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
DFSDM_Channel_HandleTypeDef hdfsdm1_channel1;

I2C_HandleTypeDef hi2c2;

QSPI_HandleTypeDef hqspi;

SPI_HandleTypeDef hspi3;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* USER CODE BEGIN PV */

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

static uint16_t adc_dma_buffer[APP_ADC_DMA_BUFFER_SAMPLES];
static uint16_t s_hit_thr_high[2] = { APP_EMG_HIT_THRESHOLD_HIGH, APP_EMG_HIT_THRESHOLD_HIGH };
static uint16_t s_hit_thr_low[2] = { APP_EMG_HIT_THRESHOLD_LOW, APP_EMG_HIT_THRESHOLD_LOW };
static volatile uint8_t s_calib_request = 0U;
static uint8_t s_calibrating = 0U;
static uint32_t s_calib_start_ms = 0U;
static uint32_t s_calib_last_report_ms = 0U;
static uint64_t s_calib_sum[2] = { 0U, 0U };
static uint32_t s_calib_count = 0U;
static uint32_t s_effort_event_id = 0U;

#if APP_USE_WIFI
static const uint8_t s_wifi_remote_ip[4] = {
  APP_WIFI_REMOTE_IP0,
  APP_WIFI_REMOTE_IP1,
  APP_WIFI_REMOTE_IP2,
  APP_WIFI_REMOTE_IP3,
};
static int32_t s_wifi_socket = -1;
static uint16_t s_wifi_sent = 0U;
static uint8_t s_wifi_ap_ok = 0U;
#endif

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DFSDM1_Init(void);
static void MX_I2C2_Init(void);
static void MX_QUADSPI_Init(void);
#if !APP_USE_WIFI
static void MX_SPI3_Init(void);
#endif
static void MX_USART1_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USB_OTG_FS_PCD_Init(void);
/* USER CODE BEGIN PFP */

static void App_ADC_WithDma_Init(void);
static void App_AdcTriggerTimer_InitStart(void);
#if APP_USE_WIFI
static void App_DebugUart1(const char *msg);
static WIFI_Status_t App_TryWifiConnect(void);
static WIFI_Status_t App_WifiSendAll(const uint8_t *buf, uint16_t len, uint32_t timeout_ms);
static void App_WifiPollPcRx(void);
#endif

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#if APP_USE_WIFI
static void App_DebugUart1(const char *msg)
{
  if (msg == NULL)
  {
    return;
  }
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)msg, (uint16_t)strlen(msg), 400U);
}

static WIFI_Status_t App_TryWifiConnect(void)
{
  static const WIFI_Ecn_t ecn_list[] = {
    WIFI_ECN_WPA2_PSK,
    WIFI_ECN_WPA_WPA2_PSK,
    WIFI_ECN_WPA_PSK,
    WIFI_ECN_OPEN
  };
  static const char *ecn_name[] = {
    "WPA2_PSK",
    "WPA_WPA2_PSK",
    "WPA_PSK",
    "OPEN"
  };

  for (uint32_t i = 0; i < (uint32_t)(sizeof(ecn_list) / sizeof(ecn_list[0])); i++)
  {
    char msg[64];
    const char *pwd = APP_WIFI_PASSWORD;
    WIFI_Status_t st;
    if (ecn_list[i] == WIFI_ECN_OPEN)
    {
      pwd = "";
    }
    (void)snprintf(msg, sizeof(msg), "[WiFi] connect try ECN=%s\r\n", ecn_name[i]);
    App_DebugUart1(msg);
    st = WIFI_Connect(APP_WIFI_SSID, pwd, ecn_list[i]);
    (void)snprintf(msg, sizeof(msg), "[WiFi] connect status=%d\r\n", (int)st);
    App_DebugUart1(msg);
    if (st == WIFI_STATUS_OK)
    {
      (void)snprintf(msg, sizeof(msg), "[WiFi] connect ok with ECN=%s\r\n", ecn_name[i]);
      App_DebugUart1(msg);
      return WIFI_STATUS_OK;
    }
  }
  return WIFI_STATUS_ERROR;
}

static WIFI_Status_t App_WifiSendAll(const uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
  uint16_t sent_total = 0U;
  uint16_t sent_once = 0U;
  uint8_t guard = 0U;

  if ((buf == NULL) || (len == 0U) || (s_wifi_socket < 0))
  {
    return WIFI_STATUS_ERROR;
  }

  while (sent_total < len)
  {
    const WIFI_Status_t st = WIFI_SendData((uint32_t)s_wifi_socket, &buf[sent_total],
                                           (uint16_t)(len - sent_total), &sent_once, timeout_ms);
    if ((st != WIFI_STATUS_OK) || (sent_once == 0U))
    {
      return WIFI_STATUS_ERROR;
    }
    sent_total = (uint16_t)(sent_total + sent_once);
    if (++guard > 8U)
    {
      return WIFI_STATUS_ERROR;
    }
  }
  s_wifi_sent = sent_total;
  return WIFI_STATUS_OK;
}

static char s_pc_rxbuf[96];
static size_t s_pc_rxlen = 0U;

static void App_ParsePcCmdLine(char *line)
{
  while ((*line == ' ') || (*line == '\t'))
  {
    line++;
  }
  if (strncmp(line, "jdg:", 4) != 0)
  {
#if APP_PC_RX_LOG_UART
    if (line[0] != '\0')
    {
      char msg[120];
      (void)snprintf(msg, sizeof(msg), "[PC] tcp rx (not jdg): %.80s\r\n", line);
      App_DebugUart1(msg);
    }
#endif
    return;
  }
  line += 4;
  while ((*line == ' ') || (*line == '\t'))
  {
    line++;
  }
  if (strncmp(line, "perfect", 7) == 0)
  {
#if APP_USE_WS2812_JDG
    App_Ws2812_SetJudge("perfect");
#endif
    App_DebugUart1("[PC] jdg:perfect -> LED green (WS2812)\r\n");
  }
  else if (strncmp(line, "good", 4) == 0)
  {
#if APP_USE_WS2812_JDG
    App_Ws2812_SetJudge("good");
#endif
    App_DebugUart1("[PC] jdg:good -> LED yellow\r\n");
  }
  else if ((strncmp(line, "miss", 4) == 0) || (strncmp(line, "bad", 3) == 0))
  {
#if APP_USE_WS2812_JDG
    App_Ws2812_SetJudge("miss");
#endif
    App_DebugUart1("[PC] jdg:miss -> LED red\r\n");
  }
  else if (strncmp(line, "off", 3) == 0)
  {
#if APP_USE_WS2812_JDG
    App_Ws2812_SetJudge("off");
#endif
    App_DebugUart1("[PC] jdg:off -> LED clear\r\n");
  }
  else
  {
    char msg[120];
    (void)snprintf(msg, sizeof(msg), "[PC] jdg:? unknown arg: %.48s\r\n", line);
    App_DebugUart1(msg);
  }
}

static void App_AppendPcRxByte(uint8_t b)
{
  const char c = (char)b;
  if ((c == '\r') || (c == '\n'))
  {
    if (s_pc_rxlen < sizeof(s_pc_rxbuf))
    {
      s_pc_rxbuf[s_pc_rxlen] = '\0';
    }
    else
    {
      s_pc_rxbuf[sizeof(s_pc_rxbuf) - 1U] = '\0';
    }
    if (s_pc_rxlen > 0U)
    {
      App_ParsePcCmdLine(s_pc_rxbuf);
    }
    s_pc_rxlen = 0U;
    return;
  }
  if (s_pc_rxlen + 1U < sizeof(s_pc_rxbuf))
  {
    s_pc_rxbuf[s_pc_rxlen++] = (char)c;
  }
  else
  {
    s_pc_rxlen = 0U;
  }
}

static void App_WifiPollPcRx(void)
{
  if (s_wifi_socket < 0)
  {
    return;
  }
  for (;;)
  {
    uint8_t chunk[48];
    uint16_t n = 0U;
    const WIFI_Status_t st = WIFI_ReceiveData((uint32_t)s_wifi_socket, chunk, (uint16_t)sizeof(chunk), &n, 1U);
    if ((st != WIFI_STATUS_OK) || (n == 0U))
    {
      break;
    }
    for (uint16_t i = 0U; i < n; i++)
    {
      App_AppendPcRxByte(chunk[i]);
    }
  }
}
#endif

/**
 * Latest interleaved ADC pair (rank1=A0 PC5 IN14, rank2=A1 PC4 IN13).
 * Applies light IIR smoothing per channel (~same as legacy single-channel).
 */
static void App_AdcLatestSmoothed(uint16_t *out_avg0, uint16_t *out_avg1)
{
  static int32_t avg_q8[2] = { 0, 0 };
  uint32_t wr = APP_ADC_DMA_BUFFER_SAMPLES - __HAL_DMA_GET_COUNTER(&hdma_adc1);

  if (wr < 2U)
  {
    wr = APP_ADC_DMA_BUFFER_SAMPLES;
  }

  wr = ((wr / 2U) * 2U);
  if (wr < 2U)
  {
    wr = APP_ADC_DMA_BUFFER_SAMPLES;
  }

  const uint16_t x0 = adc_dma_buffer[wr - 2U];
  const uint16_t x1 = adc_dma_buffer[wr - 1U];

  for (uint32_t i = 0U; i < 2U; i++)
  {
    const uint16_t x = (i == 0U) ? x0 : x1;
    avg_q8[i] += ((((int32_t)x) << 8) - avg_q8[i]) >> 3;
    if (avg_q8[i] < 0)
    {
      avg_q8[i] = 0;
    }
    if (avg_q8[i] > (4095 << 8))
    {
      avg_q8[i] = (4095 << 8);
    }
  }
  *out_avg0 = (uint16_t)(avg_q8[0] >> 8);
  *out_avg1 = (uint16_t)(avg_q8[1] >> 8);
}

static uint8_t App_EmgHitDetectCh(uint8_t ch, uint16_t pulse, uint32_t now_ms)
{
  static uint8_t armed[2] = { 1U, 1U };
  static uint32_t last_hit_ms[2] = { 0U, 0U };
  static uint16_t prev_pulse[2] = { 0U, 0U };
  uint16_t rise = 0U;

  if (ch >= 2U)
  {
    return 0U;
  }

  if (pulse > prev_pulse[ch])
  {
    rise = (uint16_t)(pulse - prev_pulse[ch]);
  }
  prev_pulse[ch] = pulse;

  if ((now_ms - last_hit_ms[ch]) < APP_EMG_HIT_COOLDOWN_MS)
  {
    return 0U;
  }

  if (armed[ch] != 0U)
  {
    if ((pulse >= s_hit_thr_high[ch]) && (rise >= APP_EMG_HIT_MIN_RISE))
    {
      armed[ch] = 0U;
      last_hit_ms[ch] = now_ms;
      return 1U;
    }
  }
  else if (pulse <= s_hit_thr_low[ch])
  {
    armed[ch] = 1U;
  }

  return 0U;
}

static uint8_t App_UpdateEffortEvent(uint16_t level, uint32_t now_ms, char *out, size_t out_len)
{
  static uint8_t active = 0U;
  static uint32_t event_start_ms = 0U;
  static uint32_t high_time_ms = 0U;
  static uint32_t peak = 0U;
  static uint32_t first_high_ms = 0U;
  static uint32_t last_update_ms = 0U;
  static uint32_t below_since_ms = 0U;
  const uint8_t above_low = (level >= s_hit_thr_low[0]) ? 1U : 0U;
  const uint8_t above_high = (level >= s_hit_thr_high[0]) ? 1U : 0U;
  const uint32_t dt = (last_update_ms == 0U) ? APP_ADC_TX_PERIOD_MS : (now_ms - last_update_ms);
  last_update_ms = now_ms;

  if (active == 0U)
  {
    if (above_low != 0U)
    {
      active = 1U;
      event_start_ms = now_ms;
      high_time_ms = 0U;
      peak = level;
      first_high_ms = 0U;
      below_since_ms = 0U;
      if (above_high != 0U)
      {
        first_high_ms = now_ms;
        high_time_ms = dt;
      }
    }
    return 0U;
  }

  if (level > peak)
  {
    peak = level;
  }
  if (above_high != 0U)
  {
    if (first_high_ms == 0U)
    {
      first_high_ms = now_ms;
    }
    high_time_ms += dt;
    below_since_ms = 0U;
  }
  else if ((above_low == 0U) && (below_since_ms == 0U))
  {
    below_since_ms = now_ms;
  }
  else if (above_low != 0U)
  {
    below_since_ms = 0U;
  }

  if ((below_since_ms != 0U) && ((now_ms - below_since_ms) >= APP_EMG_EVENT_END_GAP_MS))
  {
    const uint32_t rise_ms = (first_high_ms > event_start_ms) ? (first_high_ms - event_start_ms) : 0U;
    const int n = snprintf(out, out_len, "evt:%lu high_ms:%lu peak:%lu rise_ms:%lu\r\n",
                           (unsigned long)(++s_effort_event_id),
                           (unsigned long)high_time_ms,
                           (unsigned long)peak,
                           (unsigned long)rise_ms);
    active = 0U;
    return (n > 0) ? 1U : 0U;
  }
  return 0U;
}

static void App_StartCalibration(uint32_t now_ms)
{
  const char *msg = "[CAL] start 10s relax — calibrate BOTH A0 + A1 (relax both sensors)\r\n";
  s_calibrating = 1U;
  s_calib_start_ms = now_ms;
  s_calib_last_report_ms = now_ms;
  s_calib_sum[0] = 0U;
  s_calib_sum[1] = 0U;
  s_calib_count = 0U;
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)msg, (uint16_t)strlen(msg), 100U);
}

static void App_HandleCalibration(uint16_t avg0, uint16_t avg1, uint32_t now_ms)
{
  if (s_calibrating == 0U)
  {
    return;
  }

  s_calib_sum[0] += (uint64_t)avg0;
  s_calib_sum[1] += (uint64_t)avg1;
  s_calib_count++;

  if ((now_ms - s_calib_last_report_ms) >= 1000U)
  {
    char msg[64];
    const uint32_t elapsed = now_ms - s_calib_start_ms;
    const uint32_t left = (elapsed < APP_EMG_CALIBRATION_MS) ? (APP_EMG_CALIBRATION_MS - elapsed) : 0U;
    const int n = snprintf(msg, sizeof(msg), "[CAL] %lus left\r\n", (unsigned long)(left / 1000U));
    if (n > 0)
    {
      (void)HAL_UART_Transmit(&huart1, (uint8_t *)msg, (uint16_t)n, 100U);
    }
    s_calib_last_report_ms = now_ms;
  }

  if ((now_ms - s_calib_start_ms) >= APP_EMG_CALIBRATION_MS)
  {
    char msg[132];
    s_calibrating = 0U;

    if (s_calib_count > 0U)
    {
      for (uint32_t c = 0U; c < 2U; c++)
      {
        uint32_t baseline = (uint32_t)(s_calib_sum[c] / (uint64_t)s_calib_count);
        uint32_t low = baseline + APP_EMG_LOW_OFFSET;
        uint32_t high = baseline + APP_EMG_HIGH_OFFSET;
        if (low > 4095U)
        {
          low = 4095U;
        }
        if (high > 4095U)
        {
          high = 4095U;
        }
        if (high <= low)
        {
          high = (low < 4095U) ? (low + 1U) : 4095U;
        }
        s_hit_thr_low[c] = (uint16_t)low;
        s_hit_thr_high[c] = (uint16_t)high;

        (void)snprintf(msg, sizeof(msg), "[CAL] %s base=%lu L=%u H=%u\r\n",
                       (c == 0U) ? "A0(red)" : "A1(blue)",
                       (unsigned long)baseline, (unsigned)s_hit_thr_low[c], (unsigned)s_hit_thr_high[c]);
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)msg, (uint16_t)strlen(msg), 100U);
      }
    }
  }
}

static void App_AdcTriggerTimer_InitStart(void)
{
  const uint32_t tim_clk = HAL_RCC_GetPCLK1Freq();
  const uint32_t tick_hz = 1000000U; /* 1 MHz timer tick for exact 1 kHz period math */
  const uint32_t psc = (tim_clk / tick_hz);
  const uint32_t arr = (tick_hz / APP_ADC_SAMPLE_RATE_HZ);

  if ((psc == 0U) || (arr == 0U))
  {
    Error_Handler();
  }

  __HAL_RCC_TIM6_CLK_ENABLE();
  TIM6->CR1 = 0U;
  TIM6->PSC = psc - 1U;
  TIM6->ARR = arr - 1U;
  TIM6->CR2 = (TIM6->CR2 & ~(7U << 4)) | (2U << 4); /* MMS=010: TRGO on update */
  TIM6->EGR = 1U;  /* UG: load prescaler/arr immediately */
  TIM6->SR = 0U;
}

static void App_ADC_WithDma_Init(void)
{
  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 2;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T6_TRGO;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /* ARD_A0: PC5 = ADC1_IN14  |  ARD_A1: PC4 = ADC1_IN13 (one ADC, scan order per TIM6 tick) */
  sConfig.Channel = ADC_CHANNEL_14;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_13;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_RCC_DMA1_CLK_ENABLE();
  /*
   * Older stm32l475xx.h packs omit RCC_AHB1ENR_DMAMUX1EN / DMAMUX1; chip still has DMAMUX (RM0351).
   */
#if defined(DMAMUX1)
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
#else
  SET_BIT(RCC->AHB1ENR, 0x00000004UL);
  (void)READ_BIT(RCC->AHB1ENR, 0x00000004UL);
#endif

  hdma_adc1.Instance = DMA1_Channel1;
#if defined(DMAMUX1)
  hdma_adc1.Init.Request = DMA_REQUEST_ADC1;
#else
  /*
   * When CMSIS omits DMAMUX1, HAL configures DMA via DMA1_CSELR (4-bit index per channel), not DMAMUX
   * request line IDs. ST's STM32L4 ADC+DMA examples (e.g. NUCLEO-L496) use C1S = 0 for ADC1 -> DMA1 Ch1.
   */
  hdma_adc1.Init.Request = DMA_REQUEST_0;
#endif
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

  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

  App_AdcTriggerTimer_InitStart();

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buffer, APP_ADC_DMA_BUFFER_SAMPLES) != HAL_OK)
  {
    Error_Handler();
  }
  TIM6->CR1 |= 1U; /* CEN: start 1kHz trigger timer */
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DFSDM1_Init();
  MX_I2C2_Init();
  MX_QUADSPI_Init();
#if !APP_USE_WIFI
  MX_SPI3_Init();
#endif
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_USB_OTG_FS_PCD_Init();
  /* USER CODE BEGIN 2 */

#if APP_USE_WIFI
  /* EsWifi owns SPI3, NSS/SPI_GPIO, ISR (see SPI_WIFI_Init). Do not call MX_SPI3_Init when APP_USE_WIFI=1. */
  App_DebugUart1("[WiFi] init...\r\n");
  if (WIFI_Init() != WIFI_STATUS_OK)
  {
    App_DebugUart1("[WiFi] WIFI_Init FAILED\r\n");
  }
  else
  {
    App_DebugUart1("[WiFi] joining AP...\r\n");
    if (App_TryWifiConnect() != WIFI_STATUS_OK)
    {
      App_DebugUart1("[WiFi] WIFI_Connect FAILED (SSID/pwd/security?/2.4GHz/WPA3-only)\r\n");
    }
    else
    {
      s_wifi_ap_ok = 1U;
      uint8_t sta[4] = {0};
      if (WIFI_GetIP_Address(sta, 4U) == WIFI_STATUS_OK)
      {
        char ipmsg[56];
        (void)snprintf(ipmsg, sizeof(ipmsg), "[WiFi] STA IP %u.%u.%u.%u\r\n",
                       (unsigned)sta[0], (unsigned)sta[1], (unsigned)sta[2], (unsigned)sta[3]);
        App_DebugUart1(ipmsg);
      }
      {
        char tgt[56];
        (void)snprintf(tgt, sizeof(tgt), "[WiFi] TCP target %u.%u.%u.%u:%u\r\n",
                       (unsigned)APP_WIFI_REMOTE_IP0, (unsigned)APP_WIFI_REMOTE_IP1,
                       (unsigned)APP_WIFI_REMOTE_IP2, (unsigned)APP_WIFI_REMOTE_IP3,
                       (unsigned)APP_WIFI_REMOTE_PORT);
        App_DebugUart1(tgt);
      }
      App_DebugUart1("[WiFi] open TCP client (is Python listening? firewall?)\r\n");
      uint8_t tries = 20U;
      while (tries-- > 0U)
      {
        if (WIFI_OpenClientConnection(0U, WIFI_TCP_PROTOCOL, "adc_tcp", s_wifi_remote_ip,
                                      APP_WIFI_REMOTE_PORT, 0U) == WIFI_STATUS_OK)
        {
          s_wifi_socket = 0;
          App_DebugUart1("[WiFi] TCP connected -> hit:a0 (red) + hit:a1 (blue)\r\n");
          break;
        }
        HAL_Delay(250);
      }
      if (s_wifi_socket < 0)
      {
        App_DebugUart1("[WiFi] TCP FAILED (fix PC IP in main.h, port, run Python first, allow firewall)\r\n");
      }
    }
  }
#endif
  App_ADC_WithDma_Init();
  {
    char msg[80];
    const int n = snprintf(msg, sizeof(msg), "[ADC] sampling=%luHz tx=%lums\r\n",
                           (unsigned long)APP_ADC_SAMPLE_RATE_HZ, (unsigned long)APP_ADC_TX_PERIOD_MS);
    if (n > 0)
    {
      (void)HAL_UART_Transmit(&huart1, (uint8_t *)msg, (uint16_t)((size_t)n), 100U);
    }
  }

  App_Ws2812_Init();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    static uint32_t last_tx_ms;
    const uint32_t now = HAL_GetTick();
#if APP_USE_WIFI
    App_WifiPollPcRx();
#endif
#if APP_USE_WS2812_JDG
    App_Ws2812_Tick(now);
#endif
    if (s_calib_request != 0U)
    {
      s_calib_request = 0U;
      App_StartCalibration(now);
    }
    if ((now - last_tx_ms) >= APP_ADC_TX_PERIOD_MS)
    {
      last_tx_ms = now;
      uint16_t avg0 = 0U;
      uint16_t avg1 = 0U;
      App_AdcLatestSmoothed(&avg0, &avg1);

      App_HandleCalibration(avg0, avg1, now);

      const uint8_t hit0 = (s_calibrating != 0U) ? 0U : App_EmgHitDetectCh(0U, avg0, now);
      const uint8_t hit1 = (s_calibrating != 0U) ? 0U : App_EmgHitDetectCh(1U, avg1, now);

      char evt_line[96];
      const uint8_t evt_ready = (s_calibrating != 0U) ? 0U : App_UpdateEffortEvent(avg0, now, evt_line, sizeof(evt_line));

      char hit_line[2][72];
      size_t hit_len[2] = { 0U, 0U };

      if (hit0 != 0U)
      {
        const int n = snprintf(hit_line[0], sizeof(hit_line[0]),
                               "hit:a0 avg:%4u L:%u H:%u\r\n",
                               (unsigned)avg0, (unsigned)s_hit_thr_low[0], (unsigned)s_hit_thr_high[0]);
        if (n > 0)
        {
          hit_len[0] = ((size_t)n < sizeof(hit_line[0])) ? (size_t)n : sizeof(hit_line[0]);
        }
      }
      if (hit1 != 0U)
      {
        const int n = snprintf(hit_line[1], sizeof(hit_line[1]),
                               "hit:a1 avg:%4u L:%u H:%u\r\n",
                               (unsigned)avg1, (unsigned)s_hit_thr_low[1], (unsigned)s_hit_thr_high[1]);
        if (n > 0)
        {
          hit_len[1] = ((size_t)n < sizeof(hit_line[1])) ? (size_t)n : sizeof(hit_line[1]);
        }
      }

      if (hit_len[0] > 0U)
      {
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)hit_line[0], (uint16_t)hit_len[0], 100U);
      }
      if (hit_len[1] > 0U)
      {
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)hit_line[1], (uint16_t)hit_len[1], 100U);
      }
      if (evt_ready != 0U)
      {
        const size_t evt_len = strlen(evt_line);
        if (evt_len > 0U)
        {
          (void)HAL_UART_Transmit(&huart1, (uint8_t *)evt_line, (uint16_t)evt_len, 100U);
        }
      }
#if APP_USE_WIFI
      if (s_wifi_socket >= 0)
      {
        for (uint32_t hi = 0U; hi < 2U; hi++)
        {
          if (hit_len[hi] == 0U)
          {
            continue;
          }
          if (App_WifiSendAll((const uint8_t *)hit_line[hi], (uint16_t)hit_len[hi], 500U) != WIFI_STATUS_OK)
          {
            static uint32_t last_err_ms;
            if ((now - last_err_ms) >= 2000U)
            {
              last_err_ms = now;
              App_DebugUart1("[WiFi] WIFI_SendData failed\r\n");
            }
          }
        }
        if (evt_ready != 0U)
        {
          const size_t evt_len = strlen(evt_line);
          if (evt_len > 0U)
          {
            (void)App_WifiSendAll((const uint8_t *)evt_line, (uint16_t)evt_len, 500U);
          }
        }
      }
      else
      {
        if (s_wifi_ap_ok != 0U)
        {
          static uint32_t last_tcp_retry_ms;
          if ((now - last_tcp_retry_ms) >= 8000U)
          {
            last_tcp_retry_ms = now;
            if (WIFI_OpenClientConnection(0U, WIFI_TCP_PROTOCOL, "adc_tcp", s_wifi_remote_ip,
                                          APP_WIFI_REMOTE_PORT, 0U) == WIFI_STATUS_OK)
            {
              s_wifi_socket = 0;
              App_DebugUart1("[WiFi] TCP connected (retry)\r\n");
            }
          }
        }
        static uint32_t last_hint_ms;
        if ((now - last_hint_ms) >= 5000U)
        {
          last_hint_ms = now;
          App_DebugUart1("[WiFi] no socket: UART1 only; Python will stay empty\r\n");
        }
      }
#else
      if (hit_len[0] > 0U)
      {
        (void)HAL_UART_Transmit(&huart3, (uint8_t *)hit_line[0], (uint16_t)hit_len[0], 100U);
      }
      if (hit_len[1] > 0U)
      {
        (void)HAL_UART_Transmit(&huart3, (uint8_t *)hit_line[1], (uint16_t)hit_len[1], 100U);
      }
#endif
    }

    /* USER CODE END 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
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

  /** Initializes the CPU, AHB and APB buses clocks
  */
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

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief DFSDM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DFSDM1_Init(void)
{

  /* USER CODE BEGIN DFSDM1_Init 0 */

  /* USER CODE END DFSDM1_Init 0 */

  /* USER CODE BEGIN DFSDM1_Init 1 */

  /* USER CODE END DFSDM1_Init 1 */
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
  /* USER CODE BEGIN DFSDM1_Init 2 */

  /* USER CODE END DFSDM1_Init 2 */

}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x10D19CE4;
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

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */

}

/**
  * @brief QUADSPI Initialization Function
  * @param None
  * @retval None
  */
static void MX_QUADSPI_Init(void)
{

  /* USER CODE BEGIN QUADSPI_Init 0 */

  /* USER CODE END QUADSPI_Init 0 */

  /* USER CODE BEGIN QUADSPI_Init 1 */

  /* USER CODE END QUADSPI_Init 1 */
  /* QUADSPI parameter configuration*/
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
  /* USER CODE BEGIN QUADSPI_Init 2 */

  /* USER CODE END QUADSPI_Init 2 */

}

#if !APP_USE_WIFI
/**
  * @brief SPI3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI3_Init(void)
{

  /* USER CODE BEGIN SPI3_Init 0 */

  /* USER CODE END SPI3_Init 0 */

  /* USER CODE BEGIN SPI3_Init 1 */

  /* USER CODE END SPI3_Init 1 */
  /* SPI3 parameter configuration*/
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
  /* USER CODE BEGIN SPI3_Init 2 */

  /* USER CODE END SPI3_Init 2 */

}
#endif

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
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
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
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
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief USB_OTG_FS Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_OTG_FS_PCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_FS_Init 0 */

  /* USER CODE END USB_OTG_FS_Init 0 */

  /* USER CODE BEGIN USB_OTG_FS_Init 1 */

  /* USER CODE END USB_OTG_FS_Init 1 */
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
  /* USER CODE BEGIN USB_OTG_FS_Init 2 */

  /* USER CODE END USB_OTG_FS_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, M24SR64_Y_RF_DISABLE_Pin|M24SR64_Y_GPO_Pin|ISM43362_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, ARD_D10_Pin|SPBTLE_RF_RST_Pin|ARD_D9_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, ARD_D8_Pin|ISM43362_BOOT0_Pin|ISM43362_WAKEUP_Pin|LED2_Pin
                          |SPSGRF_915_SDN_Pin|ARD_D5_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, USB_OTG_FS_PWR_EN_Pin|PMOD_RESET_Pin|STSAFE_A100_RESET_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPBTLE_RF_SPI3_CSN_GPIO_Port, SPBTLE_RF_SPI3_CSN_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, VL53L0X_XSHUT_Pin|LED3_WIFI__LED4_BLE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPSGRF_915_SPI3_CSN_GPIO_Port, SPSGRF_915_SPI3_CSN_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(ISM43362_SPI3_CSN_GPIO_Port, ISM43362_SPI3_CSN_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : M24SR64_Y_RF_DISABLE_Pin M24SR64_Y_GPO_Pin ISM43362_RST_Pin ISM43362_SPI3_CSN_Pin */
  GPIO_InitStruct.Pin = M24SR64_Y_RF_DISABLE_Pin|M24SR64_Y_GPO_Pin|ISM43362_RST_Pin|ISM43362_SPI3_CSN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : USB_OTG_FS_OVRCR_EXTI3_Pin SPSGRF_915_GPIO3_EXTI5_Pin SPBTLE_RF_IRQ_EXTI6_Pin ISM43362_DRDY_EXTI1_Pin */
  GPIO_InitStruct.Pin = USB_OTG_FS_OVRCR_EXTI3_Pin|SPSGRF_915_GPIO3_EXTI5_Pin|SPBTLE_RF_IRQ_EXTI6_Pin|ISM43362_DRDY_EXTI1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : BUTTON_EXTI13_Pin */
  GPIO_InitStruct.Pin = BUTTON_EXTI13_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BUTTON_EXTI13_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : ARD_A5_Pin ARD_A4_Pin ARD_A3_Pin ARD_A2_Pin
                           ARD_A1_Pin ARD_A0_Pin */
  GPIO_InitStruct.Pin = ARD_A5_Pin|ARD_A4_Pin|ARD_A3_Pin|ARD_A2_Pin
                          |ARD_A1_Pin|ARD_A0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : ARD_D1_Pin ARD_D0_Pin */
  GPIO_InitStruct.Pin = ARD_D1_Pin|ARD_D0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF8_UART4;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : ARD_D10_Pin SPBTLE_RF_RST_Pin ARD_D9_Pin */
  GPIO_InitStruct.Pin = ARD_D10_Pin|SPBTLE_RF_RST_Pin|ARD_D9_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : ARD_D4_Pin */
  GPIO_InitStruct.Pin = ARD_D4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(ARD_D4_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ARD_D7_Pin */
  GPIO_InitStruct.Pin = ARD_D7_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ARD_D7_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : ARD_D13_Pin ARD_D12_Pin ARD_D11_Pin */
  GPIO_InitStruct.Pin = ARD_D13_Pin|ARD_D12_Pin|ARD_D11_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : ARD_D3_Pin */
  GPIO_InitStruct.Pin = ARD_D3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ARD_D3_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ARD_D6_Pin */
  GPIO_InitStruct.Pin = ARD_D6_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ARD_D6_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : ARD_D8_Pin ISM43362_BOOT0_Pin ISM43362_WAKEUP_Pin LED2_Pin
                           SPSGRF_915_SDN_Pin ARD_D5_Pin SPSGRF_915_SPI3_CSN_Pin */
  GPIO_InitStruct.Pin = ARD_D8_Pin|ISM43362_BOOT0_Pin|ISM43362_WAKEUP_Pin|LED2_Pin
                          |SPSGRF_915_SDN_Pin|ARD_D5_Pin|SPSGRF_915_SPI3_CSN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : LPS22HB_INT_DRDY_EXTI0_Pin LSM6DSL_INT1_EXTI11_Pin ARD_D2_Pin HTS221_DRDY_EXTI15_Pin
                           PMOD_IRQ_EXTI12_Pin */
  GPIO_InitStruct.Pin = LPS22HB_INT_DRDY_EXTI0_Pin|LSM6DSL_INT1_EXTI11_Pin|ARD_D2_Pin|HTS221_DRDY_EXTI15_Pin
                          |PMOD_IRQ_EXTI12_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : USB_OTG_FS_PWR_EN_Pin SPBTLE_RF_SPI3_CSN_Pin PMOD_RESET_Pin STSAFE_A100_RESET_Pin */
  GPIO_InitStruct.Pin = USB_OTG_FS_PWR_EN_Pin|SPBTLE_RF_SPI3_CSN_Pin|PMOD_RESET_Pin|STSAFE_A100_RESET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : VL53L0X_XSHUT_Pin LED3_WIFI__LED4_BLE_Pin */
  GPIO_InitStruct.Pin = VL53L0X_XSHUT_Pin|LED3_WIFI__LED4_BLE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : VL53L0X_GPIO1_EXTI7_Pin LSM3MDL_DRDY_EXTI8_Pin */
  GPIO_InitStruct.Pin = VL53L0X_GPIO1_EXTI7_Pin|LSM3MDL_DRDY_EXTI8_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PMOD_SPI2_SCK_Pin */
  GPIO_InitStruct.Pin = PMOD_SPI2_SCK_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(PMOD_SPI2_SCK_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PMOD_UART2_CTS_Pin PMOD_UART2_RTS_Pin PMOD_UART2_TX_Pin PMOD_UART2_RX_Pin */
  GPIO_InitStruct.Pin = PMOD_UART2_CTS_Pin|PMOD_UART2_RTS_Pin|PMOD_UART2_TX_Pin|PMOD_UART2_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : ARD_D15_Pin ARD_D14_Pin */
  GPIO_InitStruct.Pin = ARD_D15_Pin|ARD_D14_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == BUTTON_EXTI13_Pin)
  {
    s_calib_request = 1U;
  }
#if APP_USE_WIFI
  if (GPIO_Pin == ISM43362_DRDY_EXTI1_Pin)
  {
    SPI_WIFI_ISR();
  }
#endif
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
