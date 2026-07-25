#include "ws2812_bitbang.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"

#include <cinttypes>
#include <esp_cpu.h>
#include <esp_ipc.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <hal/gpio_ll.h>
#include <soc/gpio_struct.h>

namespace esphome::ws2812_bitbang {

static const char *const TAG = "ws2812_bitbang";

// Adafruit NEO_KHZ800 cycle counts at CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ.
static inline uint32_t cycles_t0h() { return CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U / 2500000U; }
static inline uint32_t cycles_t1h() { return CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U / 1250000U; }
static inline uint32_t cycles_period() { return CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U / 800000U; }

// Dual-core hold: Wi-Fi on the other core still jitters the flash/PSRAM bus
// even when this core has IRQs masked — that was the leftover sparkle.
struct DualCoreHold {
  volatile uint32_t stage;  // 0 idle, 1 other-core frozen, 2 release
};

static DualCoreHold s_hold{0};

// portDISABLE_INTERRUPTS only raises INTLEVEL to 3 — Wi-Fi MAC/timer interrupts
// at level 4+ still preempt mid-bit (the residual "minor flicker"). RSIL 15
// masks every maskable level on Xtensa LX7.
static inline IRAM_ATTR uint32_t intlevel_mask_all() {
  uint32_t ps;
  __asm__ __volatile__("rsil %0, 15" : "=r"(ps));
  return ps;
}
static inline IRAM_ATTR void intlevel_restore(uint32_t ps) {
  __asm__ __volatile__("wsr.ps %0 \n rsync" ::"r"(ps));
}

static void IRAM_ATTR HOT ipc_freeze_other_core(void *arg) {
  auto *h = static_cast<DualCoreHold *>(arg);
  uint32_t ps = intlevel_mask_all();
  h->stage = 1;
  while (h->stage != 2) {
  }
  intlevel_restore(ps);
}

static inline IRAM_ATTR void gpio47_high() { GPIO.out1_w1ts.val = (1u << (47 - 32)); }
static inline IRAM_ATTR void gpio47_low() { GPIO.out1_w1tc.val = (1u << (47 - 32)); }

static inline IRAM_ATTR void gpio_any_high(uint8_t pin) { gpio_ll_set_level(&GPIO, pin, 1); }
static inline IRAM_ATTR void gpio_any_low(uint8_t pin) { gpio_ll_set_level(&GPIO, pin, 0); }

void WS2812BitbangLightOutput::setup() {
  size_t buffer_size = this->get_buffer_size_();
  RAMAllocator<uint8_t> allocator(RAMAllocator<uint8_t>::ALLOC_INTERNAL);
  this->buf_ = allocator.allocate(buffer_size);
  if (this->buf_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate LED buffer");
    this->mark_failed();
    return;
  }
  memset(this->buf_, 0, buffer_size);

  this->effect_data_ = allocator.allocate(this->num_leds_);
  if (this->effect_data_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate effect data");
    this->mark_failed();
    return;
  }
  memset(this->effect_data_, 0, this->num_leds_);

  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << this->pin_;
  cfg.mode = GPIO_MODE_OUTPUT;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
  gpio_set_level(static_cast<gpio_num_t>(this->pin_), 0);
}

void IRAM_ATTR HOT WS2812BitbangLightOutput::show_() {
  const uint8_t *p = this->buf_;
  const uint8_t *end = p + this->get_buffer_size_();
  const uint8_t pin = this->pin_;
  const bool pin47 = (pin == 47);
  const uint32_t t0h = cycles_t0h();
  const uint32_t t1h = cycles_t1h();
  const uint32_t period = cycles_period();

  // Freeze the other CPU so Wi-Fi DMA/cache can't stretch bit timing.
  s_hold.stage = 0;
  const int other = (xPortGetCoreID() == 0) ? 1 : 0;
  esp_err_t ipc = esp_ipc_call(other, ipc_freeze_other_core, &s_hold);
  if (ipc == ESP_OK) {
    uint32_t spin = 0;
    while (s_hold.stage != 1 && spin++ < 1000000u) {
    }
  }

  uint32_t ps = intlevel_mask_all();

  while (p < end) {
    uint8_t b = *p++;
    for (int i = 7; i >= 0; i--) {
      uint32_t start = esp_cpu_get_cycle_count();
      uint32_t high = (b & (1 << i)) ? t1h : t0h;
      if (pin47) {
        gpio47_high();
      } else {
        gpio_any_high(pin);
      }
      while ((esp_cpu_get_cycle_count() - start) < high) {
      }
      if (pin47) {
        gpio47_low();
      } else {
        gpio_any_low(pin);
      }
      while ((esp_cpu_get_cycle_count() - start) < period) {
      }
    }
  }

  intlevel_restore(ps);
  s_hold.stage = 2;  // release other core
  esp_rom_delay_us(60);
}

void WS2812BitbangLightOutput::write_state(light::LightState *state) {
  uint32_t now = micros();
  auto rate = this->max_refresh_rate_.value_or(0);
  if (rate != 0 && (now - this->last_refresh_) < rate) {
    this->schedule_show();
    return;
  }
  this->last_refresh_ = now;
  this->mark_shown_();
  this->show_();
  this->status_clear_warning();
}

light::ESPColorView WS2812BitbangLightOutput::get_view_internal(int32_t index) const {
  int32_t r = 0, g = 1, b = 2;
  switch (this->rgb_order_) {
    case ORDER_RGB:
      r = 0;
      g = 1;
      b = 2;
      break;
    case ORDER_RBG:
      r = 0;
      g = 2;
      b = 1;
      break;
    case ORDER_GRB:
      r = 1;
      g = 0;
      b = 2;
      break;
    case ORDER_GBR:
      r = 2;
      g = 0;
      b = 1;
      break;
    case ORDER_BGR:
      r = 2;
      g = 1;
      b = 0;
      break;
    case ORDER_BRG:
      r = 1;
      g = 2;
      b = 0;
      break;
  }
  return {this->buf_ + (index * 3) + r, this->buf_ + (index * 3) + g, this->buf_ + (index * 3) + b, nullptr,
          &this->effect_data_[index], &this->correction_};
}

void WS2812BitbangLightOutput::dump_config() {
  ESP_LOGCONFIG(TAG,
                "WS2812 bitbang (dual-core IRQ hold):\n"
                "  Pin: %u\n"
                "  Num LEDs: %u\n"
                "  Max refresh rate: %" PRIu32,
                this->pin_, this->num_leds_, this->max_refresh_rate_.value_or(0));
}

}  // namespace esphome::ws2812_bitbang

#endif
