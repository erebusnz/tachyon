#include "analog_in.h"
#include "main.h"

extern ADC_HandleTypeDef hadc1;

/* TIM8 TRGO starts one three-rank scan every millisecond. DMA2 Stream0,
 * channel 0 drains each conversion immediately (unlike polling a scan).
 * Each DMA half holds one complete scan. This buffer must stay in SRAM,
 * because DMA cannot access CCM. */
static volatile uint16_t s_dma[6];
static DMA_HandleTypeDef s_adc_dma;
static TIM_HandleTypeDef s_adc_timer;
static volatile uint16_t s_latest[3];
static volatile uint32_t s_scans;
static uint32_t s_pot_q8;
static uint8_t s_primed;

static void publish_scan(unsigned offset)
{
  s_latest[0] = s_dma[offset];
  s_latest[1] = s_dma[offset + 1];
  uint32_t pot = (uint32_t)s_dma[offset + 2] << 8;
  if (!s_primed) { s_pot_q8 = pot; s_primed = 1; }
  else s_pot_q8 = (uint32_t)((int32_t)s_pot_q8 +
                           ((int32_t)pot - (int32_t)s_pot_q8) / 8);
  s_latest[2] = (uint16_t)((s_pot_q8 + 128) >> 8);
  ++s_scans;
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *adc)
{
  if (adc == &hadc1) publish_scan(0);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc)
{
  if (adc == &hadc1) publish_scan(3);
}

void analog_in_dma_irq(void)
{
  HAL_DMA_IRQHandler(&s_adc_dma);
}

uint16_t analog_in_cv_code(uint8_t ch)
{
  return s_latest[ch == ANALOG_CV_B ? 1 : 0];
}

float analog_in_cv_volts(uint8_t ch)
{
  /* cv-input.md: inverting map, V_in = (1.663 - Vadc) / 0.330. */
  float vadc = (float)analog_in_cv_code(ch) * 3.3f / 4095.0f;
  return (1.663f - vadc) / 0.330f;
}

uint16_t analog_in_pot(void)
{
  return s_latest[2];
}

uint32_t analog_in_scan_count(void)
{
  return s_scans;
}

void analog_in_init(void)
{
  s_scans = 0;
  s_primed = 0;
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_TIM8_CLK_ENABLE();

  s_adc_dma.Instance = DMA2_Stream0;
  s_adc_dma.Init.Channel = DMA_CHANNEL_0;
  s_adc_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
  s_adc_dma.Init.PeriphInc = DMA_PINC_DISABLE;
  s_adc_dma.Init.MemInc = DMA_MINC_ENABLE;
  s_adc_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  s_adc_dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  s_adc_dma.Init.Mode = DMA_CIRCULAR;
  s_adc_dma.Init.Priority = DMA_PRIORITY_HIGH;
  s_adc_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&s_adc_dma) != HAL_OK) Error_Handler();
  __HAL_LINKDMA(&hadc1, DMA_Handle, s_adc_dma);
  /* Short integer-only publication; audio refill (priority 8) preempts it. */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 9, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

  /* APB2 timers run at twice PCLK2 when the APB prescaler is not 1. */
  uint32_t timer_hz = HAL_RCC_GetPCLK2Freq();
  if ((RCC->CFGR & RCC_CFGR_PPRE2) != 0) timer_hz *= 2;
  s_adc_timer.Instance = TIM8;
  s_adc_timer.Init.Prescaler = timer_hz / 1000000u - 1u;
  s_adc_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
  s_adc_timer.Init.Period = 999;
  s_adc_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  s_adc_timer.Init.RepetitionCounter = 0;
  s_adc_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&s_adc_timer) != HAL_OK) Error_Handler();
  TIM_MasterConfigTypeDef master = {0};
  master.MasterOutputTrigger = TIM_TRGO_UPDATE;
  master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&s_adc_timer, &master) != HAL_OK)
    Error_Handler();

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)(void *)s_dma, 6) != HAL_OK)
    Error_Handler();
  if (HAL_TIM_Base_Start(&s_adc_timer) != HAL_OK) Error_Handler();

  /* Prime accessors before app_init. Startup only, never in the render IRQ. */
  uint32_t start = HAL_GetTick();
  while (!s_scans) {
    if (HAL_GetTick() - start >= 10u) Error_Handler();
  }
}
