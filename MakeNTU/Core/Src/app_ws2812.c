/**
 * WS2812 on ARD_D5 (PB4). Bit-bang is the default reliable path.
 * Optional: APP_WS2812_USE_PWM_DMA=1 — TIM3_CH1 + TIM3_UP DMA → CCR1 (~800 kHz slots).
 */
#include "app_ws2812.h"
#include "main.h"

#include "stm32l4xx_hal.h"

#include <string.h>

#if APP_USE_WS2812_JDG

#ifndef APP_WS2812_NUM_LEDS
#define APP_WS2812_NUM_LEDS 8U
#endif

/** 0 = standard WS2812 GRB byte order; 1 = some clones expect RGB order */
#ifndef APP_WS2812_BYTE_ORDER_RGB
#define APP_WS2812_BYTE_ORDER_RGB 0
#endif

#define WS_BUF_BYTES ((size_t)APP_WS2812_NUM_LEDS * 3U)

static uint8_t s_grb[WS_BUF_BYTES];
static uint32_t s_hold_until_ms = 0U;

#ifndef APP_WS2812_TIM_T0H_NOP
#define APP_WS2812_TIM_T0H_NOP 14U
#endif
#ifndef APP_WS2812_TIM_T1H_NOP
#define APP_WS2812_TIM_T1H_NOP 44U
#endif
#ifndef APP_WS2812_TIM_TLOW_NOP
#define APP_WS2812_TIM_TLOW_NOP 18U
#endif

/* --- Shared GPIO bit-bang (PB4 as GPIO, same physical pin as ARD_D5) --- */

static inline void ws_bb_pin_set(uint32_t level)
{
  HAL_GPIO_WritePin(APP_WS2812_GPIO_Port, APP_WS2812_GPIO_Pin,
                    (level != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* BSRR: faster than HAL_GPIO_WritePin — bit timing matches datasheet better */
static inline void ws_bb_pin_fast(uint32_t level)
{
  GPIO_TypeDef *const port = APP_WS2812_GPIO_Port;
  const uint32_t pin = (uint32_t)APP_WS2812_GPIO_Pin;
  if (level != 0U)
  {
    port->BSRR = pin;
  }
  else
  {
    port->BSRR = pin << 16U;
  }
}

static inline void ws_bb_delay_nop(volatile uint32_t n)
{
  while (n > 0U)
  {
    __NOP();
    n--;
  }
}

static void ws_bb_bus_reset_low_us(uint32_t us)
{
  const uint32_t hz = HAL_RCC_GetSysClockFreq();
  uint32_t loops = 0U;
  if (hz >= 1000000U)
  {
    loops = ((hz / 1000000U) * us) / 4U;
  }
  if (loops < 2000U)
  {
    loops = 10000U;
  }
  ws_bb_pin_set(0U);
  while (loops-- > 0U)
  {
    __NOP();
  }
}

static void ws_bb_send_byte(uint8_t b)
{
  for (int32_t i = 7; i >= 0; i--)
  {
    if ((b >> i) & 1)
    {
      ws_bb_pin_fast(1U);
      ws_bb_delay_nop(APP_WS2812_TIM_T1H_NOP);
      ws_bb_pin_fast(0U);
      ws_bb_delay_nop(APP_WS2812_TIM_TLOW_NOP);
    }
    else
    {
      ws_bb_pin_fast(1U);
      ws_bb_delay_nop(APP_WS2812_TIM_T0H_NOP);
      ws_bb_pin_fast(0U);
      ws_bb_delay_nop(APP_WS2812_TIM_TLOW_NOP);
    }
  }
}

static void ws_bb_gpio_init(void)
{
  GPIO_InitTypeDef io = {0};
  io.Pin = APP_WS2812_GPIO_Pin;
  io.Mode = GPIO_MODE_OUTPUT_PP;
  io.Pull = GPIO_NOPULL;
  io.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(APP_WS2812_GPIO_Port, &io);
  HAL_GPIO_WritePin(APP_WS2812_GPIO_Port, APP_WS2812_GPIO_Pin, GPIO_PIN_RESET);
}

static void ws_bb_push(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  for (size_t i = 0U; i < WS_BUF_BYTES; i++)
  {
    ws_bb_send_byte(s_grb[i]);
  }
  ws_bb_pin_set(0U);
  __set_PRIMASK(primask);
  ws_bb_bus_reset_low_us(120U);
}

static void ws_buf_fill_color(uint8_t g, uint8_t r, uint8_t b)
{
  for (size_t i = 0U; i < APP_WS2812_NUM_LEDS; i++)
  {
#if APP_WS2812_BYTE_ORDER_RGB
    s_grb[i * 3U + 0U] = r;
    s_grb[i * 3U + 1U] = g;
    s_grb[i * 3U + 2U] = b;
#else
    s_grb[i * 3U + 0U] = g;
    s_grb[i * 3U + 1U] = r;
    s_grb[i * 3U + 2U] = b;
#endif
  }
}

#if APP_WS2812_USE_PWM_DMA

/** PB4 = TIM3_CH1 @ AF2 (Arduino_Core STM32L475 PeripheralPins.c) */
#define WS_TIM3_CH1_AF GPIO_AF2_TIM3

#define WS_PWM_RESET_SLOTS 80U
#define WS_PWM_BUF_LEN ((size_t)APP_WS2812_NUM_LEDS * 24U + WS_PWM_RESET_SLOTS)

DMA_HandleTypeDef hdma_ws2812;

static uint16_t s_pwm_buf[WS_PWM_BUF_LEN];
static volatile uint8_t s_ws_dma_busy = 0U;
static uint16_t s_ccr_bit0 = 28U;
static uint16_t s_ccr_bit1 = 56U;
static uint8_t s_pwm_ready = 0U;

void App_Ws2812_PwmDmaIrqHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_ws2812);
}

static void ws2812_dma_tc(DMA_HandleTypeDef *hdma)
{
  (void)hdma;
  TIM3->CR1 &= ~(TIM_CR1_CEN);
  TIM3->DIER &= ~(uint32_t)TIM_DIER_UDE;
  TIM3->CCR1 = 0U;
  s_ws_dma_busy = 0U;
}

static void ws2812_dma_err(DMA_HandleTypeDef *hdma)
{
  (void)hdma;
  TIM3->CR1 &= ~(TIM_CR1_CEN);
  TIM3->DIER &= ~(uint32_t)TIM_DIER_UDE;
  TIM3->CCR1 = 0U;
  s_ws_dma_busy = 0U;
}

static void ws_dma_wait_done(uint32_t timeout_ms)
{
  const uint32_t t0 = HAL_GetTick();
  while ((s_ws_dma_busy != 0U) && ((HAL_GetTick() - t0) < timeout_ms))
  {
    __NOP();
  }
  if (s_ws_dma_busy != 0U)
  {
    (void)HAL_DMA_Abort(&hdma_ws2812);
    TIM3->CR1 &= ~(TIM_CR1_CEN);
    TIM3->DIER &= ~(uint32_t)TIM_DIER_UDE;
    TIM3->CCR1 = 0U;
    s_ws_dma_busy = 0U;
  }
}

static void ws_build_pwm_buffer(size_t *out_len)
{
  size_t o = 0U;
  for (size_t bi = 0U; bi < WS_BUF_BYTES; bi++)
  {
    const uint8_t v = s_grb[bi];
    for (int bit = 7; bit >= 0; bit--)
    {
      s_pwm_buf[o++] = ((v >> (uint32_t)bit) & 1U) ? s_ccr_bit1 : s_ccr_bit0;
    }
  }
  for (size_t i = 0U; i < WS_PWM_RESET_SLOTS; i++)
  {
    s_pwm_buf[o++] = 0U;
  }
  *out_len = o;
}

static void ws_tim3_dma_hw_init(void)
{
  static uint8_t s_inited = 0U;
  if (s_inited != 0U)
  {
    return;
  }
  s_inited = 1U;
  s_pwm_ready = 0U;

  __HAL_RCC_TIM3_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();
#if defined(DMAMUX1)
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
#else
  SET_BIT(RCC->AHB1ENR, 0x00000004UL);
  (void)READ_BIT(RCC->AHB1ENR, 0x00000004UL);
#endif

  GPIO_InitTypeDef g = {0};
  g.Pin = GPIO_PIN_4;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = WS_TIM3_CH1_AF;
  HAL_GPIO_Init(GPIOB, &g);

  const uint32_t tim_hz = HAL_RCC_GetPCLK1Freq();
  uint32_t per = (tim_hz + 400000U) / 800000U;
  if (per < 32U)
  {
    per = 100U;
  }
  const uint16_t arr = (uint16_t)(per - 1U);
  const uint32_t pw = per;
  s_ccr_bit0 = (uint16_t)((pw * 35U + 62U) / 125U);
  s_ccr_bit1 = (uint16_t)((pw * 70U + 62U) / 125U);
  if (s_ccr_bit0 < 2U)
  {
    s_ccr_bit0 = 2U;
  }
  if (s_ccr_bit1 <= s_ccr_bit0)
  {
    s_ccr_bit1 = (uint16_t)(s_ccr_bit0 + 4U);
  }
  if ((uint32_t)s_ccr_bit1 > (uint32_t)arr)
  {
    s_ccr_bit1 = (uint16_t)((arr * 3U) / 4U);
    s_ccr_bit0 = (uint16_t)((arr * 3U) / 10U);
  }

  TIM3->CR1 = 0U;
  TIM3->PSC = 0U;
  TIM3->ARR = arr;
  TIM3->CCR1 = 0U;
  /* PWM1, no OC1PE — DMA writes CCR take effect without preload delay */
  TIM3->CCMR1 = (6U << TIM_CCMR1_OC1M_Pos);
  TIM3->CCER = TIM_CCER_CC1E;
  TIM3->EGR = TIM_EGR_UG;
  TIM3->CR1 = TIM_CR1_ARPE;
  TIM3->DIER = 0U;

  hdma_ws2812.Instance = DMA1_Channel3;
#if defined(DMAMUX1)
  hdma_ws2812.Init.Request = DMA_REQUEST_TIM3_UP;
#else
  hdma_ws2812.Init.Request = 5U;
#endif
  hdma_ws2812.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_ws2812.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_ws2812.Init.MemInc = DMA_MINC_ENABLE;
  hdma_ws2812.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_ws2812.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  hdma_ws2812.Init.Mode = DMA_NORMAL;
  hdma_ws2812.Init.Priority = DMA_PRIORITY_LOW;
  if (HAL_DMA_Init(&hdma_ws2812) != HAL_OK)
  {
    ws_bb_gpio_init();
    s_inited = 0U;
    return;
  }

  hdma_ws2812.XferCpltCallback = ws2812_dma_tc;
  hdma_ws2812.XferErrorCallback = ws2812_dma_err;

  HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
  s_pwm_ready = 1U;
}

static void ws_push_dma(void)
{
  if (s_pwm_ready == 0U)
  {
    ws_bb_push();
    return;
  }

  size_t n = 0U;
  ws_build_pwm_buffer(&n);
  if (n > WS_PWM_BUF_LEN)
  {
    n = WS_PWM_BUF_LEN;
  }

  ws_dma_wait_done(5U);

  TIM3->CR1 &= ~(TIM_CR1_CEN);
  TIM3->DIER &= ~(uint32_t)TIM_DIER_UDE;
  TIM3->CCR1 = 0U;

  s_ws_dma_busy = 1U;
  if (n < 2U)
  {
    s_ws_dma_busy = 0U;
    return;
  }
  TIM3->CCR1 = s_pwm_buf[0];
  if (HAL_DMA_Start_IT(&hdma_ws2812, (uint32_t)&s_pwm_buf[1], (uint32_t)&TIM3->CCR1, (uint32_t)(n - 1U)) != HAL_OK)
  {
    s_ws_dma_busy = 0U;
    return;
  }

  TIM3->EGR = TIM_EGR_UG;
  TIM3->DIER |= TIM_DIER_UDE;
  TIM3->CR1 |= TIM_CR1_CEN;
}

static void ws_fill_color(uint8_t g, uint8_t r, uint8_t b)
{
  ws_buf_fill_color(g, r, b);
  ws_push_dma();
}

void App_Ws2812_Init(void)
{
  ws_tim3_dma_hw_init();
  if (s_pwm_ready == 0U)
  {
    /* DMA/TIM init failed or skipped — already on GPIO from ws_tim3_dma_hw_init */
  }
  memset(s_grb, 0, sizeof(s_grb));
  ws_fill_color(0U, 0U, 0U);
}

#else /* !APP_WS2812_USE_PWM_DMA */

void App_Ws2812_PwmDmaIrqHandler(void) {}

static void ws_fill_color(uint8_t g, uint8_t r, uint8_t b)
{
  ws_buf_fill_color(g, r, b);
  ws_bb_push();
}

void App_Ws2812_Init(void)
{
  ws_bb_gpio_init();
  memset(s_grb, 0, sizeof(s_grb));
  ws_bb_push();
}

#endif /* APP_WS2812_USE_PWM_DMA */

void App_Ws2812_SetJudge(const char *kind)
{
  if (kind == NULL)
  {
    return;
  }
  if (strcmp(kind, "perfect") == 0)
  {
    ws_fill_color(255U, 0U, 0U);
  }
  else if (strcmp(kind, "good") == 0)
  {
    ws_fill_color(255U, 255U, 0U);
  }
  else if ((strcmp(kind, "miss") == 0) || (strcmp(kind, "bad") == 0))
  {
    ws_fill_color(0U, 255U, 0U);
  }
  else if (strcmp(kind, "off") == 0)
  {
    ws_fill_color(0U, 0U, 0U);
  }
  else
  {
    return;
  }
  s_hold_until_ms = HAL_GetTick() + APP_WS2812_HOLD_MS;
}

void App_Ws2812_Tick(uint32_t now_ms)
{
  if (s_hold_until_ms == 0U)
  {
    return;
  }
  if ((int32_t)(now_ms - s_hold_until_ms) >= 0)
  {
    s_hold_until_ms = 0U;
    ws_fill_color(0U, 0U, 0U);
  }
}

#else /* !APP_USE_WS2812_JDG */

void App_Ws2812_Init(void) {}
void App_Ws2812_SetJudge(const char *kind) { (void)kind; }
void App_Ws2812_Tick(uint32_t now_ms) { (void)now_ms; }
void App_Ws2812_PwmDmaIrqHandler(void) {}

#endif
