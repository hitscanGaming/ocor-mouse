/********************************** (C) COPYRIGHT *******************************
 * File Name          : usbd_descriptor.h
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/08/18
 * Description        : All descriptors for the keyboard and mouse composite device.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/


#ifndef __USBD_DESC_H
#define __USBD_DESC_H

/*******************************************************************************/
/* Header File */
#include "stdint.h"

/*******************************************************************************/
/* Macro Definition */

/* File Version */
#define DEF_FILE_VERSION              0x01

/* USB Device Info */
#define DEF_USB_VID                   0x1915
#define DEF_USB_PID                   0xF001

/* USB Device Descriptor, Device Serial Number(bcdDevice) */
#define DEF_IC_PRG_VER                DEF_FILE_VERSION

/* USB Standard Device Descriptor Types */
#define USB_DESC_TYPE_DEVICE          0x01
#define USB_DESC_TYPE_CONFIGURATION   0x02
#define USB_DESC_TYPE_STRING          0x03
#define USB_DESC_TYPE_INTERFACE       0x04
#define USB_DESC_TYPE_ENDPOINT        0x05
#define USB_DESC_TYPE_DEVICE_QUALIFIER 0x06
#define USB_DESC_TYPE_OTHER_SPEED_CONFIGURATION 0x07
#define USB_DESC_TYPE_HID             0x21
#define USB_DESC_TYPE_REPORT          0x22

/* USB Class Codes */
#define USB_CLASS_HID                 0x03
#define USB_SUBCLASS_BOOT             0x01
#define USB_PROTOCOL_KEYBOARD         0x01
#define USB_PROTOCOL_MOUSE            0x02

/* Descriptor Lengths */
#define USB_DESC_SIZE_DEVICE          18
#define USB_DESC_SIZE_CONFIG          9
#define USB_DESC_SIZE_INTERFACE       9
#define USB_DESC_SIZE_HID             9
#define USB_DESC_SIZE_ENDPOINT        7

/* Interface Numbers */
#define ITF_NUM_MOUSE1                0x00
#define ITF_NUM_MOUSE2                0x01
#define ITF_NUM_MOUSE3                0x02

/* Endpoint Addresses */
#define EP_DIR_IN                     0x80
#define EP_DIR_OUT                    0x00
#define EP_ADDR_1                     0x01
#define EP_ADDR_2                     0x02
#define EP_ADDR_3                     0x03

/* Descriptor Offsets within Configuration Descriptor 
   Calculated to avoid magic numbers in the C file.
   Structure: [Config(9)] + [Itf0(9)] + [HID0(9)] + [Ep0(7)] + [Itf1(9)] + [HID1(9)] + [Ep1(7)] + ...
*/
#define OFFSET_HID_DESC_0             (USB_DESC_SIZE_CONFIG + USB_DESC_SIZE_INTERFACE)
#define OFFSET_HID_DESC_1             (OFFSET_HID_DESC_0 + USB_DESC_SIZE_HID + USB_DESC_SIZE_ENDPOINT + USB_DESC_SIZE_INTERFACE)
#define OFFSET_HID_DESC_2             (OFFSET_HID_DESC_1 + USB_DESC_SIZE_HID + USB_DESC_SIZE_ENDPOINT + USB_DESC_SIZE_INTERFACE)

/* User Config Report Definition */
#define REPORT_ID_MOUSE               0x01
#define REPORT_ID_DUAL_MOUSE          0x20
#define REPORT_ID_USER_CONFIG         0x06
#define REPORT_ID_SPI_MASTER_READ_ONLY 0xA0
#define REPORT_SIZE_USER_CONFIG       13
#define FEATURE_REPORT_SIZE 1+REPORT_SIZE_USER_CONFIG  // report id + report size

/* USB Device Endpoint Size */
#define DEF_USBD_UEP0_SIZE            64     /* usb hs/fs device end-point 0 size */
/* HS */
#define DEF_USBD_HS_PACK_SIZE         512    /* usb hs device max bluk/int pack size */
#define DEF_USBD_HS_ISO_PACK_SIZE     1024   /* usb hs device max iso pack size */
/* FS */
#define DEF_USBD_FS_PACK_SIZE         64     /* usb fs device max bluk/int pack size */
#define DEF_USBD_FS_ISO_PACK_SIZE     1023   /* usb fs device max iso pack size */
/* LS */
#define DEf_USBD_LS_UEP0_SIZE         8      /* usb ls device end-point 0 size */
#define DEF_USBD_LS_PACK_SIZE         64     /* usb ls device max int pack size */

/* USB Device Endpoint1-6 Size */
/* HS */
#define DEF_USB_EP1_HS_SIZE           DEF_USBD_HS_PACK_SIZE
#define DEF_USB_EP2_HS_SIZE           DEF_USBD_HS_PACK_SIZE
#define DEF_USB_EP3_HS_SIZE           DEF_USBD_HS_PACK_SIZE
#define DEF_USB_EP4_HS_SIZE           DEF_USBD_HS_PACK_SIZE
#define DEF_USB_EP5_HS_SIZE           DEF_USBD_HS_PACK_SIZE
#define DEF_USB_EP6_HS_SIZE           DEF_USBD_HS_PACK_SIZE
/* FS */
#define DEF_USB_EP1_FS_SIZE           DEF_USBD_FS_PACK_SIZE
#define DEF_USB_EP2_FS_SIZE           DEF_USBD_FS_PACK_SIZE
#define DEF_USB_EP3_FS_SIZE           DEF_USBD_FS_PACK_SIZE
#define DEF_USB_EP4_FS_SIZE           DEF_USBD_FS_PACK_SIZE
#define DEF_USB_EP5_FS_SIZE           DEF_USBD_FS_PACK_SIZE
#define DEF_USB_EP6_FS_SIZE           DEF_USBD_FS_PACK_SIZE
/* LS */
/* ... */

/* USB Device Descriptor Length */
/* Note: If a descriptor does not exist, set the length to 0. */
#define DEF_USBD_DEVICE_DESC_LEN      ( (uint16_t)MyDevDescr[ 0 ] )
#define DEF_USBD_CONFIG_DESC_LEN      ( (uint16_t)MyCfgDescr[ 2 ] + ( (uint16_t)MyCfgDescr[ 3 ] << 8 ) )
#define DEF_USBD_REPORT_DESC_LEN_KB   0x3E
// #define DEF_USBD_REPORT_DESC_LEN_MS   (0x34+6)
#define DEF_USBD_REPORT_DESC_LEN_MS   87
#define DEF_USBD_LANG_DESC_LEN        ( (uint16_t)MyLangDescr[ 0 ] )
#define DEF_USBD_MANU_DESC_LEN        ( (uint16_t)MyManuInfo[ 0 ] )
#define DEF_USBD_PROD_DESC_LEN        ( (uint16_t)MyProdInfo[ 0 ] )
#define DEF_USBD_SN_DESC_LEN          ( (uint16_t)MySerNumInfo[ 0 ] )
#define DEF_USBD_QUALFY_DESC_LEN      ( (uint16_t)MyQuaDesc[ 0 ])
#define DEF_USBD_BOS_DESC_LEN         0
#define DEF_USBD_FS_OTH_DESC_LEN      0
#define DEF_USBD_HS_OTH_DESC_LEN      0


/*******************************************************************************/
/* Descriptor Declaration */
extern const uint8_t MyDevDescr[ ];
extern const uint8_t MyCfgDescr[ ];
extern const uint8_t KeyRepDesc[ ];
extern const uint8_t MouseRepDesc[ ];
extern const uint8_t MyQuaDesc[ ];
extern const uint8_t MyLangDescr[ ];
extern const uint8_t MyManuInfo[ ];
extern const uint8_t MyProdInfo[ ];
extern const uint8_t MySerNumInfo[ ];

#endif