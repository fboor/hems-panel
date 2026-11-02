#!/usr/bin/env python3
"""WARP Energy Manager V2 mock for the panel emulator.

The mock serves the same module API as a real WARP device:

    /info/name, /info/version
    /meters/0..6/config, /value_ids, /values
    /power_manager/low_level_state

It also keeps the old flat /json endpoint, so the fallback parser can still be
exercised. The scenarios only change the source values; the meter arrays are
generated from them exactly like the real RCT Power virtual meters.

    python3 warp_mock.py --port 8082 --scenario midsummer
    python3 warp_mock.py --scenario island --live

Scenario can also be chosen per request: /meters/2/values?scenario=low_solar
"""

import argparse
import calendar
import datetime
import json
import math
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BASE = {
    "device_name": "wem2-AbCd",
    "firmware_version": "2.3.1",
    "grid_power_l1": 452.0,
    "grid_power_l2": 432.0,
    "grid_power_l3": 410.0,
    "grid_exchange": 1294.0,
    "grid_voltage_l1": 230.0,
    "grid_voltage_l2": 231.0,
    "grid_voltage_l3": 229.0,
    "grid_frequency": 50.0,
    "grid_import_wh": 18000.0,
    "grid_export_wh": 4000.0,
    "pv_power_a": 1234.5,
    "pv_power_b": 0.0,
    "pv_voltage_a": 460.0,
    "pv_voltage_b": 0.0,
    "pv_energy_a_wh": 16000.0,
    "pv_energy_b_wh": 0.0,
    "external_power": 42.0,
    "household_power_l1": 310.0,
    "household_power_l2": 305.0,
    "household_power_l3": 299.0,
    "household_power": 914.0,
    "household_energy_wh": 16000.0,
    "battery_soc": 85.5,
    "battery_power": 25.3,
    "battery_voltage": 51.2,
    "battery_current": 0.5,
    "battery_temperature": 22.0,
    "battery_charge_wh": 5000.0,
    "battery_discharge_wh": 4200.0,
    "island_flag": 0,
    "core_temperature": 45.2,
    "heat_sink_temperature": 55.3,
    "day_generation_wh": 12500.0,
    "day_load_wh": 8300.0,
    "day_feed_in_wh": 3100.0,
    "day_grid_draw_wh": 5700.0,
    "forecast_today_wh": 24100.0,
}


def scenario(name):
    d = dict(BASE)
    if name == "midsummer":
        pass
    elif name == "evening":
        d.update(
            grid_power_l1=-150.0,
            grid_power_l2=-130.0,
            grid_power_l3=-120.0,
            grid_exchange=-400.0,
            grid_import_wh=19000.0,
            grid_export_wh=5600.0,
            pv_power_a=0.0,
            pv_power_b=0.0,
            pv_voltage_a=0.0,
            pv_voltage_b=0.0,
            battery_soc=92.0,
            battery_power=-620.0,
            battery_current=-12.1,
            battery_discharge_wh=4700.0,
            island_flag=0,
            forecast_today_wh=22000.0,
        )
    elif name == "island":
        d.update(
            grid_power_l1=0.0,
            grid_power_l2=0.0,
            grid_power_l3=0.0,
            grid_exchange=0.0,
            grid_voltage_l1=0.0,
            grid_voltage_l2=0.0,
            grid_voltage_l3=0.0,
            pv_power_a=800.0,
            pv_power_b=350.0,
            pv_voltage_a=420.0,
            pv_voltage_b=430.0,
            battery_soc=78.0,
            battery_power=-180.0,
            battery_current=-3.5,
            island_flag=1,
            external_power=0.0,
            forecast_today_wh=18000.0,
        )
    elif name == "low_solar":
        d.update(
            grid_power_l1=180.0,
            grid_power_l2=175.0,
            grid_power_l3=170.0,
            grid_exchange=525.0,
            pv_power_a=150.0,
            pv_power_b=40.0,
            battery_soc=35.0,
            battery_power=-80.0,
            battery_current=-1.6,
            island_flag=0,
            forecast_today_wh=6500.0,
        )
    elif name == "grid_failure":
        d.update(
            grid_power_l1=0.0,
            grid_power_l2=0.0,
            grid_power_l3=0.0,
            grid_exchange=0.0,
            grid_voltage_l1=0.0,
            grid_voltage_l2=0.0,
            grid_voltage_l3=0.0,
            pv_power_a=600.0,
            pv_power_b=200.0,
            battery_soc=65.0,
            battery_power=-120.0,
            battery_current=-2.4,
            island_flag=1,
            external_power=0.0,
            forecast_today_wh=15000.0,
        )
    return d


SCENARIOS = ("midsummer", "evening", "island", "low_solar", "grid_failure")


def live_tick(d, t):
    """Small deterministic movement so the page visibly updates."""
    d = dict(d)
    slow = math.sin(t / 7.0)
    fast = math.sin(t / 2.3)
    d["grid_power_l1"] = round(d["grid_power_l1"] + 40 * slow, 1)
    d["grid_power_l2"] = round(d["grid_power_l2"] + 30 * math.sin(t / 8.1), 1)
    d["grid_power_l3"] = round(d["grid_power_l3"] + 25 * math.sin(t / 9.7), 1)
    d["grid_exchange"] = round(
        d["grid_power_l1"] + d["grid_power_l2"] + d["grid_power_l3"], 1
    )
    d["pv_power_a"] = round(max(0.0, d["pv_power_a"] + 120 * fast), 1)
    d["pv_power_b"] = round(max(0.0, d["pv_power_b"] + 80 * slow), 1)
    d["household_power_l1"] = round(d["household_power_l1"] + 20 * fast, 1)
    d["household_power_l2"] = round(d["household_power_l2"] + 15 * slow, 1)
    d["household_power_l3"] = round(d["household_power_l3"] + 10 * fast, 1)
    d["household_power"] = round(
        d["household_power_l1"] + d["household_power_l2"] + d["household_power_l3"], 1
    )
    d["battery_soc"] = round(min(100.0, max(0.0, d["battery_soc"] + 0.2 * slow)), 1)
    d["battery_power"] = round(d["battery_power"] + 15 * slow, 1)
    d["battery_current"] = round(d["battery_power"] / max(1.0, d["battery_voltage"]), 2)
    d["day_generation_wh"] = round(d["day_generation_wh"] + t * 0.4, 1)
    d["pv_energy_a_wh"] = round(d["pv_energy_a_wh"] + t * 0.4, 1)
    d["grid_import_wh"] = round(d["grid_import_wh"] + t * 0.05, 1)
    d["household_energy_wh"] = round(d["household_energy_wh"] + t * 0.3, 1)
    return d


def meter_payloads(d):
    """Return (config, value_ids, values) for the seven meter slots."""
    pv_total = d["pv_power_a"] + d["pv_power_b"]
    pv_energy = d["pv_energy_a_wh"] + d["pv_energy_b_wh"]
    pv_voltage_avg = 0.0
    if d["pv_voltage_a"] > 0 or d["pv_voltage_b"] > 0:
        pv_voltage_avg = (d["pv_voltage_a"] + d["pv_voltage_b"]) / 2.0

    slots = [
        (
            [8, {
                "display_name": "RCT Verbrauch",
                "location": 6,
                "excluded": False,
                "host": "127.0.0.1",
                "port": 8899,
                "virtual_meter": 4,
            }],
            [39, 48, 57, 215, 74],
            [
                d["household_power_l1"],
                d["household_power_l2"],
                d["household_power_l3"],
                d["household_energy_wh"],
                d["household_power"],
            ],
        ),
        (
            [8, {
                "display_name": "RCT PV",
                "location": 7,
                "excluded": False,
                "host": "127.0.0.1",
                "port": 8899,
                "virtual_meter": 5,
            }],
            [421, 442, 454, 422, 443, 456, 430, 452, 453, 472],
            [
                d["pv_voltage_a"],
                d["pv_power_a"],
                d["pv_energy_a_wh"],
                d["pv_voltage_b"],
                d["pv_power_b"],
                d["pv_energy_b_wh"],
                pv_voltage_avg,
                pv_total,
                -pv_total,
                pv_energy,
            ],
        ),
        (
            [8, {
                "display_name": "RCT Netz",
                "location": 4,
                "excluded": False,
                "host": "127.0.0.1",
                "port": 8899,
                "virtual_meter": 2,
            }],
            [1, 2, 3, 39, 48, 57, 74, 211, 209, 361, 362, 363],
            [
                d["grid_voltage_l1"],
                d["grid_voltage_l2"],
                d["grid_voltage_l3"],
                d["grid_power_l1"],
                d["grid_power_l2"],
                d["grid_power_l3"],
                d["grid_exchange"],
                d["grid_export_wh"],
                d["grid_import_wh"],
                d["grid_frequency"],
                d["grid_frequency"],
                d["grid_frequency"],
            ],
        ),
        (
            [8, {
                "display_name": "RCT Akku",
                "location": 5,
                "excluded": False,
                "host": "127.0.0.1",
                "port": 8899,
                "virtual_meter": 3,
            }],
            [10, 396, 404, 413, 415, 387, 381],
            [
                d["battery_voltage"],
                d["battery_current"],
                d["battery_power"],
                d["battery_charge_wh"],
                d["battery_discharge_wh"],
                d["battery_soc"],
                d["battery_temperature"],
            ],
        ),
        ([0, None], [], []),
        ([0, None], [], []),
        ([0, None], [], []),
    ]
    return slots


class Handler(BaseHTTPRequestHandler):
    scenario_name = "midsummer"
    live = False
    started = time.time()

    def log_message(self, fmt, *args):
        pass

    def _query(self):
        if "?" not in self.path:
            return {}
        out = {}
        for part in self.path.split("?", 1)[1].split("&"):
            if "=" in part:
                k, v = part.split("=", 1)
                out[k] = v
        return out

    def _data(self):
        q = self._query()
        name = q.get("scenario", self.scenario_name)
        if name not in SCENARIOS:
            name = "midsummer"
        data = scenario(name)
        if self.live or q.get("live") == "1":
            data = live_tick(data, time.time() - self.started)
        return data

    def _send(self, body, status=200, content_type="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body)
        if isinstance(body, str):
            body = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        d = self._data()

        if path == "/":
            body = (
                "<html><body><h1>WARP mock</h1>"
                "<p>Module API: <a href='/info/name'>/info/name</a>, "
                "<a href='/meters/2/values'>/meters/2/values</a>, "
                "<a href='/power_manager/low_level_state'>"
                "/power_manager/low_level_state</a></p>"
                "<p>Flat fallback: <a href='/json'>/json</a></p>"
                "<p>Scenarios: " + ", ".join(SCENARIOS) + "</p></body></html>"
            )
            self._send(body, content_type="text/html")
            return

        if path == "/json":
            self._send(d)
            return

        if path == "/info/name":
            self._send({
                "name": d["device_name"],
                "type": "wem2",
                "display_type": "WARP Energy Manager 2.0",
                "uid": "mock",
            })
            return

        if path == "/info/version":
            self._send({
                "firmware": d["firmware_version"],
                "config": "1.5.0",
                "config_type": "wem",
            })
            return

        slots = meter_payloads(d)
        if path.startswith("/meters/"):
            rest = path[len("/meters/"):]
            if "/" not in rest:
                self.send_error(404)
                return
            index_text, endpoint = rest.split("/", 1)
            try:
                index = int(index_text)
            except ValueError:
                self.send_error(404)
                return
            if index < 0 or index >= len(slots):
                self.send_error(404)
                return
            config, value_ids, values = slots[index]
            if endpoint == "config":
                self._send(config)
            elif endpoint == "value_ids":
                self._send(value_ids)
            elif endpoint == "values":
                self._send(values)
            elif endpoint == "state":
                self._send(None)
            elif endpoint == "errors":
                self._send({"timeout": 0, "checksum_mismatch": 0})
            else:
                self.send_error(404)
            return

        if path == "/power_manager/low_level_state":
            self._send({
                "power_at_meter": d["grid_exchange"],
                "power_at_battery": d["battery_power"],
                "battery_soc": d["battery_soc"],
                "power_available": max(0.0, d["pv_power_a"] - d["household_power"]),
                "i_meter": [0.0, 0.0, 0.0],
                "i_pp_max": [0.0, 0.0, 0.0],
                "i_pp_mavg": [0.0, 0.0, 0.0],
                "i_pp": [0.0, 0.0, 0.0],
                "max_current_limited": 16000,
                "is_3phase": True,
                "charging_blocked": 0,
            })
            return

        if path == "/solar_forecast/state":
            self._send({"wh_today": d.get("forecast_today_wh", 24100.0)})
            return

        if path == "/power_manager/state":
            self._send({"config_error_flags": 0, "external_control": 0})
            return

        if path == "/energy_manager/state":
            self._send({
                "error_flags": 0,
                "config_error_flags": 0,
                "em_version": 2,
                "inputs": [False, False, False, False],
                "sg_ready_outputs": [False, False],
                "relays": [False, False],
            })
            return

        if path == "/meter/all_values":
            # Legacy API: the primary meter (grid) in the old 65-value layout.
            values = [None] * 65
            values[6] = d["grid_power_l1"]
            values[7] = d["grid_power_l2"]
            values[8] = d["grid_power_l3"]
            values[24] = d["grid_exchange"]
            self._send(values)
            return

        if path == "/meter/values":
            self._send({
                "power": d["grid_exchange"],
                "energy_rel": None,
                "energy_abs": None,
            })
            return

        self.send_error(404)

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if path == "/energy_manager/history_energy_manager_daily":
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length) if length > 0 else b"{}"
            try:
                request = json.loads(raw)
            except json.JSONDecodeError:
                request = {}
            now = datetime.datetime.now()
            year = int(request.get("year", now.year))
            month = int(request.get("month", now.month))
            days = calendar.monthrange(year, month)[1]
            data = [None] * (days * 17)
            d = self._data()
            if year == now.year and month == now.month:
                today = now.day - 1
                base = today * 17
                data[base + 0] = round(d["household_energy_wh"] / 1000.0, 3)
                data[base + 1] = round((d["pv_energy_a_wh"] + d["pv_energy_b_wh"]) / 1000.0, 3)
                data[base + 2] = round(d["grid_export_wh"] / 1000.0, 3)
                data[base + 3] = round(d["battery_charge_wh"] / 1000.0, 3)
                data[base + 7] = round(d["grid_import_wh"] / 1000.0, 3)
                data[base + 8] = round((d["pv_energy_a_wh"] + d["pv_energy_b_wh"]) / 1000.0, 3)
                data[base + 9] = round(d["grid_import_wh"] / 1000.0, 3)
                data[base + 10] = round(d["battery_discharge_wh"] / 1000.0, 3)
            self._send(data)
            return
        self.send_error(404)

    def do_HEAD(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()


def main():
    ap = argparse.ArgumentParser(description="WARP Energy Manager V2 mock")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8082)
    ap.add_argument("--scenario", default="midsummer", choices=SCENARIOS)
    ap.add_argument("--live", action="store_true", help="values drift over time")
    args = ap.parse_args()

    Handler.scenario_name = args.scenario
    Handler.live = args.live

    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    print("WARP mock")
    print(f"  module API: http://{args.host}:{args.port}/info/name")
    print(f"  flat JSON:  http://{args.host}:{args.port}/json")
    print(f"  scenario: {args.scenario}   live: {args.live}")
    print("  scenarios: " + ", ".join(SCENARIOS))
    print("  Ctrl-C to stop")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        srv.shutdown()


if __name__ == "__main__":
    main()
