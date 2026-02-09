/*
 * USB descriptors: 1 CDC (serial) + 6 HID mouse (so Pico shows in /dev as serial).
 */
#include <string.h>
#include "tusb.h"
#include "usb_descriptors.h"

#define USB_VID 0x2E8A
#define USB_PID 0x000A

enum {
  ITF_NUM_CDC_COMM,
  ITF_NUM_CDC_DATA,
  ITF_NUM_HID0,
  ITF_NUM_HID1,
  ITF_NUM_HID2,
  ITF_NUM_HID3,
  ITF_NUM_HID4,
  ITF_NUM_HID5,
  ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF  0x87
#define EPNUM_CDC_OUT    0x08
#define EPNUM_CDC_IN     0x88
#define EPNUM_HID0       0x81
#define EPNUM_HID1       0x82
#define EPNUM_HID2       0x83
#define EPNUM_HID3       0x84
#define EPNUM_HID4       0x85
#define EPNUM_HID5       0x86
#define CONFIG_LEN       (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + 6 * TUD_HID_DESC_LEN)

uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE))
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return desc_hid_report;
}

uint8_t const desc_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_LEN, 0x00, 100),
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_COMM, 0, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
  TUD_HID_DESCRIPTOR(ITF_NUM_HID0, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID0, CFG_TUD_HID_EP_BUFSIZE, 5),
  TUD_HID_DESCRIPTOR(ITF_NUM_HID1, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID1, CFG_TUD_HID_EP_BUFSIZE, 5),
  TUD_HID_DESCRIPTOR(ITF_NUM_HID2, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID2, CFG_TUD_HID_EP_BUFSIZE, 5),
  TUD_HID_DESCRIPTOR(ITF_NUM_HID3, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID3, CFG_TUD_HID_EP_BUFSIZE, 5),
  TUD_HID_DESCRIPTOR(ITF_NUM_HID4, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID4, CFG_TUD_HID_EP_BUFSIZE, 5),
  TUD_HID_DESCRIPTOR(ITF_NUM_HID5, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID5, CFG_TUD_HID_EP_BUFSIZE, 5)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  const char *str;
  size_t chr_count;

  switch (index) {
    case 0:
      memcpy(&_desc_str[1], (const uint16_t[]){ 0x0409 }, 2);
      chr_count = 1;
      break;
    case 1: str = "Mouse"; break;
    case 2: str = "6-Input Amplified Mouse"; break;
    default: return NULL;
  }

  if (index >= 1) {
    chr_count = strlen(str);
    size_t max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;
    if (chr_count > max_count) chr_count = max_count;
    for (size_t i = 0; i < chr_count; i++)
      _desc_str[1 + i] = (uint8_t)str[i];
  }

  _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}

tusb_desc_device_t const desc_device = {
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0200,
  .bDeviceClass      = 0x00,
  .bDeviceSubClass   = 0x00,
  .bDeviceProtocol   = 0x00,
  .bMaxPacketSize0   = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor          = USB_VID,
  .idProduct         = USB_PID,
  .bcdDevice         = 0x0100,
  .iManufacturer     = 0x01,
  .iProduct          = 0x02,
  .iSerialNumber     = 0x03,
  .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *)&desc_device;
}
