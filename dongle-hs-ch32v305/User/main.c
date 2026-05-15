/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/06/06
 * Description        : Main program body.
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/
/*
 *@Note
 SPI使锟斤拷DMA锟斤拷Master/Slave 模式锟秸凤拷锟斤拷锟教ｏ�?
 Master锟斤拷SPI1_SCK(PA5)锟斤拷SPI1_MISO(PA6)锟斤拷SPI1_MOSI(PA7)锟斤�?
 Slave锟斤拷SPI1_SCK(PA5)锟斤拷SPI1_MISO(PA6)锟斤拷SPI1_MOSI(PA7)锟斤�?

 锟斤拷锟斤拷锟斤拷锟斤拷�? Master 锟斤�? Slave 同时使锟斤拷 DAM 全双锟斤拷锟秸凤拷锟斤拷
 注锟斤拷锟斤拷锟斤拷锟斤拷臃直锟斤拷锟斤拷锟??? Master 锟斤�? Slave 锟斤拷锟斤拷同时锟较电�?
     硬锟斤拷锟斤拷锟竭ｏ拷PA5 锟斤拷锟斤拷 PA5
           PA6 锟斤拷锟斤拷 PA6
           PA7 锟斤拷锟斤拷 PA7

*/

#include "debug.h"
#include "string.h"
#include "ch32v30x_usbhs_device.h"
#include "usbd_composite_km.h"
#include "ch32v30x_tim.h"
#include "ch32v30x_misc.h"
#include "spi_slave.h"
#include "ring_buffer.h"
#include <string.h>

/* SPI Communication Mode Selection */
// #define SPI_MODE   HOST_MODE
// #define SPI_MODE   SLAVE_MODE

// #define TEST_RATE
// #define RELEASE

// __attribute__ ((aligned(4))) uint8_t USBHS_EP3_TX_Buf[6] = {0x00};
// __attribute__ ((aligned(4))) uint8_t USBHS_EP3_TX_Buf[8] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
__attribute__ ((aligned(4))) uint8_t HID_Report_Buf[7] = {0};
__attribute__ ((aligned(4))) uint8_t rx_Buf[30] = {0};





extern volatile uint8_t spi_response_ready;
volatile uint8_t spi_response_ready = 0;

void print_buffer(uint8_t *buffer, uint16_t length)
{
    
    printf("%02x \r\n", buffer[3]);

    // printf("%02x %02x %02x %02x %02x %02x %02x\r\n", buffer[0], buffer[1],buffer[2],buffer[3],buffer[4], buffer[5], buffer[6]);

}

void EXTI20_INT_INIT(void)
{
    EXTI_InitTypeDef EXTI_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    EXTI_InitStructure.EXTI_Line = EXTI_Line20;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

}

volatile uint32_t g_usb_packet_count = 0;
volatile uint32_t g_usb_packet_frequency = 0;
volatile uint32_t g_time_elapsed_ms = 0;

/*********************************************************************
 * @fn      TIM3_Init
 *
 * @brief   Initialize timer3 for mouse scan.
 *
 * @param   arr - The specific period value
 *          psc - The specifies prescaler value
 *
 * @return  none
 */
void TIM3_Init( uint16_t arr, uint16_t psc )
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    /* Enable Timer3 Clock */
    RCC_APB1PeriphClockCmd( RCC_APB1Periph_TIM3, ENABLE );

    /* Initialize Timer3 */
    TIM_TimeBaseStructure.TIM_Period = arr;
    TIM_TimeBaseStructure.TIM_Prescaler = psc;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit( TIM3, &TIM_TimeBaseStructure );

    TIM_ITConfig( TIM3, TIM_IT_Update, ENABLE );

    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init( &NVIC_InitStructure );

    /* Enable Timer3 */
    TIM_Cmd( TIM3, ENABLE );
}

/*********************************************************************
 * @fn      TIM3_IRQHandler
 *
 * @brief   This function handles TIM3 global interrupt request.
 *
 * @return  none
 */
 void TIM3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM3_IRQHandler( void )
{
    if( TIM_GetITStatus( TIM3, TIM_IT_Update ) != RESET )
    {
        /* Clear interrupt flag */
        TIM_ClearITPendingBit( TIM3, TIM_IT_Update );

	    
        if( USBHS_DevEnumStatus)
	    {
                        // uint8_t r1[7] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                        //  RB_Push(&packetVal, r1);
            printf("usb count: %d\r\n", g_usb_packet_count);
            g_usb_packet_count = 0;
        //     /* 1. Ingest SPI Data */
        //     if (UserConfigReceived)
        //     {
        //         UserConfigReceived = 0; // Clear flag
        //         SPI_Update_Tx_Data(UserConfigData, FEATURE_REPORT_SIZE);
        //         Delay_Us(50);
        //         CH305_Signal_DataReady();
        //         spi_response_ready = 0;
        //     }
        
            // if(new_rx_data) {
            //     uint8_t* src = NULL;
            //     if (active_rx_buffer == 1) {
            //         src = &DoubleRxData[0];
            //     } else if (active_rx_buffer == 2) {
            //         src = &DoubleRxData[Size];
            //     }

            //     if (src != NULL) {
            //         uint8_t report_id = src[0];

            //         if (report_id == REPORT_ID_DUAL_MOUSE) {
            //              // Dual Report: Split into two standard Mouse Reports (ID 0x01)
            //              uint8_t r1[7];
            //              r1[0] = REPORT_ID_MOUSE; // Convert to standard ID
            //              memcpy(&r1[1], &src[1], 6);
            //              RB_Push(&packetVal, r1);

            //              uint8_t r2[7];
            //              r2[0] = REPORT_ID_MOUSE; // Convert to standard ID
            //              memcpy(&r2[1], &src[7], 6);
            //              RB_Push(&packetVal, r2);
            //         } else if (report_id == REPORT_ID_MOUSE) {
            //              // Single Report: Push directly
            //              // Ensure size is correct (7 bytes)
            //              RB_Push(&packetVal, src);
            //         } else if (report_id == REPORT_ID_USER_CONFIG) {
            //              // User Config
            //              memcpy((void*)UserConfigData, src, FEATURE_REPORT_SIZE);
            //              spi_response_ready = 1;
            //         } 
            //         // Other IDs ignored or handled elsewhere
            //     }
            //     new_rx_data = 0;
            //     #ifdef TEST_RATE
            //     g_usb_packet_count++;
            //     #endif
            // }

        //     /* 2. Egest to USB from Ring Buffer */
        //     if (!RB_IsEmpty(&packetVal)) {
        //         if (USBHS_Endp_Busy[DEF_UEP3] == 0) {
        //             // Use static buffer for DMA safety
        //             // Zero-Copy: Use pointer directly
        //             uint8_t *p = RB_Pop(&packetVal);
        //             if (p != NULL) {
        //                 USBHS_Endp_DataUp( DEF_UEP3, p, 7, DEF_UEP_DMA_LOAD );
        //             }
        //         }

                    // if( ( USBHS_Endp_Busy[ DEF_UEP3 ] & DEF_UEP_BUSY ) == 0x00 ) {

                    //         // uint8_t test[MOUSE_REPORT_SIZE] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                    //         // memcpy( USBHSD_UEP_TXDMA(DEF_UEP3), (uint8_t*)test, MOUSE_REPORT_SIZE );
                    //         uint8_t* send_buf = NULL;
                    //         if (active_rx_buffer == 1) {
                    //             send_buf = &USBHS_EP3_TX_Buf[0];
                    //         } else if (active_rx_buffer == 2){
                    //             send_buf = &USBHS_EP3_TX_Buf[MOUSE_REPORT_SIZE];
                    //         }
                    //         USBHSD_UEP_TXDMA(DEF_UEP3) = (uint32_t)(uint8_t*)send_buf;
                    //         /* endpoint n response tx ack */
                    //         USBHSD_UEP_TLEN( DEF_UEP3 ) = MOUSE_REPORT_SIZE;
                    //         USBHSD_UEP_TXCTRL( DEF_UEP3 ) = ( USBHSD_UEP_TXCTRL( DEF_UEP3 ) &= ~USBHS_UEP_T_RES_MASK ) | USBHS_UEP_T_RES_ACK;
                    //         USBHS_Endp_Busy[ DEF_UEP3 ] |= DEF_UEP_BUSY;
                    //         new_rx_data=0;
                    // }
            // }
	    }
    }
}


void GPIO_PC8_Output_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    
    // printf("Initializing PC8 as Push-Pull Output...\r\n");
    
    // 使锟斤拷GPIOC时锟接ｏ拷PC8锟斤拷GPIOC锟剿口ｏ�?
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    
    // 锟斤拷锟斤拷PC8锟斤拷锟斤拷
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;            // PC8
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;     // 锟斤拷锟斤拷锟斤拷锟侥ｏ�??
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;    // 锟斤拷锟斤拷俣锟??50MHz
    
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    
    // 锟斤拷锟矫筹拷始状态为锟酵碉拷�?
    GPIO_ResetBits(GPIOC, GPIO_Pin_5);
    
    // printf("PC8 initialized as Push-Pull Output.\r\n");
}

/**
 * @brief 锟斤拷锟斤拷PC8锟斤拷锟斤拷叩锟斤拷?
 */
void PC8_Set_High(void)
{
    GPIO_SetBits(GPIOC, GPIO_Pin_8);
    printf("PC8 set to HIGH\r\n");
}

/**
 * @brief 锟斤拷锟斤拷PC8锟斤拷锟斤拷偷锟斤拷?
 */
void PC8_Set_Low(void)
{
    GPIO_ResetBits(GPIOC, GPIO_Pin_8);
    // printf("PC8 set to LOW\r\n");
}

void CH305_Signal_DataReady(void)
{
    // Pulse High
    GPIO_SetBits(GPIOC, GPIO_Pin_8);
    
    // Optional: Small delay if needed by nRF to detect the edge, 
    // but usually CPU cycles are enough for a rising edge.
    Delay_Us(1); 

    // Return to Low
    GPIO_ResetBits(GPIOC, GPIO_Pin_8);
}

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	SystemCoreClockUpdate();

    Delay_Init();
    // Delay_Ms(10000);
    #ifndef RELEASE
    USART_Printf_Init(115200);
    printf("SystemClk:%d\r\n", SystemCoreClock);
    printf( "ChipID:%08x\r\n", DBGMCU_GetCHIPID() );
    printf("Slave Mode\r\n");
    #endif
	USBHS_RCC_Init( );
	USBHS_Device_Init( ENABLE );
    USB_Sleep_Wakeup_CFG();
    SPI_FullDuplex_Init();
    DMA_Rx_Init(DMA1_Channel4, (u32)&SPI2->DATAR, (u32)DoubleRxData, Size*2);
    DMA_Cmd(DMA1_Channel4, ENABLE);
    DMA_Tx_Init(DMA1_Channel5, (u32)&SPI2->DATAR, (u32)TxData, Size*2);
    DMA_Cmd(DMA1_Channel5, ENABLE);
    GPIO_PC8_Output_Init();
    RB_Init(&packetVal);
    Delay_Us(10);
 
    // 1ms = (9999 + 1) * (9599 + 1) / 96,000,000
    // TIM2_Init(9999, 9599);
    // (11999 + 1) * (0 + 1) / 96,000,000 = 12000 / 96,000,000 = 0.000125锟斤�? = 125锟斤拷s
    // TIM2_Init(11999, 0);
    // TIM3_Init(11999, 0); // 8Khz
    // TIM3_Init(5999, 0);  // 16KHz
    TIM3_Init(65535, 1464);  // 1Hz
    // TIM2_Init(124, 95);

    while(1)
    {
        // if (UserConfigReceived)
        // {
        //     UserConfigReceived = 0; // Clear flag
        //     // printf("Received User Config :\r\n");
            
        //     // Print the received data (Buffer size is 32, but payload is 29)
        //     // Note: index 0 might be Report ID if sent by host, otherwise it's data
        //     // Usually Setup Packet wValue has ID, data stage is payload.
        //     // for (int i = 0; i < FEATURE_REPORT_SIZE; i++)
        //     // {
        //     //     printf("%02x ", UserConfigData[i]);
        //     // }
        //     // printf("\r\n");

        //     SPI_Update_Tx_Data(UserConfigData, FEATURE_REPORT_SIZE);
        //     Delay_Us(50);
        //     CH305_Signal_DataReady();
        //     spi_response_ready = 0;
        //     // SPI2_DMA_Transceive(UserConfigData, rx_Buf, 30);
        //     // SPI2_DMA_Send(TxData, FEATURE_REPORT_SIZE);

        //     // TODO: Apply configuration to your SPI device or global settings here
        // }
        
        if( USBHS_DevEnumStatus)
	    {
            /* 1. Ingest SPI Data */
            if (UserConfigReceived)
            {
                UserConfigReceived = 0; // Clear flag
                SPI_Update_Tx_Data(UserConfigData, FEATURE_REPORT_SIZE);
                Delay_Us(50);
                CH305_Signal_DataReady();
                spi_response_ready = 0;
            }
        
            // if(new_rx_data) {
            //     uint8_t* src = NULL;
            //     if (active_rx_buffer == 1) {
            //         src = &DoubleRxData[0];
            //     } else if (active_rx_buffer == 2) {
            //         src = &DoubleRxData[Size];
            //     }

            //     if (src != NULL) {
            //         uint8_t report_id = src[0];

            //         if (report_id == REPORT_ID_DUAL_MOUSE) {
            //              // Dual Report: Split into two standard Mouse Reports (ID 0x01)
            //             //  uint8_t r1[7];
            //             //  r1[0] = REPORT_ID_MOUSE; // Convert to standard ID
            //             //  memcpy(&r1[1], &src[1], 6);
            //             //  RB_Push(&packetVal, r1);

            //             //  uint8_t r2[7];
            //             //  r2[0] = REPORT_ID_MOUSE; // Convert to standard ID
            //             //  memcpy(&r2[1], &src[7], 6);
            //             //  RB_Push(&packetVal, r2);
            //         } else if (report_id == REPORT_ID_MOUSE) {
            //              // Single Report: Push directly
            //              // Ensure size is correct (7 bytes)
            //             //  RB_Push(&packetVal, src);
            //         } else if (report_id == REPORT_ID_USER_CONFIG) {
            //              // User Config
            //              memcpy((void*)UserConfigData, src, FEATURE_REPORT_SIZE);
            //              spi_response_ready = 1;
            //         } 
            //         // Other IDs ignored or handled elsewhere
            //     }
            //     new_rx_data = 0;
            //     #ifdef TEST_RATE
            //     g_usb_packet_count++;
            //     #endif
            // }
	    }
    }
}
