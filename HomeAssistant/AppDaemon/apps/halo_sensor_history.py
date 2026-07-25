"""
Halo offline AQI history ingest for AppDaemon.

Listens for ``esphome.aqi_history_batch`` events fired by the device and
imports hourly statistics via the Home Assistant WebSocket API
(``recorder/import_statistics`` — HA 2026.x).

Configure statistic entity IDs in apps.yaml / halo_sensor_history.yaml —
do not hard-code a specific MAC-suffixed device.
"""

from __future__ import annotations

import json
from datetime import datetime, timezone

import aiohttp
import appdaemon.plugins.hass.hassapi as hass


DEFAULT_SENSORS = [
    # (json_key, config_arg_for_statistic_id, unit)
    ("q", "statistic_id_aqi", None),
    ("c2", "statistic_id_co2", "ppm"),
    ("n2", "statistic_id_no2", "ppm"),
    ("co", "statistic_id_co", "ppm"),
    ("h2", "statistic_id_h2", "ppm"),
    ("c4", "statistic_id_ch4", "ppm"),
    ("et", "statistic_id_ethanol", "ppm"),
    ("n3", "statistic_id_nh3", "ppm"),
    ("p1", "statistic_id_pm1", "µg/m³"),
    ("p25", "statistic_id_pm25", "µg/m³"),
    ("p4", "statistic_id_pm4", "µg/m³"),
    ("p10", "statistic_id_pm10", "µg/m³"),
    ("tm", "statistic_id_temp", "°C"),
    ("vc", "statistic_id_voc", ""),
    ("rh", "statistic_id_humidity", "%"),
]


class HaloSensorHistory(hass.Hass):
    def initialize(self):
        self._device_filter = self.args.get("device_filter")
        self._ha_url = self.args["ha_url"].rstrip("/")
        self._ha_token = self.args["ha_token"]
        self._ws_url = (
            self._ha_url.replace("https://", "wss://", 1).replace("http://", "ws://", 1)
            + "/api/websocket"
        )
        self._sensors = []
        for key, arg_name, unit in DEFAULT_SENSORS:
            entity = self.args.get(arg_name) or (
                self.args.get("statistic_id") if key == "q" else None
            )
            if entity:
                self._sensors.append((key, entity, unit))

        if not self._sensors:
            self.log(
                "No statistic_id_* entities configured — set at least statistic_id (AQI).",
                level="ERROR",
            )
            return

        self.listen_event(self.handle_batch, self.args.get("event_type", "esphome.aqi_history_batch"))
        self.log(f"Listening for AQI history batches (sensors={len(self._sensors)})")

    async def handle_batch(self, event_name, data, kwargs):
        device = data.get("device", "unknown")
        if self._device_filter and device != self._device_filter:
            return

        try:
            readings = json.loads(data.get("readings", "[]"))
        except Exception as exc:
            self.log(f"[{device}] Bad readings JSON: {exc}", level="ERROR")
            return
        if not readings:
            return

        sensor_stats = {}
        for key, statistic_id, unit in self._sensors:
            buckets = {}
            for row in readings:
                val = row.get(key)
                if val is None:
                    continue
                try:
                    val = float(val)
                    dt = datetime.fromtimestamp(int(row["ts"]), tz=timezone.utc)
                    hour = dt.replace(minute=0, second=0, microsecond=0).isoformat()
                    bucket = buckets.setdefault(hour, {"sum": 0.0, "count": 0, "min": val, "max": val})
                    bucket["sum"] += val
                    bucket["count"] += 1
                    bucket["min"] = min(bucket["min"], val)
                    bucket["max"] = max(bucket["max"], val)
                except Exception:
                    continue
            if buckets:
                sensor_stats[key] = (
                    statistic_id,
                    unit,
                    [
                        {
                            "start": iso,
                            "mean": b["sum"] / b["count"],
                            "min": b["min"],
                            "max": b["max"],
                        }
                        for iso, b in sorted(buckets.items())
                    ],
                )

        if not sensor_stats:
            return

        try:
            await self._import_via_websocket(device, sensor_stats)
        except Exception as exc:
            self.log(f"[{device}] Import failed: {exc}", level="ERROR")

    async def _import_via_websocket(self, device: str, sensor_stats: dict) -> None:
        msg_id = 1
        async with aiohttp.ClientSession() as session:
            async with session.ws_connect(self._ws_url) as ws:
                opening = await ws.receive_json()
                if opening.get("type") != "auth_required":
                    raise RuntimeError(f"Unexpected WS open: {opening}")
                await ws.send_json({"type": "auth", "access_token": self._ha_token})
                auth = await ws.receive_json()
                if auth.get("type") != "auth_ok":
                    raise RuntimeError(f"WS auth failed: {auth}")

                for key, (statistic_id, unit, stats) in sensor_stats.items():
                    await ws.send_json(
                        {
                            "id": msg_id,
                            "type": "recorder/import_statistics",
                            "metadata": {
                                "statistic_id": statistic_id,
                                "source": "recorder",
                                "unit_of_measurement": unit,
                                "has_mean": True,
                                "has_sum": False,
                                "name": None,
                            },
                            "stats": stats,
                        }
                    )
                    result = await ws.receive_json()
                    if result.get("success") is False:
                        self.log(
                            f"[{device}] Failed {statistic_id}: {result.get('error', result)}",
                            level="ERROR",
                        )
                    else:
                        self.log(f"[{device}] Imported {len(stats)} pts → {statistic_id}")
                    msg_id += 1
