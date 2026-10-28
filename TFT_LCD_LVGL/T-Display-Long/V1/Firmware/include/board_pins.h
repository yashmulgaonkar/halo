#pragma once

// LilyGO T-Display-S3-Long, native portrait 180 x 640.
// Display pins match the board examples. Sensor and touch pins match the
// HALO ESPHome firmware (Qwiic on GPIO9/48, touch on GPIO15/10).

#define HALO_LCD_W 180
#define HALO_LCD_H 640

#define PIN_LCD_CS 12
#define PIN_LCD_SCK 17
#define PIN_LCD_D0 13
#define PIN_LCD_D1 18
#define PIN_LCD_D2 21
#define PIN_LCD_D3 14
#define PIN_LCD_RST 16
#define PIN_LCD_BL 1

#define PIN_TOUCH_SDA 15
#define PIN_TOUCH_SCL 10
#define PIN_TOUCH_IRQ 11

#define PIN_I2C_SDA 9
#define PIN_I2C_SCL 48

#define PIN_RING 47
#define RING_LED_COUNT 11

#define PIN_BOOT 0
