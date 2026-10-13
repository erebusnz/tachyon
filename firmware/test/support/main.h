#ifndef TEST_MAIN_H
#define TEST_MAIN_H
#include <stdint.h>

/* Host substitutes for only the hardware surface used by the IO tests. */
typedef struct { uint32_t CYCCNT, CTRL; } test_dwt_t;
typedef struct { uint32_t DEMCR; } test_debug_t;
extern test_dwt_t test_dwt;
extern test_debug_t test_debug;
#define DWT (&test_dwt)
#define CoreDebug (&test_debug)
#define CoreDebug_DEMCR_TRCENA_Msk 1u
#define DWT_CTRL_CYCCNTENA_Msk 1u
#define SystemCoreClock 168000000u
#define __disable_irq() ((void)0)
#define __enable_irq() ((void)0)

typedef struct {
  void *Instance;
  struct { uint32_t Channel, Direction, PeriphInc, MemInc;
    uint32_t PeriphDataAlignment, MemDataAlignment, Mode, Priority, FIFOMode;
  } Init;
} DMA_HandleTypeDef;
typedef struct { DMA_HandleTypeDef *DMA_Handle; } ADC_HandleTypeDef;
typedef struct {
  void *Instance;
  struct { uint32_t Prescaler, CounterMode, Period, ClockDivision;
    uint32_t RepetitionCounter, AutoReloadPreload;
  } Init;
} TIM_HandleTypeDef;
typedef struct { uint32_t MasterOutputTrigger, MasterSlaveMode; } TIM_MasterConfigTypeDef;
typedef struct { uint32_t CFGR; } test_rcc_t;
extern test_rcc_t test_rcc;
#define RCC (&test_rcc)
#define RCC_CFGR_PPRE2 0xE000u
#define DMA2_Stream0 ((void *)1)
#define TIM8 ((void *)2)
#define DMA2_Stream0_IRQn 56
#define DMA_CHANNEL_0 0
#define DMA_PERIPH_TO_MEMORY 0
#define DMA_PINC_DISABLE 0
#define DMA_MINC_ENABLE 1
#define DMA_PDATAALIGN_HALFWORD 1
#define DMA_MDATAALIGN_HALFWORD 1
#define DMA_CIRCULAR 1
#define DMA_PRIORITY_HIGH 2
#define DMA_FIFOMODE_DISABLE 0
#define TIM_COUNTERMODE_UP 0
#define TIM_CLOCKDIVISION_DIV1 0
#define TIM_AUTORELOAD_PRELOAD_DISABLE 0
#define TIM_TRGO_UPDATE 2
#define TIM_MASTERSLAVEMODE_DISABLE 0
#define HAL_OK 0
#define __HAL_RCC_DMA2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM8_CLK_ENABLE() ((void)0)
#define __HAL_LINKDMA(h, field, dma) ((h)->field = &(dma))
int HAL_DMA_Init(DMA_HandleTypeDef *dma);
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *dma);
void HAL_NVIC_SetPriority(int irq, int priority, int sub);
void HAL_NVIC_EnableIRQ(int irq);
uint32_t HAL_RCC_GetPCLK2Freq(void);
int HAL_TIM_Base_Init(TIM_HandleTypeDef *timer);
int HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef *timer, TIM_MasterConfigTypeDef *master);
int HAL_ADC_Start_DMA(ADC_HandleTypeDef *adc, uint32_t *buffer, uint32_t size);
int HAL_TIM_Base_Start(TIM_HandleTypeDef *timer);
uint32_t HAL_GetTick(void);
void Error_Handler(void);
#endif
