#!/usr/bin/env python3
"""WARP Energy Manager V2 mock for the panel emulator.

The mock serves the same module API as a real WARP device:

    /info/name, /info/version
    /meters/0..6/config, /value_ids, /values
    /power_manager/low_level_state
    /charge_manager/state
    /solar_forecast/state

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
    "day_battery_charge_wh": 1800.0,
    "day_battery_discharge_wh": 1200.0,
    "island_flag": 0,
    "core_temperature": 45.2,
    "heat_sink_temperature": 55.3,
    "day_generation_wh": 12500.0,
    "day_load_wh": 8300.0,
    "day_feed_in_wh": 3100.0,
    "day_grid_draw_wh": 5700.0,
    "forecast_today_wh": 24100.0,
    "forecast_today_remaining_wh": 12000.0,
    # Charge manager. BASE has no vehicle; the wallbox scenarios set these.
    "charge_state": 0,            # CASState: 0 NoVehicle, 4 Charging, ...
    "charge_current": 0.0,        # allocated current, mA
    "charge_phases": 0,           # allocated phases, 0..3
    "charge_supported_phases": 1,  # 1/3, bit 2 set when switchable
    "charge_name": "WARP Wallbox",
    "charge_meter_power": None,   # W when the wallbox has its own meter
    "charge_meter_energy_wh": None,  # cumulative counter of that meter
    "charge_meter_day_wh": None,     # today's part, for the daily history
    # Global and per-charger charge modes (ConfigChargeMode).
    "charge_mode": 2,             # PV, like the WARP default
    "charge_modes": None,         # per-charger list; None = use charge_mode
    "supported_charge_modes": [1, 2, 3, 0],  # Off, PV, Min+PV, Fast
    "charger_specs": None,        # explicit multi-charger layout
    # Heating (SG-Ready) and outdoor temperatures.
    "heating_enabled": True,
    "sgr_blocking": True,
    "sgr_extended": False,
    "p14enwg": False,
    "automation_override": False,
    "sgr_override_until": 0,       # minutes since epoch, 0 = inactive
    "temp_current": 52,            # degC x 10
    "temp_today_min": 31,
    "temp_today_max": 96,
    "temp_today_avg": 63,
    "temp_tomorrow_min": 28,
    "temp_tomorrow_max": 88,
    "temp_tomorrow_avg": 57,
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
            forecast_today_remaining_wh=800.0,
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
            forecast_today_remaining_wh=7000.0,
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
            forecast_today_remaining_wh=2500.0,
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
            forecast_today_remaining_wh=6000.0,
        )
    elif name == "wallbox":
        d.update(
            charge_state=4,
            charge_current=10000.0,
            charge_phases=1,
            charge_supported_phases=1,
            forecast_today_wh=20500.0,
            forecast_today_remaining_wh=9000.0,
        )
    elif name == "wallbox_3p":
        d.update(
            charge_state=4,
            charge_current=16000.0,
            charge_phases=3,
            charge_supported_phases=7,
            forecast_today_wh=20500.0,
            forecast_today_remaining_wh=9000.0,
        )
    elif name == "wallbox_meter":
        d.update(
            charge_state=4,
            charge_current=16000.0,
            charge_phases=3,
            charge_supported_phases=7,
            charge_meter_power=11100.0,
            charge_meter_energy_wh=234567.0,
            charge_meter_day_wh=4567.0,
            forecast_today_wh=20500.0,
            forecast_today_remaining_wh=9000.0,
        )
    elif name == "wallboxes":
        # Three boxes in different states. The first has its own meter, the
        # other two are idle and exercise the per-charger mode buttons.
        d.update(
            charger_specs=[
                {
                    "name": "Carport West",
                    "state": 4,
                    "current": 16000.0,
                    "phases": 3,
                    "supported": 7,
                    "meter_power": 11100.0,
                    "meter_energy_wh": 120000.0,
                    "meter_day_wh": 2000.0,
                },
                {
                    "name": "Garage",
                    "state": 0,
                    "current": 0.0,
                    "phases": 1,
                    "supported": 1,
                    "meter_power": 0.0,
                    "meter_energy_wh": 45000.0,
                    "meter_day_wh": 800.0,
                },
                {
                    "name": "Stellplatz",
                    "state": 6,
                    "current": 0.0,
                    "phases": 1,
                    "supported": 1,
                    "meter_power": 0.0,
                    "meter_energy_wh": 8000.0,
                    "meter_day_wh": 0.0,
                },
            ],
            charge_mode=2,
            charge_modes=[2, 1, 0],
            forecast_today_wh=20500.0,
            forecast_today_remaining_wh=9000.0,
        )
    return d


SCENARIOS = (
    "midsummer",
    "evening",
    "island",
    "low_solar",
    "grid_failure",
    "wallbox",
    "wallbox_3p",
    "wallbox_meter",
    "wallboxes",
)


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


def chargers_for(d):
    """Every wallbox in the scenario, as a list of per-charger dicts."""
    specs = d.get("charger_specs")
    if specs is not None:
        return specs
    return [{
        "name": d.get("charge_name") or "WARP Wallbox",
        "state": d.get("charge_state") or 0,
        "current": d.get("charge_current") or 0.0,
        "phases": d.get("charge_phases") or 0,
        "supported": d.get("charge_supported_phases") or 1,
        "meter_power": d.get("charge_meter_power"),
        "meter_energy_wh": d.get("charge_meter_energy_wh"),
        "meter_day_wh": d.get("charge_meter_day_wh"),
    }]


def metered_chargers(d):
    """Chargers with their own meter, in slot order after the four base meters."""
    return [spec for spec in chargers_for(d)
            if spec.get("meter_power") is not None]


def charger_phase_count(spec):
    phases = int(spec.get("phases") or 0)
    if phases < 1:
        phases = int(spec.get("supported") or 1) & 3
    return 3 if phases >= 3 else 1


def wallbox_phase_count(d):
    for spec in chargers_for(d):
        active = float(spec.get("current") or 0.0) > 0.0
        if not active and spec.get("meter_power") is not None:
            active = float(spec["meter_power"]) > 0.0
        if active and charger_phase_count(spec) == 3:
            return 3
    return 1


def wallbox_phase_voltages(d):
    volts = [
        d.get("grid_voltage_l1", 230.0),
        d.get("grid_voltage_l2", 230.0),
        d.get("grid_voltage_l3", 230.0),
    ]
    return [v if v >= 50.0 else 230.0 for v in volts]


def charger_power(spec, d):
    """Power from a wallbox meter, or the same estimate the panel uses."""
    meter = spec.get("meter_power")
    if meter is not None:
        return float(meter)
    amps = float(spec.get("current") or 0.0) / 1000.0
    volts = wallbox_phase_voltages(d)
    return amps * (sum(volts) if charger_phase_count(spec) == 3 else volts[0])


def wallbox_power(d):
    return sum(charger_power(spec, d) for spec in chargers_for(d))


def wh_to_kwh(wh):
    """WARP meter energy values are kWh; the scenario data is kept in Wh."""
    return round(float(wh) / 1000.0, 3)


def meter_payloads(d):
    """Return (config, value_ids, values) for the seven meter slots."""
    pv_total = d["pv_power_a"] + d["pv_power_b"]
    pv_energy = wh_to_kwh(d["pv_energy_a_wh"] + d["pv_energy_b_wh"])
    pv_voltage_avg = 0.0
    if d["pv_voltage_a"] > 0 or d["pv_voltage_b"] > 0:
        pv_voltage_avg = (d["pv_voltage_a"] + d["pv_voltage_b"]) / 2.0

    # The load meter sits behind the house connection and therefore includes
    # the wallbox, like it does on the real installation.
    wallbox_w = wallbox_power(d)
    house_l = [
        d["household_power_l1"],
        d["household_power_l2"],
        d["household_power_l3"],
    ]
    if wallbox_w > 0:
        if wallbox_phase_count(d) == 3:
            for i in range(3):
                house_l[i] = round(house_l[i] + wallbox_w / 3.0, 1)
        else:
            house_l[0] = round(house_l[0] + wallbox_w, 1)
    house_total = round(sum(house_l), 1)

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
                house_l[0],
                house_l[1],
                house_l[2],
                wh_to_kwh(d["household_energy_wh"]),
                house_total,
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
                wh_to_kwh(d["pv_energy_a_wh"]),
                d["pv_voltage_b"],
                d["pv_power_b"],
                wh_to_kwh(d["pv_energy_b_wh"]),
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
                wh_to_kwh(d["grid_export_wh"]),
                wh_to_kwh(d["grid_import_wh"]),
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
                wh_to_kwh(d["battery_charge_wh"]),
                wh_to_kwh(d["battery_discharge_wh"]),
                d["battery_soc"],
                d["battery_temperature"],
            ],
        ),
    ]
    # One meter slot per metered wallbox, so the panel has to sum several
    # energy counters in the `wallboxes` scenario.
    for spec in metered_chargers(d):
        wallbox_w = charger_power(spec, d)
        if charger_phase_count(spec) == 3:
            phases = [wallbox_w / 3.0] * 3
        else:
            phases = [wallbox_w, 0.0, 0.0]
        ids = [39, 48, 57, 74]
        values = [round(phases[0], 1), round(phases[1], 1),
                  round(phases[2], 1), round(wallbox_w, 1)]
        energy = spec.get("meter_energy_wh")
        if energy is not None:
            ids.append(209)
            values.append(wh_to_kwh(energy))
        slots.append((
            [8, {
                "display_name": spec.get("name") or "WARP Wallbox",
                "location": 2,
                "excluded": False,
                "host": "127.0.0.1",
                "port": 8899,
                "virtual_meter": 0,
            }],
            ids,
            values,
        ))
    while len(slots) < 7:
        slots.append(([0, None], [], []))
    return slots


class Handler(BaseHTTPRequestHandler):
    scenario_name = "midsummer"
    live = False
    started = time.time()
    # Command overrides persisted for the lifetime of the mock, per scenario.
    global_mode_overrides = {}
    charge_mode_overrides = {}
    sgr_override_until = 0

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
        return self._apply_modes(name, data)

    def _apply_modes(self, name, data):
        """Overlay the command state on the scenario's charge modes."""
        if name in self.global_mode_overrides:
            data["charge_mode"] = self.global_mode_overrides[name]

        count = len(chargers_for(data))
        modes = self.charge_mode_overrides.get(name)
        if modes is None:
            if name in self.global_mode_overrides:
                # The global command applies to every box.
                modes = [data.get("charge_mode", 2)] * count
            else:
                modes = data.get("charge_modes")
        if modes is None:
            modes = [data.get("charge_mode", 2)] * count
        modes = list(modes)
        default = data.get("charge_mode", 2)
        modes = (modes + [default] * count)[:count]
        data["charge_modes"] = modes
        return data

    def _read_body(self):
        length = int(self.headers.get("Content-Length", "0"))
        return self.rfile.read(length) if length > 0 else b""

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

        if path == "/rtc/time":
            now = datetime.datetime.now()
            self._send({
                "year": now.year,
                "month": now.month,
                "day": now.day,
                "hour": now.hour,
                "minute": now.minute,
                "second": now.second,
                "weekday": now.weekday(),
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

        if path == "/charge_manager/state":
            chargers = []
            for index, spec in enumerate(chargers_for(d)):
                current = float(spec.get("current") or 0.0)
                chargers.append({
                    "s": spec.get("state", 0),
                    "e": 0,
                    "ac": current,
                    "ap": spec.get("phases", 0),
                    "sc": current,
                    "sp": spec.get("supported", 1),
                    "lu": 1234 + index,
                    "n": spec.get("name", "WARP Wallbox"),
                    "uid": 12345 + index,
                })
            self._send({"state": 1, "chargers": chargers})
            return

        if path == "/charge_manager/charge_modes":
            self._send(d.get("charge_modes", []))
            return

        if path == "/charge_manager/supported_charge_modes":
            self._send(d.get("supported_charge_modes", []))
            return

        if path == "/power_manager/charge_mode":
            self._send({"mode": d.get("charge_mode", 2)})
            return

        if path == "/heating/state":
            self._send({
                "automation_override": bool(d.get("automation_override", False)),
                "sgr_blocking": bool(d.get("sgr_blocking", False)),
                "sgr_extended": bool(d.get("sgr_extended", False)),
                "p14enwg": bool(d.get("p14enwg", False)),
                "next_update": 0,
                "enabled": bool(d.get("heating_enabled", False)),
            })
            return

        if path == "/heating/sgr_blocking_override":
            self._send({"override_until": self.sgr_override_until})
            return

        if path == "/temperatures/state":
            self._send({
                "last_sync": 0,
                "last_check": 0,
                "next_check": 0,
                "current": d.get("temp_current", 32767),
                "today_min": d.get("temp_today_min", 32767),
                "today_max": d.get("temp_today_max", 32767),
                "today_avg": d.get("temp_today_avg", 32767),
                "tomorrow_min": d.get("temp_tomorrow_min", 32767),
                "tomorrow_max": d.get("temp_tomorrow_max", 32767),
                "tomorrow_avg": d.get("temp_tomorrow_avg", 32767),
            })
            return

        if path == "/solar_forecast/state":
            self._send({
                "wh_today": d.get("forecast_today_wh", 24100.0),
                "wh_today_remaining": d.get(
                    "forecast_today_remaining_wh", 12000.0
                ),
            })
            return

        if path == "/power_manager/state":
            self._send({"config_error_flags": 0, "external_control": 0})
            return

        if path == "/day_ahead_prices/config":
            self._send({"enable": True, "source": 0, "resolution": 0,
                        "vat": 1900, "grid_costs_and_taxes": 15000,
                        "supplier_markup": 2000})
            return

        if path == "/day_ahead_prices/prices":
            # Synthetic day-ahead curve in 15-min steps for two days:
            # cheap at night, peak in the evening (ct/kWh/1000).
            now = datetime.datetime.now()
            midnight = now.replace(hour=0, minute=0, second=0, microsecond=0)
            first_min = int(midnight.timestamp()) // 60
            prices = []
            for i in range(192):
                t = midnight + datetime.timedelta(minutes=15 * i)
                hour = t.hour + t.minute / 60.0
                base = 22000 + int(14000 * ((hour - 14.0) / 10.0) ** 2)
                prices.append(max(8000, base))
            self._send({"first_date": first_min, "resolution": 0,
                        "prices": prices})
            return

        if path == "/energy_manager/state":
            self._send({
                "error_flags": 0,
                "config_error_flags": 0,
                "em_version": 2,
                "inputs": [True, False, True, False],
                "sg_ready_outputs": [True, False],
                "relays": [False, True],
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

    def _command(self, path):
        """Handle a WARP command. Commands answer with an empty 200 body."""
        q = self._query()
        name = q.get("scenario", self.scenario_name)
        if name not in SCENARIOS:
            name = "midsummer"
        raw = self._read_body()

        if path == "/power_manager/charge_mode_update":
            try:
                request = json.loads(raw or b"{}")
                mode = int(request.get("mode", -1))
            except (json.JSONDecodeError, ValueError, TypeError):
                self.send_error(400)
                return
            if mode < 0:
                self.send_error(400)
                return
            # The global command applies to every box and clears any
            # per-charger overrides.
            self.global_mode_overrides[name] = mode
            self.charge_mode_overrides.pop(name, None)
            self._send("")
            return

        if path == "/charge_manager/charge_modes_update":
            try:
                modes = [int(v) for v in json.loads(raw or b"[]")]
            except (json.JSONDecodeError, ValueError, TypeError):
                self.send_error(400)
                return
            self.charge_mode_overrides[name] = modes
            self._send("")
            return

        if path.startswith("/charge_manager/charge_modes/"):
            index_text = path.rsplit("/", 1)[1]
            try:
                index = int(index_text)
                mode = int(raw.decode().strip())
            except (ValueError, UnicodeDecodeError):
                self.send_error(400)
                return
            data = self._data()
            if index < 0 or index >= len(chargers_for(data)):
                self.send_error(404)
                return
            modes = list(data.get("charge_modes", []))
            while len(modes) <= index:
                modes.append(data.get("charge_mode", 2))
            modes[index] = mode
            self.charge_mode_overrides[name] = modes
            self._send("")
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
                pv_energy = (d["pv_energy_a_wh"] + d["pv_energy_b_wh"]) / 1000.0
                today = now.day - 1
                base = today * 17
                # Per day: import counters for meter slots 0..6, export
                # counters for slots 0..6, then three price values. Slots:
                # 0 load, 1 PV, 2 grid, 3 battery.
                data[base + 0] = round(d["household_energy_wh"] / 1000.0, 3)
                data[base + 1] = 0.0
                data[base + 2] = round(d["grid_import_wh"] / 1000.0, 3)
                data[base + 3] = round(d["battery_charge_wh"] / 1000.0, 3)
                data[base + 7] = 0.0
                data[base + 8] = round(pv_energy, 3)
                data[base + 9] = round(d["grid_export_wh"] / 1000.0, 3)
                data[base + 10] = round(d["battery_discharge_wh"] / 1000.0, 3)
                # Wallbox meters use the slots after the four base meters.
                for offset, spec in enumerate(metered_chargers(d)):
                    energy = spec.get("meter_energy_wh")
                    if energy is None or 4 + offset >= 7:
                        continue
                    data[base + 4 + offset] = wh_to_kwh(energy)

                if today > 0:
                    # Yesterday's cumulative counters, so the panel can
                    # difference them into today's energy.
                    yesterday = (today - 1) * 17
                    data[yesterday + 0] = round(
                        (d["household_energy_wh"] - d["day_load_wh"]) / 1000.0, 3)
                    data[yesterday + 1] = 0.0
                    data[yesterday + 2] = round(
                        (d["grid_import_wh"] - d["day_grid_draw_wh"]) / 1000.0, 3)
                    data[yesterday + 3] = round(
                        (d["battery_charge_wh"] - d["day_battery_charge_wh"])
                        / 1000.0, 3)
                    data[yesterday + 7] = 0.0
                    data[yesterday + 8] = round(
                        pv_energy - d["day_generation_wh"] / 1000.0, 3)
                    data[yesterday + 9] = round(
                        (d["grid_export_wh"] - d["day_feed_in_wh"]) / 1000.0, 3)
                    data[yesterday + 10] = round(
                        (d["battery_discharge_wh"] - d["day_battery_discharge_wh"])
                        / 1000.0, 3)
                    for offset, spec in enumerate(metered_chargers(d)):
                        energy = spec.get("meter_energy_wh")
                        if energy is None or 4 + offset >= 7:
                            continue
                        day = float(spec.get("meter_day_wh") or 0.0)
                        data[yesterday + 4 + offset] = wh_to_kwh(
                            max(0.0, energy - day))
            self._send(data)
            return
        if (path.startswith("/power_manager/charge_mode_update") or
                path.startswith("/charge_manager/charge_modes")):
            self._command(path)
            return
        if path.startswith("/heating/sgr_blocking_override_update"):
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length) if length > 0 else b"{}"
            try:
                request = json.loads(raw)
                Handler.sgr_override_until = int(request.get("override_until", 0))
            except (json.JSONDecodeError, ValueError, TypeError):
                self.send_error(400)
                return
            self._send("")
            return
        self.send_error(404)

    def do_PUT(self):
        path = self.path.split("?", 1)[0]
        if (path.startswith("/power_manager/charge_mode_update") or
                path.startswith("/charge_manager/charge_modes")):
            self._command(path)
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
