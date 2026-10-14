#include "unity.h"
#include "../Core/IO/analog_in.c"

ADC_HandleTypeDef hadc1;
test_rcc_t test_rcc;
static uint32_t ticks;
static int dma_started;

int HAL_DMA_Init(DMA_HandleTypeDef *dma)
{
  TEST_ASSERT_EQUAL_INT(DMA_CIRCULAR, dma->Init.Mode);
  TEST_ASSERT_EQUAL_INT(DMA_MDATAALIGN_HALFWORD, dma->Init.MemDataAlignment);
  return HAL_OK;
}
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *dma) { (void)dma; }
void HAL_NVIC_SetPriority(int irq, int priority, int sub)
{ (void)irq; (void)sub; TEST_ASSERT_TRUE(priority > 8); }
void HAL_NVIC_EnableIRQ(int irq) { (void)irq; }
uint32_t HAL_RCC_GetPCLK2Freq(void) { return 84000000; }
int HAL_TIM_Base_Init(TIM_HandleTypeDef *timer)
{
  TEST_ASSERT_EQUAL_UINT32(999, timer->Init.Period);
  TEST_ASSERT_EQUAL_UINT32(test_rcc.CFGR ? 167 : 83, timer->Init.Prescaler);
  return HAL_OK;
}
int HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef *timer, TIM_MasterConfigTypeDef *master)
{ (void)timer; TEST_ASSERT_EQUAL_INT(TIM_TRGO_UPDATE, master->MasterOutputTrigger); return HAL_OK; }
int HAL_ADC_Start_DMA(ADC_HandleTypeDef *adc, uint32_t *buffer, uint32_t size)
{
  TEST_ASSERT_EQUAL_PTR(&hadc1, adc);
  TEST_ASSERT_EQUAL_PTR((void *)s_dma, buffer);
  TEST_ASSERT_EQUAL_UINT32(6, size);
  dma_started = 1;
  return HAL_OK;
}
int HAL_TIM_Base_Start(TIM_HandleTypeDef *timer)
{
  (void)timer;
  TEST_ASSERT_TRUE(dma_started);
  s_dma[0]=100; s_dma[1]=200; s_dma[2]=1000;
  HAL_ADC_ConvHalfCpltCallback(&hadc1);
  return HAL_OK;
}
uint32_t HAL_GetTick(void) { return ticks++; }
void Error_Handler(void) { TEST_FAIL_MESSAGE("Unexpected hardware initialization error"); }
void setUp(void) { ticks=0; dma_started=0; test_rcc.CFGR=RCC_CFGR_PPRE2; }
void tearDown(void) {}

static void test_acquisition_publishes_without_main_loop_service(void)
{
  analog_in_init();
  TEST_ASSERT_EQUAL_UINT16(1000, analog_in_pot()); /* primed, no boot ramp */
  for (int n=0; n<100; ++n) {
    unsigned offset = (n & 1) ? 0 : 3;
    s_dma[offset]=4095; s_dma[offset+1]=0; s_dma[offset+2]=2000;
    if (offset) HAL_ADC_ConvCpltCallback(&hadc1);
    else HAL_ADC_ConvHalfCpltCallback(&hadc1);
    TEST_ASSERT_EQUAL_UINT16(4095, analog_in_cv_code(ANALOG_CV_A));
    TEST_ASSERT_EQUAL_UINT16(0, analog_in_cv_code(ANALOG_CV_B));
    if (!n) TEST_ASSERT_EQUAL_UINT16(1125, analog_in_pot());
  }
  TEST_ASSERT_EQUAL_UINT32(101, analog_in_scan_count());
  TEST_ASSERT_UINT16_WITHIN(1, 2000, analog_in_pot());
  ADC_HandleTypeDef other = {0};
  HAL_ADC_ConvCpltCallback(&other);
  TEST_ASSERT_EQUAL_UINT32(101, analog_in_scan_count());
}
static void test_timer_clock_without_apb_divider(void)
{ test_rcc.CFGR=0; analog_in_init(); }

static void test_counter_wrap_does_not_reprime_pot_filter(void)
{
  analog_in_init();
  s_scans = UINT32_MAX;
  s_dma[3]=0; s_dma[4]=0; s_dma[5]=2000;
  HAL_ADC_ConvCpltCallback(&hadc1);
  TEST_ASSERT_EQUAL_UINT32(0, analog_in_scan_count());
  HAL_ADC_ConvCpltCallback(&hadc1);
  TEST_ASSERT_UINT16_WITHIN(1, 1234, analog_in_pot());
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_acquisition_publishes_without_main_loop_service);
  RUN_TEST(test_timer_clock_without_apb_divider);
  RUN_TEST(test_counter_wrap_does_not_reprime_pot_filter);
  return UNITY_END();
}
