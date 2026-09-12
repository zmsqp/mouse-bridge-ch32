#include "usb_lib.h"
#include "usb_desc.h"
#include "bridge_usb_import.h"

/* WCH 官方 CompositeKM 描述符（标准 62B 键盘 + 52B 鼠标，PID FE01） */
const uint8_t USBD_DeviceDescriptor[] = {
    USBD_SIZE_DEVICE_DESC, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00,
    DEF_USBD_UEP0_SIZE, 0x86, 0x1A, 0x01, 0xFE, 0x00, 0x01,
    0x01, 0x02, 0x00, 0x01
};

uint8_t USBD_ConfigDescriptor[USBD_SIZE_CONFIG_DESC] = {
    0x09, 0x02, USBD_SIZE_CONFIG_DESC & 0xFF, USBD_SIZE_CONFIG_DESC >> 8,
    0x03, 0x01, 0x00, 0xA0, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22,
    USBD_SIZE_REPORT_DESC_MS & 0xFF, USBD_SIZE_REPORT_DESC_MS >> 8,
    0x07, 0x05, 0x81, 0x03,
    DEF_ENDP_SIZE_MS & 0xFF, DEF_ENDP_SIZE_MS >> 8, 0x01,
    0x09, 0x04, 0x01, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,
    0x09, 0x21, 0x10, 0x01, 0x00, 0x01, 0x22,
    USBD_SIZE_REPORT_DESC_INJECT & 0xFF, USBD_SIZE_REPORT_DESC_INJECT >> 8,
    0x07, 0x05, 0x82, 0x03,
    DEF_ENDP_SIZE_INJECT & 0xFF, DEF_ENDP_SIZE_INJECT >> 8, 0x01,
    0x09, 0x04, 0x02, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22,
    BRIDGE_USB_IMPORT_REPORT_DESC_SIZE & 0xFF, BRIDGE_USB_IMPORT_REPORT_DESC_SIZE >> 8,
    0x07, 0x05, 0x83, 0x03,
    DEF_ENDP_SIZE_CFG & 0xFF, DEF_ENDP_SIZE_CFG >> 8, 0x0A
};

const uint8_t USBD_StringLangID[USBD_SIZE_STRING_LANGID] = {
    USBD_SIZE_STRING_LANGID, USB_STRING_DESCRIPTOR_TYPE, 0x09, 0x04
};

const uint8_t USBD_StringVendor[USBD_SIZE_STRING_VENDOR] = {
    USBD_SIZE_STRING_VENDOR, USB_STRING_DESCRIPTOR_TYPE,
    'w', 0, 'c', 0, 'h', 0, '.', 0, 'c', 0, 'n', 0
};

const uint8_t USBD_StringProduct[USBD_SIZE_STRING_PRODUCT] = {
    USBD_SIZE_STRING_PRODUCT, USB_STRING_DESCRIPTOR_TYPE,
    'B', 0, 'r', 0, 'i', 0, 'd', 0, 'g', 0, 'e', 0, ' ', 0, 'K', 0, 'M', 0
};

uint8_t USBD_StringSerial[USBD_SIZE_STRING_SERIAL] = {
    USBD_SIZE_STRING_SERIAL, USB_STRING_DESCRIPTOR_TYPE,
    '0', 0, '1', 0, '2', 0, '3', 0, '4', 0, '5', 0, '6', 0, '7', 0, '8', 0, '9', 0
};

static const uint8_t USBD_DefaultMouseRepDesc[USBD_SIZE_REPORT_DESC_MS] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x75, 0x05, 0x95, 0x01,
    0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0
};

uint8_t USBD_MouseRepDesc[USBD_SIZE_REPORT_DESC_MAX];
uint16_t USBD_MouseRepDesc_Len = USBD_SIZE_REPORT_DESC_MS;

const uint8_t USBD_InjectMouseRepDesc[USBD_SIZE_REPORT_DESC_INJECT] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F,
    0x75, 0x08, 0x95, 0x03, 0x81, 0x06, 0xC0, 0xC0
};

void USBD_LoadDefaultReportDescriptor(void)
{
    uint16_t i;

    for(i = 0; i < USBD_SIZE_REPORT_DESC_MS; i++)
    {
        USBD_MouseRepDesc[i] = USBD_DefaultMouseRepDesc[i];
    }
    USBD_MouseRepDesc_Len = USBD_SIZE_REPORT_DESC_MS;
    USBD_ConfigDescriptor[25] = (uint8_t)(USBD_MouseRepDesc_Len & 0xFFU);
    USBD_ConfigDescriptor[26] = (uint8_t)(USBD_MouseRepDesc_Len >> 8);
}

void USBD_LoadPassthroughReportDescriptor(const uint8_t *desc, uint16_t len)
{
    uint16_t i;

    if(desc == 0 || len == 0U || len > USBD_SIZE_REPORT_DESC_MAX)
    {
        USBD_LoadDefaultReportDescriptor();
        return;
    }

    for(i = 0; i < len; i++)
    {
        USBD_MouseRepDesc[i] = desc[i];
    }
    USBD_MouseRepDesc_Len = len;
    USBD_ConfigDescriptor[25] = (uint8_t)(USBD_MouseRepDesc_Len & 0xFFU);
    USBD_ConfigDescriptor[26] = (uint8_t)(USBD_MouseRepDesc_Len >> 8);
}
