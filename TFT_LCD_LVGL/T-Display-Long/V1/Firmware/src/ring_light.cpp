#include "ring_light.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <driver/rmt.h>
#include <esp_rom_sys.h>
#include <math.h>
#include <soc/rmt_struct.h>
#include <string.h>

#include "board_pins.h"

namespace {
RingLight g_ring;
Adafruit_NeoPixel g_pixels(RING_LED_COUNT, PIN_RING, NEO_GRB + NEO_KHZ800);
bool g_rmtReady = false;

// Adafruit NEO_KHZ800 bit times. The whole frame is written into RMT memory
// and sent with no driver interrupt, so Wi-Fi cannot refill or chop it.
bool startRmt(uint32_t* hzOut) {
  rmt_config_t config = RMT_DEFAULT_CONFIG_TX((gpio_num_t)PIN_RING, RMT_CHANNEL_0);
  config.clk_div = 2;
  config.mem_block_num = 6;
  config.tx_config.carrier_en = false;
  config.tx_config.loop_en = false;
  config.tx_config.idle_output_en = true;
  config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  if (rmt_config(&config) != ESP_OK) return false;

  RMT.sys_conf.apb_fifo_mask = 1;
  RMT.sys_conf.mem_clk_force_on = 1;
  RMT.chnconf0[0].carrier_en_n = 0;
  RMT.chnconf0[0].mem_tx_wrap_en_n = 0;
  RMT.chnconf0[0].idle_out_en_n = 1;
  RMT.chnconf0[0].idle_out_lv_n = 0;
  RMT.chnconf0[0].div_cnt_n = 2;
  RMT.chnconf0[0].mem_size_n = 6;
  RMT.chnconf0[0].conf_update_n = 1;

  uint32_t hz = 0;
  rmt_get_counter_clock(RMT_CHANNEL_0, &hz);
  if (hz < 1000000) hz = 40000000;
  *hzOut = hz;
  return true;
}
}  // namespace

RingLight& ring() { return g_ring; }

void RingLight::begin() {
  memset(shown_, 0xFF, sizeof(shown_));
  xTaskCreatePinnedToCore(workerTrampoline, "halo-ring", 4096, this, 2, nullptr, xPortGetCoreID());
}

void RingLight::suspend(bool suspended) { suspended_ = suspended; }

void RingLight::setPower(bool on) {
  power_ = on;
  dirty_ = true;
  cleared_ = false;
}

void RingLight::setBrightness(uint8_t brightness) {
  brightness_ = brightness;
  dirty_ = true;
}

void RingLight::setColor(uint8_t r, uint8_t g, uint8_t b) {
  red_ = r;
  green_ = g;
  blue_ = b;
  dirty_ = true;
}

void RingLight::setEffect(const char* name) {
  effect_ = RingEffect::None;
  if (name == nullptr || strcmp(name, "None") == 0) effect_ = RingEffect::None;
  else if (strcmp(name, "Pulse") == 0) effect_ = RingEffect::Pulse;
  else if (strcmp(name, "Rainbow") == 0 || strcmp(name, "Addressable Rainbow") == 0) effect_ = RingEffect::Rainbow;
  else if (strcmp(name, "Scan") == 0) effect_ = RingEffect::Scan;
  else if (strcmp(name, "Twinkle") == 0 || strcmp(name, "Random Twinkle") == 0) effect_ = RingEffect::Twinkle;
  else if (strcmp(name, "Color Wipe") == 0) effect_ = RingEffect::ColorWipe;
  dirty_ = true;
}

bool RingLight::consumePureBlueAsWhite() {
  if (effect_ != RingEffect::None) return false;
  if (red_ < 13 && green_ < 13 && blue_ > 217) {
    red_ = 255;
    green_ = 255;
    blue_ = 255;
    dirty_ = true;
    return true;
  }
  return false;
}

const char* RingLight::effectName() const {
  switch (effect_) {
    case RingEffect::Pulse: return "Pulse";
    case RingEffect::Rainbow: return "Rainbow";
    case RingEffect::Scan: return "Scan";
    case RingEffect::Twinkle: return "Twinkle";
    case RingEffect::ColorWipe: return "Color Wipe";
    case RingEffect::None:
    default: return "None";
  }
}

bool RingLight::takeDirty() {
  bool was = dirty_;
  dirty_ = false;
  return was;
}

void RingLight::setPixel(int index, uint8_t r, uint8_t g, uint8_t b, uint8_t scale) {
  if (index < 0 || index >= RING_LED_COUNT) return;
  buffer_[index * 3 + 0] = (uint8_t)((g * scale) / 255);
  buffer_[index * 3 + 1] = (uint8_t)((r * scale) / 255);
  buffer_[index * 3 + 2] = (uint8_t)((b * scale) / 255);
}

void RingLight::render(uint32_t now, bool startupRainbow) {
  if (startupRainbow || effect_ == RingEffect::Rainbow) {
    uint8_t scale = startupRainbow ? 160 : brightness_;
    for (int i = 0; i < RING_LED_COUNT; i++) {
      uint16_t hue = (uint16_t)(phase_ + (uint32_t)i * 65536u / RING_LED_COUNT);
      uint32_t color = g_pixels.gamma32(g_pixels.ColorHSV(hue, 255, scale));
      setPixel(i, (color >> 16) & 255, (color >> 8) & 255, color & 255, 255);
    }
    return;
  }

  if (effect_ == RingEffect::Pulse) {
    float phase = (now % 2000) / 2000.0f;
    float wave = 0.5f - 0.5f * cosf(phase * 6.2831853f);
    uint8_t scale = (uint8_t)(brightness_ * (0.20f + 0.80f * wave));
    for (int i = 0; i < RING_LED_COUNT; i++) setPixel(i, red_, green_, blue_, scale);
    return;
  }

  if (effect_ == RingEffect::Scan) {
    int pos = (int)((now / 80) % RING_LED_COUNT);
    for (int i = 0; i < RING_LED_COUNT; i++) {
      bool on = i == pos || i == (pos + 1) % RING_LED_COUNT;
      setPixel(i, red_, green_, blue_, on ? brightness_ : 0);
    }
    return;
  }

  if (effect_ == RingEffect::Twinkle) {
    for (int i = 0; i < RING_LED_COUNT; i++) {
      twinkle_[i] = twinkle_[i] > 12 ? twinkle_[i] - 12 : 0;
    }
    if ((esp_random() % 100) < 8) twinkle_[esp_random() % RING_LED_COUNT] = 255;
    for (int i = 0; i < RING_LED_COUNT; i++) {
      uint8_t scale = (uint8_t)((brightness_ * twinkle_[i]) / 255);
      setPixel(i, red_, green_, blue_, scale);
    }
    return;
  }

  if (effect_ == RingEffect::ColorWipe) {
    static const uint8_t colors[3][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
    uint32_t step = now / 60;
    int color = (step / RING_LED_COUNT) % 3;
    int lit = step % RING_LED_COUNT;
    for (int i = 0; i < RING_LED_COUNT; i++) {
      if (i <= (int)lit) setPixel(i, colors[color][0], colors[color][1], colors[color][2], brightness_);
      else setPixel(i, 0, 0, 0, 0);
    }
    return;
  }

  for (int i = 0; i < RING_LED_COUNT; i++) setPixel(i, red_, green_, blue_, brightness_);
}

void RingLight::markStage(uint8_t stage, uint32_t elapsedUs) {
  if (stage < 4 && elapsedUs > stageMaxUs_[stage]) stageMaxUs_[stage] = elapsedUs;
  stage_ = stage;
}

bool RingLight::show() {
  if (suspended_ || !g_rmtReady) return false;
  uint32_t nowUs = micros();
  if (lastShowUs_ != 0) {
    uint32_t gap = (nowUs - lastShowUs_) / 1000;
    lastGapMs_ = gap;
    if (gap > maxGapMs_) {
      maxGapMs_ = gap;
      lateStage_ = stage_;
    }
    if (gap > 35) late_++;
  }

  float ratio = (float)rmtHz_ / 1000000000.0f;
  uint32_t t0h = (uint32_t)(ratio * 400.0f);
  uint32_t t0l = (uint32_t)(ratio * 850.0f);
  uint32_t t1h = (uint32_t)(ratio * 800.0f);
  uint32_t t1l = (uint32_t)(ratio * 450.0f);
  if (t0h == 0) t0h = 1;
  if (t0l == 0) t0l = 1;
  if (t1h == 0) t1h = 1;
  if (t1l == 0) t1l = 1;

  static rmt_item32_t items[RING_LED_COUNT * 24 + 1];
  int n = 0;
  for (int i = 0; i < RING_LED_COUNT * 3; i++) {
    uint8_t byte = buffer_[i];
    for (int bit = 7; bit >= 0; bit--) {
      bool one = byte & (1u << bit);
      items[n].duration0 = one ? t1h : t0h;
      items[n].level0 = 1;
      items[n].duration1 = one ? t1l : t0l;
      items[n].level1 = 0;
      n++;
    }
  }
  items[n].val = 0;
  n++;

  volatile uint32_t* mem = &RMTMEM.chan[0].data32[0].val;
  for (int i = 0; i < n; i++) mem[i] = items[i].val;

  RMT.int_clr.ch0_tx_end_int_clr = 1;
  RMT.chnconf0[0].mem_rd_rst_n = 1;
  RMT.chnconf0[0].conf_update_n = 1;
  uint32_t sendUs = micros();
  RMT.chnconf0[0].tx_start_n = 1;
  uint32_t spin = 0;
  while (RMT.int_raw.ch0_tx_end_int_raw == 0 && spin++ < 400000) {
  }
  sendUs = micros() - sendUs;
  if (sendUs > maxFreezeUs_) maxFreezeUs_ = sendUs;
  if (RMT.int_raw.ch0_tx_end_int_raw == 0) {
    freezeFail_++;
    return false;
  }
  frames_++;
  lastShowUs_ = micros();
  esp_rom_delay_us(300);
  return true;
}

bool RingLight::commit() {
  if (memcmp(buffer_, shown_, sizeof(buffer_)) == 0) return true;
  if (!show()) return false;
  memcpy(shown_, buffer_, sizeof(buffer_));
  return true;
}

void RingLight::workerTrampoline(void* arg) { static_cast<RingLight*>(arg)->worker(); }

void RingLight::worker() {
  g_pixels.begin();
  g_pixels.setBrightness(255);
  g_rmtReady = startRmt(&rmtHz_);
  core_ = (uint8_t)xPortGetCoreID();
  Serial.printf("ring rmt %u Hz core %u\n", rmtHz_, core_);

  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(20));
    pump();
  }
}

void RingLight::loop(bool startupBlink, bool mqttConnected, bool wifiUp) {
  startupBlink_ = startupBlink;
  if (mqttConnected) mqttSeen_ = true;
  if (wifiUp) wifiUp_ = true;
  primed_ = true;
}

void RingLight::pump() {
  if (!primed_ || suspended_) return;
  uint32_t now = millis();
  // The LED timer fights the Wi-Fi join. Stay dark until the link is up.
  if (!wifiUp_ && now < 45000) return;

  if (!startupFinished_) {
    bool done = !startupBlink_ || mqttSeen_ || now > 30000;
    if (!done) {
      phase_ += 256;
      render(now, true);
      commit();
      cleared_ = false;
      return;
    }
    startupFinished_ = true;
    if (!power_) {
      red_ = 255;
      green_ = 255;
      blue_ = 255;
      brightness_ = 0;
      effect_ = RingEffect::None;
      dirty_ = true;
    }
  }

  if (!power_) {
    if (!cleared_) {
      memset(buffer_, 0, sizeof(buffer_));
      if (commit()) cleared_ = true;
    }
    return;
  }

  cleared_ = false;
  if (effect_ == RingEffect::Rainbow) phase_ += 256;
  render(now, false);
  commit();
}
