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
