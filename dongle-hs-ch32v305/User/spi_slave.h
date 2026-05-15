#ifndef __SPI_SLAVE_H
#define __SPI_SLAVE_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "debug.h"
#include "string.h"
#include "usbd_desc.h"

/* Global define */
#define Size          14

extern __attribute__ ((aligned(4))) volatile uint8_t TxData[Size * 2];
extern __attribute__ ((aligned(4))) volatile uint8_t DoubleRxData[Size * 2];
extern volatile uint8_t new_rx_data;
extern volatile uint8_t active_rx_buffer;

extern volatile uint8_t active_tx_buffer;



extern void SPI_FullDuplex_Init( void );
extern void DMA_Rx_Init(DMA_Channel_TypeDef *DMA_CHx, u32 ppadr, u32 memadr, u16 bufsize);
extern void DMA_Tx_Init(DMA_Channel_TypeDef *DMA_CHx, u32 ppadr, u32 memadr, u16 bufsize);

/* Function to update TxData from User Config */
extern void SPI_Update_Tx_Data(volatile uint8_t* data, uint8_t len);

extern void SPI2_DMA_Transceive(uint8_t *tx_data, uint8_t *rx_data, uint16_t length);


#endif /* __SPI_SLAVE_H */
