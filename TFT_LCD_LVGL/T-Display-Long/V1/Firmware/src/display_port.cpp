#include "display_port.h"

#include <Arduino.h>
#include <driver/spi_master.h>
#include <lvgl.h>
#include <string.h>

#include "board_pins.h"

namespace {
spi_device_handle_t g_spi = nullptr;
lv_disp_draw_buf_t g_drawBuf;
lv_color_t* g_buf = nullptr;
constexpr int kBandRows = 8;
constexpr int kSpiHz = 32000000;
alignas(4) uint16_t g_band[HALO_LCD_W * kBandRows];

void csLow() { digitalWrite(PIN_LCD_CS, LOW); }
void csHigh() { digitalWrite(PIN_LCD_CS, HIGH); }

void sendCmd(uint8_t cmd, const uint8_t* data, uint8_t len) {
  uint8_t ram[32];
  if (len > sizeof(ram)) len = sizeof(ram);
  if (len && data) memcpy(ram, data, len);

  spi_transaction_t tx;
  memset(&tx, 0, sizeof(tx));
  tx.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
  tx.cmd = 0x02;
  tx.addr = ((uint32_t)cmd) << 8;
  if (len) {
    tx.tx_buffer = ram;
    tx.length = (uint32_t)len * 8;
  }
  csLow();
  spi_device_polling_transmit(g_spi, &tx);
  csHigh();
}

void sendCmdDelay(uint8_t cmd, const uint8_t* data, uint8_t len, uint16_t delayMs) {
  sendCmd(cmd, data, len);
  if (delayMs) delay(delayMs);
}

// Vendor setup for the LilyGO 180x640 AXS15231B. Lengths match the board
// example, including commands whose stored array is longer than the length byte.
void panelInit() {
  const uint8_t bbUnlock[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5};
  const uint8_t a0[] = {0x00, 0x30, 0x00, 0x02, 0x00, 0x00, 0x05, 0x3F, 0x30,
                        0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00};
  const uint8_t a2[] = {0x30, 0x04, 0x14, 0x50, 0x80, 0x30, 0x85, 0x80, 0xB4, 0x28, 0xFF,
                        0xFF, 0xFF, 0x20, 0x50, 0x10, 0x02, 0x06, 0x20, 0xD0, 0xC0, 0x01,
                        0x12, 0xA0, 0x91, 0xC0, 0x20, 0x7F, 0xFF, 0x00, 0x06};
  const uint8_t d0[] = {0x80, 0xB4, 0x21, 0x24, 0x08, 0x05, 0x10, 0x01, 0xF2, 0x02,
                        0xC2, 0x02, 0x22, 0x22, 0xAA, 0x03, 0x10, 0x12, 0xC0, 0x10,
                        0x10, 0x40, 0x04, 0x00, 0x30, 0x10, 0x00, 0x03, 0x0D, 0x12};
  const uint8_t a3[] = {0xA0, 0x06, 0xAA, 0x00, 0x08, 0x02, 0x0A, 0x04, 0x04, 0x04, 0x04,
                        0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x55, 0x55};
  const uint8_t c1[] = {0x33, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55, 0x02, 0x00,
                        0x01, 0x01, 0x53, 0xFF, 0xFF, 0xFF, 0x4F, 0x52, 0x00, 0x4F,
                        0x52, 0x00, 0x45, 0x3B, 0x0B, 0x04, 0x0D, 0x00, 0xFF, 0x42};
  const uint8_t c4[] = {0x00, 0x24, 0x33, 0x80, 0x66, 0xEA, 0x64, 0x32, 0xC8, 0x64,
                        0xC8, 0x32, 0x90, 0x90, 0x11, 0x06, 0xDC, 0xFA, 0x00, 0x00,
                        0x80, 0xFE, 0x10, 0x10, 0x00, 0x0A, 0x0A, 0x44, 0x50};
  const uint8_t c5[] = {0x18, 0x00, 0x00, 0x03, 0xFE, 0xE8, 0x3B, 0x20, 0x30, 0x10, 0x88, 0xDE,
                        0x0D, 0x08, 0x0F, 0x0F, 0x01, 0xE8, 0x3B, 0x20, 0x10, 0x10, 0x00};
  const uint8_t c6[] = {0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B, 0x12, 0x22,
                        0x12, 0x22, 0x01, 0x03, 0x00, 0x02, 0x6A, 0x18, 0xC8, 0x22};
  const uint8_t c7[] = {0x50, 0x36, 0x28, 0x00, 0xA2, 0x80, 0x8F, 0x00, 0x80, 0xFF, 0x07, 0x11, 0x9C, 0x6F,
                        0xFF, 0x24, 0x0C, 0x0D, 0x0E, 0x0F, 0x01, 0x01, 0x01, 0x01, 0x3F, 0x07, 0x00};
  const uint8_t c9[] = {0x33, 0x44, 0x44, 0x01};
  const uint8_t cf[] = {0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18, 0x1E, 0x68, 0xF7, 0x00, 0x66, 0x0D,
                        0x22, 0xC4, 0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x04, 0x04, 0x12, 0xA0, 0x08};
  const uint8_t d5[] = {0x30, 0x30, 0x8A, 0x00, 0x44, 0x04, 0x4A, 0xE5, 0x02, 0x4A,
                        0xE5, 0x02, 0x04, 0xD9, 0x02, 0x47, 0x03, 0x03, 0x03, 0x03,
                        0x83, 0x00, 0x00, 0x00, 0x80, 0x52, 0x53, 0x50, 0x50, 0x00};
  const uint8_t d6[] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x34, 0x02,
                        0x01, 0x83, 0xFF, 0x00, 0x20, 0x50, 0x00, 0x30, 0x03, 0x03,
                        0x50, 0x13, 0x00, 0x00, 0x00, 0x04, 0x50, 0x20, 0x01, 0x00};
  const uint8_t d7[] = {0x03, 0x01, 0x09, 0x0B, 0x0D, 0x0F, 0x1E, 0x1F, 0x18, 0x1D,
                        0x1F, 0x19, 0x30, 0x30, 0x04, 0x00, 0x20, 0x20, 0x1F};
  const uint8_t d8[] = {0x02, 0x00, 0x08, 0x0A, 0x0C, 0x0E, 0x1E, 0x1F, 0x18, 0x1D, 0x1F, 0x19};
  const uint8_t df[] = {0x44, 0x33, 0x4B, 0x69, 0x00, 0x0A};
  const uint8_t e0[] = {0x1F, 0x20, 0x10, 0x17, 0x0D, 0x09, 0x12, 0x2A, 0x44,
                        0x25, 0x0C, 0x15, 0x13, 0x31, 0x36, 0x2F, 0x02};
  const uint8_t e1[] = {0x3F, 0x20, 0x10, 0x16, 0x0C, 0x08, 0x12, 0x29, 0x43,
                        0x25, 0x0C, 0x15, 0x13, 0x32, 0x36, 0x2F, 0x27};
  const uint8_t e2[] = {0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44,
                        0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D};
  const uint8_t e3[] = {0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44,
                        0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F};
  const uint8_t e4[] = {0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44,
                        0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D};
  const uint8_t e5[] = {0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44,
                        0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F};
  const uint8_t bbLock[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  const uint8_t madctl[] = {0x00};

  sendCmdDelay(0x28, nullptr, 0, 20);
  sendCmdDelay(0x10, nullptr, 0, 200);
  sendCmd(0xBB, bbUnlock, sizeof(bbUnlock));
  sendCmd(0xA0, a0, sizeof(a0));
  sendCmd(0xA2, a2, sizeof(a2));
  sendCmd(0xD0, d0, sizeof(d0));
  sendCmd(0xA3, a3, sizeof(a3));
  sendCmd(0xC1, c1, sizeof(c1));
  sendCmd(0xC4, c4, sizeof(c4));
  sendCmd(0xC5, c5, sizeof(c5));
  sendCmd(0xC6, c6, sizeof(c6));
  sendCmd(0xC7, c7, sizeof(c7));
  sendCmd(0xC9, c9, sizeof(c9));
  sendCmd(0xCF, cf, sizeof(cf));
  sendCmd(0xD5, d5, sizeof(d5));
  sendCmd(0xD6, d6, sizeof(d6));
  sendCmd(0xD7, d7, sizeof(d7));
  sendCmd(0xD8, d8, sizeof(d8));
  sendCmd(0xDF, df, sizeof(df));
  sendCmd(0xE0, e0, sizeof(e0));
  sendCmd(0xE1, e1, sizeof(e1));
  sendCmd(0xE2, e2, sizeof(e2));
  sendCmd(0xE3, e3, sizeof(e3));
  sendCmd(0xE4, e4, sizeof(e4));
  sendCmd(0xE5, e5, sizeof(e5));
  sendCmd(0xBB, bbLock, sizeof(bbLock));
  sendCmdDelay(0x28, nullptr, 0, 20);
  sendCmdDelay(0x10, nullptr, 0, 200);
  sendCmdDelay(0x11, nullptr, 0, 200);
  sendCmd(0x29, nullptr, 0);
  sendCmd(0x36, madctl, sizeof(madctl));
}

void pushFrame(const uint16_t* frame) {
  uint8_t caset[4] = {0, 0, (uint8_t)((HALO_LCD_W - 1) >> 8), (uint8_t)(HALO_LCD_W - 1)};
  uint8_t raset[4] = {0, 0, (uint8_t)((HALO_LCD_H - 1) >> 8), (uint8_t)(HALO_LCD_H - 1)};
  sendCmd(0x2A, caset, 4);
  sendCmd(0x2B, raset, 4);

  // One window for the whole glass. Later chunks continue that write; opening
  // a new window every few rows left a stripe on each boundary.
  csLow();
  bool first = true;
  for (int y = 0; y < HALO_LCD_H; y += kBandRows) {
    uint32_t n = (uint32_t)HALO_LCD_W * kBandRows;
    const uint16_t* src = frame + (uint32_t)y * HALO_LCD_W;
    for (uint32_t i = 0; i < n; ++i) {
      uint16_t c = src[i];
      g_band[i] = (uint16_t)((c << 8) | (c >> 8));
    }

    spi_transaction_ext_t tx;
    memset(&tx, 0, sizeof(tx));
    if (first) {
      tx.base.flags = SPI_TRANS_MODE_QIO;
      tx.base.cmd = 0x32;
      tx.base.addr = 0x002C00;
      first = false;
    } else {
      tx.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR |
                      SPI_TRANS_VARIABLE_DUMMY;
      tx.command_bits = 0;
      tx.address_bits = 0;
      tx.dummy_bits = 0;
    }
    tx.base.tx_buffer = g_band;
    tx.base.length = n * 16;
    spi_device_polling_transmit(g_spi, (spi_transaction_t*)&tx);
  }
  csHigh();
}

void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color) {
  (void)area;
  if (g_spi == nullptr || color == nullptr) {
    lv_disp_flush_ready(drv);
    return;
  }
  pushFrame((const uint16_t*)&color->full);
  lv_disp_flush_ready(drv);
}

void fillBlack() {
  static uint16_t* black = nullptr;
  if (black == nullptr) {
    black = (uint16_t*)heap_caps_calloc((size_t)HALO_LCD_W * HALO_LCD_H, sizeof(uint16_t),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (black != nullptr) pushFrame(black);
}
}  // namespace

void displayBegin() {
  pinMode(PIN_LCD_CS, OUTPUT);
  csHigh();
  pinMode(PIN_LCD_RST, OUTPUT);
  digitalWrite(PIN_LCD_RST, HIGH);
  delay(130);
  digitalWrite(PIN_LCD_RST, LOW);
  delay(130);
  digitalWrite(PIN_LCD_RST, HIGH);
  delay(300);

  spi_bus_config_t buscfg;
  memset(&buscfg, 0, sizeof(buscfg));
  buscfg.mosi_io_num = PIN_LCD_D0;
  buscfg.miso_io_num = PIN_LCD_D1;
  buscfg.sclk_io_num = PIN_LCD_SCK;
  buscfg.quadwp_io_num = PIN_LCD_D2;
  buscfg.quadhd_io_num = PIN_LCD_D3;
  buscfg.max_transfer_sz = (int)sizeof(g_band) + 16;
  buscfg.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;
  esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    Serial.printf("Display SPI bus failed: %d\n", (int)err);
    return;
  }

  spi_device_interface_config_t devcfg;
  memset(&devcfg, 0, sizeof(devcfg));
  devcfg.command_bits = 8;
  devcfg.address_bits = 24;
  devcfg.mode = 0;
  devcfg.clock_speed_hz = kSpiHz;
  devcfg.spics_io_num = -1;
  devcfg.flags = SPI_DEVICE_HALFDUPLEX;
  devcfg.queue_size = 1;
  err = spi_bus_add_device(SPI2_HOST, &devcfg, &g_spi);
  if (err != ESP_OK) {
    Serial.printf("Display SPI device failed: %d\n", (int)err);
    return;
  }

  panelInit();
  fillBlack();

  lv_init();
  size_t pixels = (size_t)HALO_LCD_W * HALO_LCD_H;
  g_buf = (lv_color_t*)heap_caps_malloc(sizeof(lv_color_t) * pixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (g_buf == nullptr) {
    Serial.println("LVGL framebuffer alloc failed");
    return;
  }
  lv_disp_draw_buf_init(&g_drawBuf, g_buf, nullptr, pixels);

  static lv_disp_drv_t drv;
  lv_disp_drv_init(&drv);
  drv.hor_res = HALO_LCD_W;
  drv.ver_res = HALO_LCD_H;
  drv.flush_cb = flushCb;
  drv.draw_buf = &g_drawBuf;
  drv.full_refresh = 1;
  lv_disp_t* disp = lv_disp_drv_register(&drv);
  lv_disp_set_bg_color(disp, lv_color_hex(0x000000));
  lv_disp_set_bg_opa(disp, LV_OPA_COVER);
}

void displayPump() { lv_timer_handler(); }
