# Modular packages (HALO)

## Cause of "Duplicate switch ... Startup Light Blink"

That switch lives only in `Halo-v1-Core.yaml`. It is duplicated when Core is
merged **twice**, usually because the device YAML `files:` list includes both:

- `Halo-v1.yaml` (which already pulls in Core), and
- `Halo-v1-Core.yaml` (again)

List **either** the entry file **or** the leaf packages — never both.

## Home Assistant — preferred (leaf packages once)

The NeoPixel driver lives at repo-root `components/ws2812_bitbang`.
HA leaf installs must load it via `external_components` **before** the packages
block (local `path:` only works for USB builds from this folder).

```yaml
esphome:
  name: halo-v1-4d1658
  friendly_name: Halo Air Quality Sensor 4d1658
  name_add_mac_suffix: false
  min_version: 2026.7.2

external_components:
  - source: github://yashmulgaonkar/halo@main
    components: [ws2812_bitbang]

ota:
  - platform: esphome
    id: ota_default

wifi:
  power_save_mode: none
  use_psram: true
  ap:
    ssid: "Halo Hotspot"

packages:
  remote_package:
    url: https://github.com/yashmulgaonkar/halo/
    ref: main
    refresh: 1d
    files:
      - TFT_LCD/T-Display-Long/V1/Firmware/ESPHome/packages/base/globals.yaml
      - TFT_LCD/T-Display-Long/V1/Firmware/ESPHome/packages/system/esphome_core.yaml
      - TFT_LCD/T-Display-Long/V1/Firmware/ESPHome/packages/features/ble/ble_stub.yaml
      - TFT_LCD/T-Display-Long/V1/Firmware/ESPHome/packages/features/airq/airq_history.yaml
      - TFT_LCD/T-Display-Long/V1/Firmware/ESPHome/packages/features/page_rotation/page_rotation.yaml
      - TFT_LCD/T-Display-Long/V1/Firmware/ESPHome/packages/features/ota/ota_channel.yaml
      - TFT_LCD/T-Display-Long/V1/Firmware/ESPHome/Halo-v1-Core.yaml
```

Do **not** add `Halo-v1.yaml` to that `files:` list.

Use `ble_improv.yaml` instead of `ble_stub.yaml` only when you need Improv
Wi-Fi provisioning (Bluedroid can worsen LED flicker under heavy radio load).

## Local / CI

[`Halo-v1.yaml`](Halo-v1.yaml) and [`Halo-v1-dev.yaml`](Halo-v1-dev.yaml) use
sibling `!include`s from this directory and a local `external_components` path
to `components/ws2812_bitbang` at the repo root.

```
packages/
  base/globals.yaml
  system/esphome_core.yaml
  features/ble/{ble_improv,ble_stub}.yaml
  features/airq/airq_history.yaml
  features/page_rotation/page_rotation.yaml
  features/ota/ota_channel.yaml
  features/weather/weather_ha.yaml      # optional
  features/wireguard/wireguard.yaml     # optional
```

Calibration: [`CALIBRATION.md`](CALIBRATION.md).
