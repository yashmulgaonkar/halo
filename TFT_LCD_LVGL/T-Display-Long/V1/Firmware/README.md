# HALO LVGL firmware

Portrait firmware for the [LilyGO T-Display-S3-Long](https://github.com/Xinyuan-LilyGO/T-Display-S3-Long) (ESP32-S3, 180×640). It replaces the ESPHome package in `TFT_LCD/T-Display-Long/V1/Firmware/ESPHome` with a PlatformIO build: LVGL on the panel, the same sensor set, and MQTT discovery so Home Assistant still receives the readings.

The screen is a vertical stack of three pages. Swipe, or tap BOOT, to move between them. Hold a temperature, humidity, or gas row to adjust its offset, then tap outside the panel to close it.

1. **Air** — AQI category and color, temperature, humidity, CO2, particles, VOC.
2. **Gases** — CO, NO2, H2, methane, ethanol, ammonia, NOx, and pressure, each with the same color bands as the ESPHome UI.
3. **Device** — firmware, network, and which sensors answered on the bus.

## Install from the browser

Chrome or Edge: [https://yashmulgaonkar.github.io/halo/](https://yashmulgaonkar.github.io/halo/). Choose **Install HALO LVGL firmware (new UI)**. That writes a full image, including the bootloader and partition table. When the installer asks, erase the flash so an ESPHome layout is not left behind.

After it boots with no Wi-Fi saved, join **Halo Hotspot** and open `http://192.168.4.1`.

The same image is attached to the GitHub release tagged `lvgl-<version>` once it is built from `main`.

## Build and flash

From this directory, with PlatformIO on `PATH`:

```powershell
pio run
pio run -t upload
pio device monitor
```

The PlatformIO VS Code extension keeps its CLI in the user profile. If `pio` is not recognized:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```

`python -m platformio` only works when that Python has PlatformIO installed. The default `python` on this machine may be a newer interpreter that does not.

The board uses the USB port for both upload and the serial log. If upload cannot find the port, hold BOOT, tap RST, release BOOT, and try again.

## First boot

With no Wi-Fi saved, the device opens a hotspot named **Halo Hotspot**. Join it and open `http://192.168.4.1`. Enter the home Wi-Fi network and the MQTT broker. A blank password field keeps the previous password.

Hold BOOT for 5 seconds to forget Wi-Fi and return to that hotspot.

After it joins Wi-Fi, the same page is at `http://halo-xxxxxx.local/` (the last three bytes of the MAC). **Update firmware** checks [the installer site](https://yashmulgaonkar.github.io/halo/firmware-lvgl/version.txt) and, when that version is newer, downloads the app image over Wi-Fi. Home Assistant shows the same install on the device’s firmware update. `/api/state` returns the latest readings as JSON. ArduinoOTA uses the same hostname.

Timezone is a POSIX string. The default is US Pacific: `PST8PDT,M3.2.0,M11.1.0`.

## Home Assistant

Discovery is published to `homeassistant/.../config` when MQTT connects. The device id looks like `halo_a1b2c3`.

Live sensors match the ESPHome names: CO2, the MiCS-4514 gases, BME280, SEN55 particles / temperature / humidity / VOC / NOx, computed AQI, NowCast AQI (after 12 hourly samples), uptime, and RSSI. VOC quality and the firmware version are text sensors.

Also exposed:

- Light **Halo** (11-LED ring on GPIO47, GRB). Effects: None, Pulse, Rainbow, Scan, Twinkle, Color Wipe.
- Light **Display Backlight**.
- Switches for startup blink, 24-hour clock, CO2 automatic calibration, history buffering, and page rotation.
- Numbers for the SEN55 temperature and humidity offsets (temperature default 6.0 °C), the NO2 offset, history interval and coverage, and the page interval.
- Select **Display Temperature Unit** (Fahrenheit, Celsius, Kelvin). The display converts; MQTT temperature stays in °C.
- Buttons to reboot, calibrate the SCD40 to 420 ppm, and run the SEN55 fan clean.

The ring uses a dual-core bitbang driver so Wi-Fi does not flicker the LEDs. Until MQTT connects, or for 30 seconds, it plays a rainbow when startup blink is on, then turns off and remembers white so the next color command is not pure blue.

While the broker is unreachable, AQI samples are stored in PSRAM and published to `halo/<id>/history` after reconnect. Each message is a JSON array of the same short keys the ESPHome history package used (`ts`, `q`, `c2`, `n2`, `co`, `h2`, `c4`, `et`, `n3`, `p1`, `p25`, `p4`, `p10`, `tm`, `vc`, `rh`).

## Pins

| Function | GPIO |
| --- | --- |
| QSPI display CS, SCK, D0–D3, RST | 12, 17, 13, 18, 21, 14, 16 |
| Backlight | 1 |
| Touch SDA, SCL, IRQ | 15, 10, 11 |
| Sensor SDA, SCL (Qwiic) | 9, 48 |
| LED ring, 11 × GRB | 47 |
| BOOT / page | 0 |

Sensors on the Qwiic bus: SCD4x `0x62`, SEN55 `0x69`, BME280 `0x76` or `0x77`, MiCS-4514 `0x75`. The MiCS reading is the same ppm conversion the ESPHome component used, including the 3-minute warmup. It is not a safety alarm.

## Not carried over

These ESPHome packages are not in this build:

- Native ESPHome API. Home Assistant uses MQTT discovery instead.
- BLE provisioning. The release ESPHome profile was already a stub. Use the hotspot page.
- Optional weather-from-Home-Assistant and WireGuard packages.
- The ESPHome update channel at `firmware/manifest.json`. That file stays an ESPHome image so existing devices keep updating. This build is published at `firmware-lvgl/`. `firmware.factory.bin` is the USB image. `firmware.bin` and `version.txt` are the Wi-Fi update used by the device portal and Home Assistant. ArduinoOTA still uses the device hostname.
