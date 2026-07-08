/******************************************************************************
**************************Hardware interface layer*****************************
* | file      		:	DEV_Config.c
* |	version			:	V1.0
* | date			:	2020-06-17
* | function		:	Provide the hardware underlying interface
******************************************************************************/
#include "DEV_Config.h"
#include <stdio.h>

extern SPI_HandleTypeDef hspi1;

/********************************************************************************
function:	System Init
********************************************************************************/
uint8_t System_Init(void)
{
#if USE_SPI_4W
	printf("USE_SPI_4W\r\n");
#endif
  return 0;
}

void System_Exit(void)
{

}

/********************************************************************************
function:	Hardware interface
********************************************************************************/
uint8_t SPI4W_Write_Byte(uint8_t value)
{
    HAL_SPI_Transmit(&hspi1, &value, 1, HAL_MAX_DELAY);
    return 0;
}

/* Bulk transfer: one HAL call for the whole buffer. The caller drives DC/CS
 * around it. A full 128x128 4-bit frame (8 KB) goes out in one transfer
 * instead of 8192 per-byte HAL calls with CS/DC toggles around each. */
void SPI4W_Write_nByte(const uint8_t *buf, uint16_t len)
{
    HAL_SPI_Transmit(&hspi1, (uint8_t *)buf, len, HAL_MAX_DELAY);
}

/* ---- non-blocking bulk transfer (DMA2 Stream3) ----
 * The frame blit must not stall the main loop: even the single-call blocking
 * transfer costs ~7-23 ms per frame (build-dependent), which quantizes arp
 * steps and gate edges. With DMA the blit costs ~0; CS is released from the
 * transfer-complete IRQ. The caller must hold `buf` stable and not issue
 * other SPI1 traffic until idle (see SPI4W_Wait_Idle). */
static volatile uint8_t s_spi1_dma_busy;

void SPI4W_Wait_Idle(void)
{
    while (s_spi1_dma_busy) { }
}

uint8_t SPI4W_Write_DMA(const uint8_t *buf, uint16_t len)
{
    s_spi1_dma_busy = 1;
    if (HAL_SPI_Transmit_DMA(&hspi1, (uint8_t *)buf, len) != HAL_OK) {
        s_spi1_dma_busy = 0;
        return 1;                    /* caller falls back to blocking write */
    }
    return 0;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) {
        /* DMA done != wire done: wait out the last byte (<1 us at 10.5 MHz)
         * before releasing CS. */
        while (__HAL_SPI_GET_FLAG(hspi, SPI_FLAG_BSY)) { }
        OLED_CS_1;
        s_spi1_dma_busy = 0;
    }
}

void I2C_Write_Byte(uint8_t value, uint8_t Cmd)
{
    // Not used — SPI only
}

/********************************************************************************
function:	Delay function
********************************************************************************/
void Driver_Delay_ms(uint32_t xms)
{
    HAL_Delay(xms);
}

void Driver_Delay_us(uint32_t xus)
{
    int j;
    for(j=xus; j > 0; j--);
}
