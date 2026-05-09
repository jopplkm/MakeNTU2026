/**
 * WS2812 / NeoPixel feedback for PC judgement lines (jdg:perfect|good|miss).
 * Data pin: APP_WS2812_GPIO_* in main.h (default ARD_D5 / PB4).
 */
#ifndef APP_WS2812_H
#define APP_WS2812_H

#include <stdint.h>

void App_Ws2812_Init(void);
/** kind: "perfect" (green), "good" (yellow), "miss" (red), "off" */
void App_Ws2812_SetJudge(const char *kind);
void App_Ws2812_Tick(uint32_t now_ms);
/** DMA1_Channel5 ISR when APP_WS2812_USE_PWM_DMA is 1 */
void App_Ws2812_PwmDmaIrqHandler(void);

#endif
