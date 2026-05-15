#include "ch32v30x_gpio.h"
#include "spi_slave.h"
#include "debug.h"
#include "ring_buffer.h"
#include "ch32v30x_usbhs_device.h"

__attribute__ ((aligned(4))) uint8_t volatile TxData[Size * 2] = {0xAA};
__attribute__ ((aligned(4))) uint8_t volatile DoubleRxData[Size * 2] = {0x00};

volatile uint8_t new_rx_data = 0; 
volatile uint8_t active_rx_buffer = 0; 
volatile uint8_t active_tx_buffer = 1; 

// Internal function declaration
void SPI_NSS_EXTI_Init(void);

/*********************************************************************
 * @fn      SPI_FullDuplex_Init
 *
 * @brief   Configuring the SPI for full-duplex communication.
 *
 * @return  none
 */
void SPI_FullDuplex_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    SPI_InitTypeDef  SPI_InitStructure = {0};

    // RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_SPI1, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

// #if(SPI_MODE == HOST_MODE)
//     // PB12 (NSS)   -> �����������������Ƭ�?
//     GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
//     GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
//     GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
//     GPIO_Init(GPIOB, &GPIO_InitStructure);
//     GPIO_SetBits(GPIOB, GPIO_Pin_12); // ��ʼ״̬��Ƭѡ��Ч���ߵ�ƽ��

//     // PB13 (SCK)   -> ���������������������ʱ��???
//     GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
//     GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
//     GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
//     GPIO_Init(GPIOB, &GPIO_InitStructure);

//     // PB14 (MISO)  -> �������룬�������մӻ�����
//     GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14;
//     GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
//     GPIO_Init(GPIOB, &GPIO_InitStructure);

//     // PB15 (MOSI)  -> ������������������������ݸ��ӻ�???
//     GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;
//     GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
//     GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
//     GPIO_Init(GPIOB, &GPIO_InitStructure);

// #elif(SPI_MODE == SLAVE_MODE)
    // Slave:SPI1_SCK(PA5)\SPI1_MISO(PA6)\SPI1_MOSI(PA7).
    // ����SPI2����: NSS, SCK, MOSI, MISO
    // PB12 (NSS)  ��Ϊ���룬����������
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    // GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;  // this is for ncs software
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;  // this is for ncs hardware

    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // PB13 (SCK)  ��Ϊ���룬ʱ���������ṩ
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // PB14 (MISO) ��Ϊ������ӻ��������ݸ�����???
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // PB15 (MOSI) ��Ϊ���룬�����������ݸ��ӻ�
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

// #endif

    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;

// #if(SPI_MODE == HOST_MODE)
//     SPI_InitStructure.SPI_Mode = SPI_Mode_Master;

// #elif(SPI_MODE == SLAVE_MODE)
    SPI_InitStructure.SPI_Mode = SPI_Mode_Slave;

// #endif

    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    // SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Hard;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;  // (96000000 / 6)    // 12 MHz
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(SPI2, &SPI_InitStructure);

// #if(SPI_MODE == HOST_MODE)
    // SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
    // SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Rx, ENABLE);

// #endif

// #if(SPI_MODE == SLAVE_MODE)
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Rx, ENABLE);

// #endif

    SPI_Cmd(SPI2, ENABLE);

    // Initialize the EXTI on NSS pin to handle synchronization
    // SPI_NSS_EXTI_Init();
}

/*********************************************************************
 * @fn      SPI_NSS_EXTI_Init
 *
 * @brief   Initializes EXTI for PB12 (NSS) to reset DMA on Rising Edge
 *
 * @return  none
 */
void SPI_NSS_EXTI_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    EXTI_InitTypeDef EXTI_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* GPIOA ----> EXTI_Line0 */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource12);
    EXTI_InitStructure.EXTI_Line = EXTI_Line0;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/*********************************************************************
 * @fn      EXTI15_10_IRQHandler
 *
 * @brief   This function handles PB12 (NSS) interrupt.
 *
 * @return  none
 */
void EXTI0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI0_IRQHandler(void)
{
    // printf("nss interupt \r\n");
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        // 1. Clear the EXTI Line 12 Pending Bit
        EXTI_ClearITPendingBit(EXTI_Line0);
        
        // 2. Rising Edge Detected (NSS went High -> Transaction Ended)
        // We must reset the DMA RX channel to index 0 to align the next packet.
        
        // Disable DMA Channel
        DMA_Cmd(DMA1_Channel4, DISABLE);
        
        // Reload the counter to the full buffer size
        // This resets the pointer to the beginning of DoubleRxData
        DMA1_Channel4->CNTR = Size * 2; 
        
        // Clear any pending DMA flags that might have triggered spuriously
        DMA_ClearFlag(DMA1_FLAG_TC4 | DMA1_FLAG_HT4 | DMA1_FLAG_TE4);
        DMA_ClearITPendingBit(DMA1_IT_TC4 | DMA1_IT_HT4 | DMA1_IT_TE4);

        // Re-enable DMA Channel for the next transaction
        DMA_Cmd(DMA1_Channel4, ENABLE);
    }
}


/*********************************************************************
 * @fn      DMA_Tx_Init
 *
 * @brief   Initializes the DMAy Channelx configuration.
 *
 * @param   DMA_CHx - x can be 1 to 7.
 *          ppadr - Peripheral base address.
 *          memadr - Memory base address.
 *          bufsize - DMA channel buffer size.
 *
 * @return  none
 */
void DMA_Tx_Init(DMA_Channel_TypeDef *DMA_CHx, u32 ppadr, u32 memadr, u16 bufsize)
{
    DMA_InitTypeDef   DMA_InitStructure = {0};
    NVIC_InitTypeDef  NVIC_InitStructure={0};

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(DMA_CHx);

    DMA_InitStructure.DMA_PeripheralBaseAddr = ppadr;
    DMA_InitStructure.DMA_MemoryBaseAddr = memadr;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = bufsize;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    // DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA_CHx, &DMA_InitStructure);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_EnableIRQ(DMA1_Channel5_IRQn);

    DMA_ITConfig(DMA1_Channel5, DMA_IT_HT, ENABLE);
	DMA_ClearFlag(DMA1_FLAG_HT5);

    DMA_ITConfig(DMA1_Channel5, DMA_IT_TC, ENABLE);
    DMA_ITConfig(DMA1_Channel5, DMA_IT_TE, ENABLE);
	DMA_ClearFlag(DMA1_FLAG_TC5 | DMA1_FLAG_TE5);

    DMA_Cmd(DMA1_Channel5 , ENABLE); //ʹ��DMA
}

/*********************************************************************
 * @fn      DMA_Rx_Init
 *
 * @brief   Initializes the DEF_UEP31 DMA Channelx configuration.
 *
 * @param   DMA_CHx - x can be 1 to 7.
 *          ppadr - Peripheral base address.
 *          memadr - Memory base address.
 *          bufsize - DMA channel buffer size.
 *
 * @return  none
 */
void DMA_Rx_Init(DMA_Channel_TypeDef *DMA_CHx, u32 ppadr, u32 memadr, u16 bufsize)
{
    DMA_InitTypeDef DMA_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure={0};

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(DMA_CHx);

    DMA_InitStructure.DMA_PeripheralBaseAddr = ppadr;
    DMA_InitStructure.DMA_MemoryBaseAddr = memadr;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = bufsize;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    // DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA_CHx, &DMA_InitStructure);


    // new added for rx dma irq channel
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_EnableIRQ(DMA1_Channel4_IRQn);

    DMA_ITConfig(DMA1_Channel4, DMA_IT_TC, ENABLE);  
    DMA_ITConfig(DMA1_Channel4, DMA_IT_HT, ENABLE);  
    DMA_ITConfig(DMA1_Channel4, DMA_IT_TE, ENABLE);
	DMA_ClearFlag(DMA1_FLAG_TC4 | DMA1_FLAG_TE4 | DMA1_FLAG_HT4);

    DMA_Cmd(DMA1_Channel4 , ENABLE);
}


void DMA1_Channel5_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA1_Channel5_IRQHandler(void)
{
    if(DMA_GetITStatus(DMA1_IT_TC5))
    {
        // printf("\r\n Enter DMA tx Interrupt \r\n");
        // active_tx_buffer = 2;
        active_tx_buffer = 1;
        DMA_ClearITPendingBit(DMA1_IT_TC5);
    } else if (DMA_GetITStatus(DMA1_IT_HT5)) {
        // printf("\r\n Enter DMA tX5 half Interrupt \r\n");
        active_tx_buffer = 2;
        DMA_ClearITPendingBit(DMA1_IT_HT5);
    }  else if(DMA_GetFlagStatus(DMA1_IT_TE5))
    {
        // printf("DMA TX5 ERROr Interrupt \r\n");
        DMA_ClearITPendingBit(DMA1_IT_TE5); 
    }
}

void Reset_DMA1_Channel4()
{

    // 1. Disable DMA to stop any current activities
    DMA_Cmd(DMA1_Channel4, DISABLE);

    // 2. FORCE RESET SPI to clear internal FIFOs/Shift Registers
    // This is the only way to guarantee the pre-fetched '0xAA' is gone.
    // Simply disabling SPI is often not enough to clear the Data Register.
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_SPI2, ENABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_SPI2, DISABLE);
    
    // 4. Re-Initialize SPI
    // We must re-configure the SPI peripheral after the RCC reset.
    // This function re-sets CR1, CR2, enables DMA requests, and enables SPI.
    SPI_FullDuplex_Init();

    // 5. Reset the transmission counter (CNTR)
    DMA1_Channel4->CNTR = Size * 2;
    
    // 6. Reset Memory Address to the start of TxData
    DMA1_Channel4->MADDR = (uint32_t)DoubleRxData;
    
    // 7. Reset internal state tracker
    active_rx_buffer = 1;
    
    // 8. Clear flags
    DMA_ClearFlag(DMA1_FLAG_TC4 | DMA1_FLAG_HT4 | DMA1_FLAG_TE4);
    
    // 9. Re-enable DMA 
    // Since SPI was enabled in SPI_FullDuplex_Init(), enabling DMA now 
    // will immediately trigger the fetch of the new TxData[0].
    DMA_Cmd(DMA1_Channel4, ENABLE);
}

void DMA1_Channel4_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA1_Channel4_IRQHandler(void)
{
    // printf("rx inter \r\n");

    if(DMA_GetITStatus(DMA1_IT_TC4))
    {
        // printf("\r\n Enter DMA RX4 Interrupt \r\n");
        DMA_ClearITPendingBit(DMA1_IT_TC4);
        uint8_t report_id = DoubleRxData[Size];
        active_rx_buffer = 2;

        if (report_id == REPORT_ID_MOUSE || report_id == REPORT_ID_USER_CONFIG || report_id == REPORT_ID_DUAL_MOUSE) {
            new_rx_data = 1;
                    if (report_id == REPORT_ID_DUAL_MOUSE) {
                    }
                    if (report_id == REPORT_ID_MOUSE) {
                        g_usb_packet_count++;
                         memcpy(&USBHS_EP3_TX_Buf[MOUSE_REPORT_SIZE], &DoubleRxData[Size], MOUSE_REPORT_SIZE);
                        //  RB_Push(&packetVal, &DoubleRxData[Size]);
                    }
            // print_buffers(&DoubleRxData[Size], Size);
        } else if (report_id == REPORT_ID_SPI_MASTER_READ_ONLY) {
            printf(" read only \r\n");
        } else {
            printf(" not mouse report \r\n");
            print_buffers(&DoubleRxData[Size], Size);
            Reset_DMA1_Channel4();
        }

    } else if (DMA_GetITStatus(DMA1_IT_HT4)) {
        // printf("\r\n Enter DMA RX4 half Interrupt \r\n");
        DMA_ClearITPendingBit(DMA1_IT_HT4);
        uint8_t report_id = DoubleRxData[0];
        active_rx_buffer = 1;
        if (report_id == REPORT_ID_MOUSE || report_id == REPORT_ID_USER_CONFIG || report_id == REPORT_ID_DUAL_MOUSE) {
            new_rx_data = 1;
                    if (report_id == REPORT_ID_DUAL_MOUSE) {
                    }
                    if (report_id == REPORT_ID_MOUSE) {
                                                    g_usb_packet_count++;
                         // Dual Report: Split into two standard Mouse Reports (ID 0x01)
                         memcpy(&USBHS_EP3_TX_Buf[0], &DoubleRxData[0], MOUSE_REPORT_SIZE);
                        //  RB_Push(&packetVal, &DoubleRxData[0]);
                    }
            // print_buffers(&DoubleRxData[0], Size);
        } else if (report_id == REPORT_ID_SPI_MASTER_READ_ONLY) {
            printf(" read only \r\n");
        } else {
            printf(" not mouse report half \r\n");
            print_buffers(&DoubleRxData[0], Size);
            Reset_DMA1_Channel4();

        }
    } else if(DMA_GetFlagStatus(DMA1_IT_TE4))
    {
        // printf("DMA RX4 ERROr Interrupt \r\n");
        DMA_ClearITPendingBit(DMA1_IT_TE4); 
        Reset_DMA1_Channel4();
    }
}

/*********************************************************************
 * @fn      SPI_Update_Tx_Data
 *
 * @brief   Update the data to be sent to the Master.
 *
 * @param   data - Pointer to the data source.
 * len  - Length of data to copy.
 *
 * @return  none
 */
void SPI_Update_Tx_Data(volatile uint8_t* data, uint8_t len)
{
    // Limit length to prevent buffer overflow (Total Buffer is Size * 2)
    uint8_t max_len = Size * 2;
    uint8_t copy_len = (len > max_len) ? max_len : len;
    
    // 1. Disable DMA to stop any current activities
    DMA_Cmd(DMA1_Channel5, DISABLE);

    // 2. FORCE RESET SPI to clear internal FIFOs/Shift Registers
    // This is the only way to guarantee the pre-fetched '0xAA' is gone.
    // Simply disabling SPI is often not enough to clear the Data Register.
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_SPI2, ENABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_SPI2, DISABLE);

    // 3. Update the data in memory
    memcpy(TxData, (uint8_t*)data, copy_len);
    
    // Debug print
    // printf("tx data updated: \r\n");
    // for (int i = 0; i < copy_len; i++) {
    //     printf("%02x ", TxData[i]);
    // }   
    // printf("\r\n");

    // 4. Re-Initialize SPI
    // We must re-configure the SPI peripheral after the RCC reset.
    // This function re-sets CR1, CR2, enables DMA requests, and enables SPI.
    SPI_FullDuplex_Init();

    // 5. Reset the transmission counter (CNTR)
    DMA1_Channel5->CNTR = Size * 2;
    
    // 6. Reset Memory Address to the start of TxData
    DMA1_Channel5->MADDR = (uint32_t)TxData;
    
    // 7. Reset internal state tracker
    active_tx_buffer = 1;
    
    // 8. Clear flags
    DMA_ClearFlag(DMA1_FLAG_TC5 | DMA1_FLAG_HT5 | DMA1_FLAG_TE5);
    
    // 9. Re-enable DMA 
    // Since SPI was enabled in SPI_FullDuplex_Init(), enabling DMA now 
    // will immediately trigger the fetch of the new TxData[0].
    DMA_Cmd(DMA1_Channel5, ENABLE);
}


uint8_t DMA_Channel_IsEnabled(DMA_Channel_TypeDef* DMAy_Channelx)
{
    return (DMAy_Channelx->CFGR & DMA_CFGR1_EN) ? 1 : 0;
}


void SPI2_DMA_Send(uint8_t *data, uint16_t length)
{
    // while(DMA_Channel_IsEnabled(DMA1_Channel5));
    

    DMA_Cmd(DMA1_Channel5, DISABLE);
    
    DMA1_Channel5->CNTR = length;
    DMA1_Channel5->MADDR = (uint32_t)data;
    
    DMA_ClearFlag(DMA1_FLAG_TC5);
    
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
    DMA_Cmd(DMA1_Channel5, ENABLE);
}


void SPI2_DMA_Transceive(uint8_t *tx_data, uint8_t *rx_data, uint16_t length)
{

    while(DMA_Channel_IsEnabled(DMA1_Channel4) || 
          DMA_Channel_IsEnabled(DMA1_Channel5));
    
    DMA_Cmd(DMA1_Channel4, DISABLE);
    DMA_Cmd(DMA1_Channel5, DISABLE);
    
    DMA1_Channel5->CNTR = length;
    DMA1_Channel5->MADDR = (uint32_t)tx_data;
    
    DMA1_Channel4->CNTR = length;
    DMA1_Channel4->MADDR = (uint32_t)rx_data;
    
    DMA_ClearFlag(DMA1_FLAG_TC4 | DMA1_FLAG_TC5);
    
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Rx, ENABLE);
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
    
    DMA_Cmd(DMA1_Channel4, ENABLE);
    DMA_Cmd(DMA1_Channel5, ENABLE);
}