# Sensor calibration and heat management (HALO)

HALO shares the same sensor family as many ESPHome air-quality boards
(SCD4x, SEN55, MiCS-4514, BME280). These notes cover the controls exposed
in firmware on branch `feat/community-adoption`.

## CO₂ (SCD4x)

- **Forced calibration:** Home Assistant button `Calibrate SCD40 To 420ppm`,
  or API service `calibrate_co2_value` with a target ppm. Take the device
  outdoors (or to known fresh air) for several minutes before calibrating.
- **Automatic self-calibration (ASC):** Switch `CO2 Auto Calibration`.
  The SCD4x forgets ASC across power loss. HALO uses stop → write ASC →
  start periodic measurement so the command is accepted while idle.
- **Pressure compensation:** CO₂ readings use BME280 pressure as
  `ambient_pressure_compensation_source`.

## SEN55 temperature / humidity

- The USB-powered display board self-heats. Default
  **SEN55 Temperature Offset** is **6.0 °C** (subtracted from the raw
  reading). Tune the offset entity until SEN55 tracks a reference room
  thermometer.
- **SEN55 Humidity Offset** defaults to 0; adjust if RH is systematically
  high/low versus a reference.
- Continuous always-on reporting favors responsiveness over thermal
  settling. If you need better absolute T/RH accuracy, increase airflow
  around the vents and re-tune offsets after a 30+ minute warm-up.

## SEN55 model / NOx / fan clean

- Firmware forces `model: sen55` so NOx stays enabled on modules that
  misreport as SEN54 (requires ESPHome **2026.7.2+**).
- Button **Clean SEN55** (and API `sen55_clean`) runs the fan autoclean cycle.
- VOC index thresholds for the **VOC Quality** text sensor follow Sensirion
  guidance (Improved / Normal / Abnormal / …).

## AQI

- **Computed AQI** on the display is the on-device US EPA PM2.5 banding used
  by the LVGL UI.
- **NowCast AQI** is the native ESPHome `aqi` platform (PM2.5 + PM10) for
  Home Assistant — it does not replace the display math.

## Offline history

When the `airq_history` package is enabled (default), HALO buffers readings
in PSRAM while Home Assistant is unreachable and flushes
`esphome.aqi_history_batch` events on reconnect. Optional AppDaemon ingest
lives under [`HomeAssistant/AppDaemon/apps/`](../../../../HomeAssistant/AppDaemon/apps/).

## MiCS-4514 gases

Treat multi-gas readings as relative indicators, not safety-grade CO or
leak alarms. Cross-sensitivity (especially methane) is expected.
