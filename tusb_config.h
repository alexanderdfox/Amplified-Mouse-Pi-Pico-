#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUD_ENDPOINT0_SIZE  64
#define CFG_TUD_ENABLED         1
#define CFG_TUD_HID             6   /* 6 HID interfaces for 6 separate mice or 1 combined */
#define CFG_TUD_HID_EP_BUFSIZE  8

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT        0
#endif

#ifdef __cplusplus
}
#endif

#endif
