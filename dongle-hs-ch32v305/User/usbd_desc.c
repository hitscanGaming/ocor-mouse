/********************************** (C) COPYRIGHT *******************************
 * File Name          : composite_km_desc.h
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/08/18
 * Description        : All descriptors for the keyboard and mouse composite device.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/


/*******************************************************************************/
/* Header File */
#include "usbd_desc.h"

/*******************************************************************************/
/* Device Descriptor */
const uint8_t MyDevDescr[ ] =
{
    USB_DESC_SIZE_DEVICE,                                   // bLength
    USB_DESC_TYPE_DEVICE,                                   // bDescriptorType
    0x00, 0x02,                                             // bcdUSB
    0x00,                                                   // bDeviceClass
    0x00,                                                   // bDeviceSubClass
    0x00,                                                   // bDeviceProtocol
    DEF_USBD_UEP0_SIZE,                                     // bMaxPacketSize0
    (uint8_t)DEF_USB_VID, (uint8_t)( DEF_USB_VID >> 8 ),    // idVendor
    (uint8_t)DEF_USB_PID, (uint8_t)( DEF_USB_PID >> 8 ),    // idProduct
    0x00, DEF_IC_PRG_VER,                                   // bcdDevice
    0x01,                                                   // iManufacturer
    0x02,                                                   // iProduct
    0x03,                                                   // iSerialNumber
    0x01,                                                   // bNumConfigurations
};

/* Configuration Descriptor Set */
const uint8_t MyCfgDescr[ ] =
{
    /* Configuration Descriptor */
    USB_DESC_SIZE_CONFIG,                                   // bLength
    USB_DESC_TYPE_CONFIGURATION,                            // bDescriptorType
    0x3B+9+9+7, 0x00,                                       // wTotalLength
    0x03,                                                   // bNumInterfaces
    0x01,                                                   // bConfigurationValue
    0x00,                                                   // iConfiguration
    0xA0,                                                   // bmAttributes: Bus Powered; Remote Wakeup
    0x32,                                                   // MaxPower: 100mA

    /* Interface Descriptor (MOUSE1) */
    USB_DESC_SIZE_INTERFACE,                                // bLength
    USB_DESC_TYPE_INTERFACE,                                // bDescriptorType
    ITF_NUM_MOUSE1,                                         // bInterfaceNumber
    0x00,                                                   // bAlternateSetting
    0x01,                                                   // bNumEndpoints
    USB_CLASS_HID,                                          // bInterfaceClass
    USB_SUBCLASS_BOOT,                                      // bInterfaceSubClass
    USB_PROTOCOL_MOUSE,                                     // bInterfaceProtocol: MOUSE
    0x00,                                                   // iInterface

    /* HID Descriptor (MOUSE1) */
    USB_DESC_SIZE_HID,                                      // bLength
    USB_DESC_TYPE_HID,                                      // bDescriptorType
    0x11, 0x01,                                             // bcdHID
    0x00,                                                   // bCountryCode
    0x01,                                                   // bNumDescriptors
    USB_DESC_TYPE_REPORT,                                   // bDescriptorType
    DEF_USBD_REPORT_DESC_LEN_MS, 0x00,                      // wDescriptorLength

    /* Endpoint Descriptor (MOUSE1) */
    USB_DESC_SIZE_ENDPOINT,                                 // bLength
    USB_DESC_TYPE_ENDPOINT,                                 // bDescriptorType
    (EP_DIR_IN | EP_ADDR_1),                                // bEndpointAddress: IN Endpoint 1
    0x03,                                                   // bmAttributes (Interrupt)
    0x08, 0x00,                                             // wMaxPacketSize
    0x08,                                                   // bInterval: 10mS

    /* Interface Descriptor (Mouse2) */
    USB_DESC_SIZE_INTERFACE,                                // bLength
    USB_DESC_TYPE_INTERFACE,                                // bDescriptorType
    ITF_NUM_MOUSE2,                                         // bInterfaceNumber
    0x00,                                                   // bAlternateSetting
    0x01,                                                   // bNumEndpoints
    USB_CLASS_HID,                                          // bInterfaceClass
    USB_SUBCLASS_BOOT,                                      // bInterfaceSubClass
    USB_PROTOCOL_MOUSE,                                     // bInterfaceProtocol: Mouse
    0x00,                                                   // iInterface

    /* HID Descriptor (Mouse2) */
    USB_DESC_SIZE_HID,                                      // bLength
    USB_DESC_TYPE_HID,                                      // bDescriptorType
    0x10, 0x01,                                             // bcdHID
    0x00,                                                   // bCountryCode
    0x01,                                                   // bNumDescriptors
    USB_DESC_TYPE_REPORT,                                   // bDescriptorType
    DEF_USBD_REPORT_DESC_LEN_MS, 0x00,                      // wDescriptorLength

    /* Endpoint Descriptor (Mouse2) */
    USB_DESC_SIZE_ENDPOINT,                                 // bLength
    USB_DESC_TYPE_ENDPOINT,                                 // bDescriptorType
    (EP_DIR_IN | EP_ADDR_2),                                // bEndpointAddress: IN Endpoint 2
    0x03,                                                   // bmAttributes (Interrupt)
    0x08, 0x00,                                             // wMaxPacketSize
    0x08,                                                   // bInterval: 1mS

    /* Interface Descriptor (Mouse3) */
    USB_DESC_SIZE_INTERFACE,                                // bLength
    USB_DESC_TYPE_INTERFACE,                                // bDescriptorType
    ITF_NUM_MOUSE3,                                         // bInterfaceNumber
    0x00,                                                   // bAlternateSetting
    0x01,                                                   // bNumEndpoints
    USB_CLASS_HID,                                          // bInterfaceClass
    USB_SUBCLASS_BOOT,                                      // bInterfaceSubClass
    USB_PROTOCOL_MOUSE,                                     // bInterfaceProtocol: Mouse
    0x00,                                                   // iInterface

    /* HID Descriptor (Mouse3) */
    USB_DESC_SIZE_HID,                                      // bLength
    USB_DESC_TYPE_HID,                                      // bDescriptorType
    0x10, 0x01,                                             // bcdHID
    0x00,                                                   // bCountryCode
    0x01,                                                   // bNumDescriptors
    USB_DESC_TYPE_REPORT,                                   // bDescriptorType
    DEF_USBD_REPORT_DESC_LEN_MS, 0x00,                      // wDescriptorLength

    /* Endpoint Descriptor (Mouse3) */
    USB_DESC_SIZE_ENDPOINT,                                 // bLength
    USB_DESC_TYPE_ENDPOINT,                                 // bDescriptorType
    (EP_DIR_IN | EP_ADDR_3),                                // bEndpointAddress: IN Endpoint 3
    0x03,                                                   // bmAttributes (Interrupt)
    0x08, 0x00,                                             // wMaxPacketSize
    0x01                                                    // bInterval: 1mS
};

/* Keyboard Report Descriptor */
const uint8_t KeyRepDesc[ ] =
{
    0x05, 0x01,                                             // Usage Page (Generic Desktop)
    0x09, 0x06,                                             // Usage (Keyboard)
    0xA1, 0x01,                                             // Collection (Application)
    0x05, 0x07,                                             // Usage Page (Key Codes)
    0x19, 0xE0,                                             // Usage Minimum (224)
    0x29, 0xE7,                                             // Usage Maximum (231)
    0x15, 0x00,                                             // Logical Minimum (0)
    0x25, 0x01,                                             // Logical Maximum (1)
    0x75, 0x01,                                             // Report Size (1)
    0x95, 0x08,                                             // Report Count (8)
    0x81, 0x02,                                             // Input (Data,Variable,Absolute)
    0x95, 0x01,                                             // Report Count (1)
    0x75, 0x08,                                             // Report Size (8)
    0x81, 0x01,                                             // Input (Constant)
    0x95, 0x03,                                             // Report Count (3)
    0x75, 0x01,                                             // Report Size (1)
    0x05, 0x08,                                             // Usage Page (LEDs)
    0x19, 0x01,                                             // Usage Minimum (1)
    0x29, 0x03,                                             // Usage Maximum (3)
    0x91, 0x02,                                             // Output (Data,Variable,Absolute)
    0x95, 0x05,                                             // Report Count (5)
    0x75, 0x01,                                             // Report Size (1)
    0x91, 0x01,                                             // Output (Constant,Array,Absolute)
    0x95, 0x06,                                             // Report Count (6)
    0x75, 0x08,                                             // Report Size (8)
    0x26, 0xFF, 0x00,                                       // Logical Maximum (255)
    0x05, 0x07,                                             // Usage Page (Key Codes)
    0x19, 0x00,                                             // Usage Minimum (0)
    0x29, 0x91,                                             // Usage Maximum (145)
    0x81, 0x00,                                             // Input(Data,Array,Absolute)
    0xC0                                                    // End Collection
};

/* Mouse Report Descriptor */
const uint8_t MouseRepDescBackUP[ ] =
{
    0x05, 0x01,                                             // Usage Page (Generic Desktop)
    0x09, 0x02,                                             // Usage (Mouse)
    0xA1, 0x01,                                             // Collection (Application)

    0x09, 0x01,                                             // Usage (Pointer)
    0xA1, 0x00,                                             // Collection (Physical)
    0x05, 0x09,                                             // Usage Page (Button)
    0x19, 0x01,                                             // Usage Minimum (Button 1)
    0x29, 0x05,                                             // Usage Maximum (Button 5)
    0x15, 0x00,                                             // Logical Minimum (0)
    0x25, 0x01,                                             // Logical Maximum (1)
    0x75, 0x01,                                             // Report Size (1)
    0x95, 0x05,                                             // Report Count (5)
    0x81, 0x02,                                             // Input (Data,Variable,Absolute)
    0x75, 0x03,                                             // Report Size (3)
    0x95, 0x01,                                             // Report Count (1)
    0x81, 0x01,                                             // Input (Constant,Array,Absolute)

    0x05, 0x01,                                             // Usage Page (Generic Desktop)
    0x09, 0x30,                                             // Usage (X)
    0x09, 0x31,                                             // Usage (Y)
    0x09, 0x38,                                             // Usage (Wheel)
    0x15, 0x81,                                             // Logical Minimum (-127)
    0x25, 0x7F,                                             // Logical Maximum (127)
    0x75, 0x08,                                             // Report Size (8)
    0x95, 0x03,                                             // Report Count (3)
    0x81, 0x06,                                             // Input (Data,Variable,Relative)
    0x75, 0x08,                                             // Report Size (8)
    0x95, 0x04,                                             // Report Count (4)
    0x81, 0x01,                                             // Input (Data,Variable,Relative)
    0xC0,                                                   // End Collection
    0xC0                                                    // End Collection
};

/* Mouse Report Descriptor */
const uint8_t MouseRepDesc[DEF_USBD_REPORT_DESC_LEN_MS] =
{
    0x05, 0x01,                                             // Usage Page (Generic Desktop)
    0x09, 0x02,                                             // Usage (Mouse)
    0xA1, 0x01,                                             // Collection (Application)

    0x09, 0x01,                                             // Usage (Pointer)
    0xA1, 0x00,                                             // Collection (Physical)
    0x85, 0x01,                                             /* Report ID */
    0x05, 0x09,                                             // Usage Page (Button)
    0x19, 0x01,                                             // Usage Minimum (Button 1)
    0x29, 0x05,                                             // Usage Maximum (Button 5)
    0x15, 0x00,                                             // Logical Minimum (0)
    0x25, 0x01,                                             // Logical Maximum (1)
    0x75, 0x01,                                             // Report Size (1)
    0x95, 0x05,                                             // Report Count (5)
    0x81, 0x02,                                             // Input (Data,Variable,Absolute)
    0x75, 0x03,                                             // Report Size (3)
    0x95, 0x01,                                             // Report Count (1)
    0x81, 0x01,                                             // Input (Constant,Array,Absolute)

    0x05, 0x01,                                             // Usage Page (Generic Desktop)
    0x09, 0x38,                                             // Usage (Wheel)
    0x15, 0x81,                                             // Logical Minimum (-127)
    0x25, 0x7F,                                             // Logical Maximum (127)
    0x75, 0x08,                                             // Report Size (8)
    0x95, 0x01,                                             // Report Count (1)
    0x81, 0x06,                                             // Input (Data,Variable,Relative)

    0x05, 0x01,                                             // Usage Page (Generic Desktop)
    0x09, 0x30,                                             // Usage (X)
    0x09, 0x31,                                             // Usage (Y)
    0x16, 0x00, 0x80,                                       // Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,                                       // Logical Maximum (32767)
    0x75, 0x10,                                             // Report Size (16)
    0x95, 0x02,                                             // Report Count (2)
    0x81, 0x06,                                             // Input (Data,Variable,Relative)
    0xC0,                                                   // End Collection

    /* User Config Feature Report */
    0x85, REPORT_ID_USER_CONFIG,                            // Report ID (6)
    0x06, 0x00, 0xFF,                                       // Usage Page (Vendor Defined 0xFF00)
    0x0A, 0x01, 0xFF,                                       // Usage (0xFF01)
    0x15, 0x00,                                             // Logical Minimum (0)
    0x26, 0xFF, 0x00,                                       // Logical Maximum (255)
    0x75, 0x08,                                             // Report Size (8)
    0x95, REPORT_SIZE_USER_CONFIG,                          // Report Count (29)
    0xB1, 0x02,                                             // Feature (Data,Variable,Absolute)

    0xC0                                                    // End Collection
};

/* Qualifier Descriptor */
const uint8_t  MyQuaDesc[ ] =
{
    0x0A,                                                   // bLength
    USB_DESC_TYPE_DEVICE_QUALIFIER,                         // bDescriptorType
    0x00, 0x02,                                             // bcdUSB
    0x00,                                                   // bDeviceClass
    0x00,                                                   // bDeviceSubClass
    0x00,                                                   // bDeviceProtocol
    0x40,                                                   // bMaxPacketSize0
    0x00,                                                   // bNumConfigurations
    0x00                                                    // bReserved
};

/* Language Descriptor */
const uint8_t MyLangDescr[ ] =
{
    0x04,  // total length
    USB_DESC_TYPE_STRING,
    0x09,
    0x04
};

/* * HELPER MACRO: Automatically calculates the descriptor length
 * Usage: USB_STRING_DESC('c', 0, 'h', 0...)
 * Logic: 
 * 1. (uint8_t[]){__VA_ARGS__} creates a temporary array of the arguments.
 * 2. sizeof(...) counts those bytes.
 * 3. Adds 2 bytes for the header (Length byte + Type byte).
 */
#define USB_STRING_DESC(...) \
    (sizeof((uint8_t[]){__VA_ARGS__}) + 2), \
    USB_DESC_TYPE_STRING, \
    __VA_ARGS__

/* Manufacturer Descriptor: "MyLab" */
const uint8_t MyManuInfo[] =
{
    USB_STRING_DESC(
        'H', 0, 'i', 0, 't', 0, 's', 0, 'c', 0, 'a', 0, 'n', 0, ' ', 0
    )
};

/* Product Information: "CH32V30x" */
const uint8_t MyProdInfo[] =
{
    USB_STRING_DESC(
        'H', 0, 'i', 0, 't', 0, 's', 0, 'c', 0, 'a', 0, 'n', 0, ' ', 0,
        '8', 0, 'k', 0, ' ', 0,
        'D', 0, 'o', 0, 'n', 0, 'g', 0, 'l', 0, 'e', 0
    )
};

/* Serial Number Information: "0123456789" */
const uint8_t MySerNumInfo[] =
{
    USB_STRING_DESC(
        '0', 0, '1', 0, '2', 0, 
        '3', 0, '4', 0, '5', 0, 
        '6', 0, '7', 0, '8', 0, 
        '9', 0
    )
};