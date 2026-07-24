# Modular packages (HALO)

Default [`Halo-v1.yaml`](Halo-v1.yaml) enables a **full** profile that matches
the previous monolithic flash-and-go experience, plus community-adopted
extras (ASC, NowCast, offline history, OTA channel, page-rotation controls).

```
packages/
  base/globals.yaml              # boot / OTA / rotation shared state
  system/esphome_core.yaml       # ESP32-S3 + PSRAM/mbedTLS/TCP memory stack
  features/
    ble/ble_improv.yaml          # default BLE (Improv)
    ble/ble_stub.yaml            # no BLE (~40KB savings)
    airq/airq_history.yaml       # offline AQI ring buffer
    page_rotation/…              # HA controls for multi-page rotation
    ota/ota_channel.yaml         # Stable/Beta http_request OTA
    weather/weather_ha.yaml      # optional HA weather sensors (off by default)
    wireguard/wireguard.yaml     # optional VPN (off by default)
```

Toggle features by commenting/uncommenting lines in `Halo-v1.yaml`.
Keep **exactly one** BLE package enabled (`ble_improv` or `ble_stub`).

Sensor calibration notes: [`CALIBRATION.md`](CALIBRATION.md).
