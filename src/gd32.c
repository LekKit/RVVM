/*
gd32.c - Giga Device RISC-V microcontroller
Copyright (C) 2021  Mr0maks <mr.maks0443@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#if 0
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "riscv.h"
#include "riscv32.h"

void flash_emulate(rvvm_hart_t *vm, uint32_t operation, uint32_t addr, uint32_t *data)
{
        printf("%u %u %u\n", operation, addr, *data);
}

static memory_map_t map[] =
{
        {0xA0000000, 0xA0000FFF, "EXMC_SWREG", NULL},
/*
        0x90000000 -0x9FFFFFFF Reserved
        0x70000000 -0x8FFFFFFF Reserved
*/
        {0x60000000, 0x6FFFFFFF, "EXMC -NOR/PSRAM/SRAM", NULL},
        {0x50000000, 0x5003FFFF, "USBFS", NULL},
/*
        0x40080000 -0x4FFFFFFF Reserved
        0x40040000 -0x4007FFFF Reserved
        0x4002BC00 -0x4003FFFF Reserved
        0x4002B000 -0x4002BBFF Reserved
        0x4002A000 -0x4002AFFF Reserved
        0x40028000 -0x40029FFF Reserved
        0x40026800 -0x40027FFF Reserved
        0x40026400 -0x400267FF Reserved
        0x40026000 -0x400263FF Reserved
        0x40025000 -0x40025FFF Reserved
        0x40024000 -0x40024FFF Reserved
        0x40023C00 -0x40023FFF Reserved
        0x40023800 -0x40023BFF Reserved
        0x40023400 -0x400237FF Reserved
*/
        {0x40023000, 0x400233FF, "CRC", NULL},
/*
        0x40022C00 - 0x40022FFF Reserved
        0x40022800 - 0x40022BFF Reserved
        0x40022400 - 0x400227FF Reserved
*/
        {0x40022000, 0x400223FF, "FMC", NULL},
/*
        0x40021C00 - 0x40021FFF Reserved
        0x40021800 - 0x40021BFF Reserved
        0x40021400 - 0x400217FF Reserved
*/
        {0x40021000, 0x400213FF, "RCU", NULL},
/*
        0x40020C00 - 0x40020FFF Reserved
        0x40020800 - 0x40020BFF Reserved
*/
        {0x40020400, 0x400207FF, "DMA1", NULL},
        {0x40020000, 0x400203FF, "DMA0", NULL},
/*
        0x40018400 - 0x4001FFFF Reserved
        0x40018000 - 0x400183FF Reserved
*/
/*
        0x40017C00 - 0x40017FFF Reserved
        0x40017800 - 0x40017BFF Reserved
        0x40017400 - 0x400177FF Reserved
        0x40017000 - 0x400173FF Reserved
        0x40016C00 - 0x40016FFF Reserved
        0x40016800 - 0x40016BFF Reserved
        0x40015C00 - 0x400167FF Reserved
        0x40015800 - 0x40015BFF Reserved
        0x40015400 - 0x400157FF Reserved
        0x40015000 - 0x400153FF Reserved
        0x40014C00 - 0x40014FFF Reserved
        0x40014800 - 0x40014BFF Reserved
        0x40014400 - 0x400147FF Reserved
        0x40014000 - 0x400143FF Reserved
        0x40013C00 - 0x40013FFF Reserved
*/
        {0x40013800, 0x40013BFF, "USART0", NULL},
/*
        0x40013400 - 0x400137FF Reserved
*/
        {0x40013000, 0x400133FF, "SPI0", NULL},

        {0x40012C00, 0x40012FFF, "TIMER0", NULL},
        {0x40012800, 0x40012BFF, "ADC1", NULL},
        {0x40012400, 0x400127FF, "ADC0", NULL},
/*
        0x40012000 - 0x400123FF Reserved
        0x40011C00 - 0x40011FFF Reserved
*/
        {0x40011800, 0x40011BFF, "GPIOE", NULL},
        {0x40011400, 0x400117FF, "GPIOD", NULL},
        {0x40011000, 0x400113FF, "GPIOC", NULL},
        {0x40010C00, 0x40010FFF, "GPIOB", NULL},
        {0x40010800, 0x40010BFF, "GPIOA", NULL},
        {0x40010400, 0x400107FF, "EXTI", NULL},
        {0x40010000, 0x400103FF, "AFIO", NULL},
/*
        0x4000CC00 - 0x4000FFFF Reserved
        0x4000C800 - 0x4000CBFF Reserved
        0x4000C400 - 0x4000C7FF Reserved
        0x4000C000 - 0x4000C3FF Reserved
        0x40008000 - 0x4000BFFF Reserved
        0x40007C00 - 0x40007FFF Reserved
        0x40007800 - 0x40007BFF Reserved
*/
        {0x40007400, 0x400077FF, "DAC", NULL},
        {0x40007000, 0x400073FF, "PMU", NULL},
        {0x40006C00, 0x40006FFF, "BKP", NULL},

        {0x40006800, 0x40006BFF, "CAN1", NULL},
        {0x40006400, 0x400067FF, "CAN0", NULL},
        {0x40006000, 0x400063FF, "Shared USB/CAN SRAM 512bytes", NULL},
        {0x40005C00, 0x40005FFF, "USB device FS registers", NULL},
        {0x40005800, 0x40005BFF, "I2C1", NULL},
        {0x40005400, 0x400057FF, "I2C0", NULL},
        {0x40005000, 0x400053FF, "UART4", NULL},
        {0x40004C00, 0x40004FFF, "UART3", NULL},
        {0x40004800, 0x40004BFF, "USART2", NULL},
        {0x40004400, 0x400047FF, "USART1", NULL},
/*
        0x40004000 - 0x400043FF Reserved
*/
        {0x40003C00, 0x40003FFF, "SPI2/I2S2", NULL},
        {0x40003800, 0x40003BFF, "SPI1/I2S1", NULL},
/*
        0x40003400 - 0x400037FF Reserved
*/
        {0x40003000, 0x400033FF, "FWDGT", NULL},
        {0x40002C00, 0x40002FFF, "WWDGT", NULL},
        {0x40002800, 0x40002BFF, "RTC", NULL},
/*
        0x40002400 - 0x400027FF Reserved
        0x40002000 - 0x400023FF Reserved
        0x40001C00 - 0x40001FFF Reserved
        0x40001800 - 0x40001BFF Reserved
*/
        {0x40001400, 0x400017FF, "TIMER6", NULL},
        {0x40001000, 0x400013FF, "TIMER5", NULL},
        {0x40000C00, 0x40000FFF, "TIMER4", NULL},
        {0x40000800, 0x40000BFF, "TIMER3", NULL},
        {0x40000400, 0x400007FF, "TIMER2", NULL},
        {0x40000000, 0x400003FF, "TIMER1", NULL},
/*
        0x20070000 - 0x3FFFFFFF Reserved
        0x20060000 - 0x2006FFFF Reserved
        0x20030000 - 0x2005FFFF Reserved
        0x20020000 - 0x2002FFFF Reserved
        0x2001C000 - 0x2001FFFF Reserved
        0x20018000 - 0x2001BFFF Reserved
*/
        {0x20000000, 0x20017FFF, "SRAM", NULL},
/*
        0x1FFFF810 - 0x1FFFFFFF Reserved
*/
        {0x1FFFF800, 0x1FFFF80F, "Option Bytes", NULL},
        {0x1FFFB000, 0x1FFFF7FF, "Bootloader", NULL},
/*
         0x1FFF7A10 - 0x1FFFAFFF Reserved
         0x1FFF7800 - 0x1FFF7A0F Reserved
         0x1FFF0000 - 0x1FFF77FF Reserved
         0x1FFEC010 - 0x1FFEFFFF Reserved
         0x1FFEC000 - 0x1FFEC00F Reserved
         0x10010000 - 0x1FFEBFFF Reserved
         0x10000000 - 0x1000FFFF Reserved
         0x083C0000 - 0x0FFFFFFF Reserved
         0x08300000 - 0x083BFFFF Reserved
         0x08020000 - 0x082FFFFF Reserved
*/
        // Main Flash or bootloader
        {0x08000000, 0x0801FFFF, "Flash", NULL},
/*
        0x00300000 - 0x07FFFFFF Reserved
*/
        {0x00000000, 0x002FFFFF, "Flash", NULL},
        {0xDEADBEEF, 0xDEADF00D, NULL, NULL}
};


void gd32_prepare_memory_map(rvvm_hart_t *vm)
{
    vm->memory_map = map;
}

#endif
