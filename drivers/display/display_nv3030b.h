/*
 * Copyright 2026, KONTRBND
 */
#ifndef NV3030B_DISPLAY_DRIVER_H__
#define NV3030B_DISPLAY_DRIVER_H__

#include <zephyr/kernel.h>

#define NV3030B_CMD_SWRESET     0x01 /* Soft Reset */
#define NV3030B_CMD_SLPIN       0x10 /* Sleep In */
#define NV3030B_CMD_SLPOUT      0x11 /* Sleep Out */
#define NV3030B_CMD_INVOFF      0x20 /* Dispay Inversion OFF */
#define NV3030B_CMD_INVON       0x21 /* Dispay Inversion ON */
#define NV3030B_CMD_DISPOFF     0x28 /* Display OFF */
#define NV3030B_CMD_DISPON      0x29 /* Display ON */
#define NV3030B_CMD_CASET       0x2A  /* Column address set */
#define NV3030B_CMD_PASET       0x2B  /* Row address set */
#define NV3030B_CMD_RAMWR       0x2C  /* Memory write */
#define NV3030B_CMD_MADCTL      0x36 /* memory data access control */
#define NV3030B_CMD_COLMOD      0x3A /* Pixel Format */

#endif // NV3030B_DISPLAY_DRIVER_H__
