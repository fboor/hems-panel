# WARP module API and data mapping

A real WARP Energy Manager V2 does not serve one flat `/json` document; `/json`
returns 404. It exposes one object per module. The panel discovers the modules,
maps their meter slots to the values the UI needs and keeps the flat JSON parser
as a fallback for a bridge.

The adapter lives in `src/warp/WarpApi.h`; the transport is an interface, so the
same code runs over `HTTPClient` in the firmware and over POSIX sockets in the
emulator. The flat fallback is `src/warp/WarpParse.h`.

## Endpoints used

| Endpoint | Purpose |
|---|---|
| `/info/name`, `/info/version` | device identity and firmware version |
| `/meters/N/config` | meter slot configuration: location, virtual meter, name |
| `/meters/N/value_ids` | the value IDs that this slot provides |
| `/meters/N/values` | the current values, in the order of `value_ids` |
| `/power_manager/low_level_state` | fallback for grid/battery power and SOC |
| `/power_manager/charge_mode` | global charge mode |
| `/charge_manager/state` | chargers: state, current, phases, name |
| `/charge_manager/charge_modes` | per-charger modes |
| `/charge_manager/supported_charge_modes` | modes the device offers |
| `/solar_forecast/state` | today's and remaining PV forecast |
| `/rtc/time` | device time for the daily history and the price window |
| `/energy_manager/history_energy_manager_daily` | daily meter history |
| `/heating/state` | SG-Ready state and enable flag |
| `/heating/sgr_blocking_override` | current party-mode override |
| `/temperatures/state` | outdoor temperatures |
| `/energy_manager/state` | digital I/O states |
| `/day_ahead_prices/config` | tariff enable flag and adders |
| `/day_ahead_prices/prices` | spot price series |

The panel polls the meter values and the charge manager every 10 seconds. Slow
state — heating, temperatures, I/O and prices — is fetched every sixth poll
(about one minute); prices themselves are cached for about 15 minutes.

## Commands

| Endpoint | Body |
|---|---|
| `/power_manager/charge_mode_update` | `{"mode": <int>}` |
| `/charge_manager/charge_modes/<index>` | bare integer |
| `/heating/sgr_blocking_override_update` | `{"override_until": <unix minutes>}` |

Commands answer with an empty `200`, like the WARP firmware.

## Meter mapping

The adapter reads the slot configuration and value IDs, then selects the values
the UI needs:

| Role | WARP location | Values |
|---|---|---|
| grid | 4 | power L1/L2/L3 `39/48/57`, total `74`, import `209`, export `211`, voltage `1/2/3`, frequency `361` |
| battery | 5 | voltage `10`, current `396`, power `404`, charge `413`, discharge `415`, SOC `387` |
| household | 6 | power L1/L2/L3 `39/48/57`, total `74`, energy `215` |
| PV | 7 | string power `442/443`, total `452`, string energy `454/456`, total `472` |
| wallbox | 2 or 8 | power L1/L2/L3 `39/48/57`, total `74`, energy import `209` (fallback `215`) |
| external / S0 | other | total `74` |

The energy value IDs (`209/211/215/413/415/454/456/472`) are reported in **kWh**
by WARP. The panel converts them to Wh on ingest; see [energy.md](energy.md).
Several wallbox meters are summed into one energy value; today's part is the
day-over-day difference of their import counters in the daily history.

A meter is matched by its WARP location first. If the location is unknown, the
adapter tries `virtual_meter`, then keywords in `display_name` ("Netz", "Akku",
"PV", "Verbrauch", ...). A slot that still has no role is treated as an external
meter.

If no grid or battery meter is configured, the adapter falls back to
`/power_manager/low_level_state` for `power_at_meter`, `power_at_battery` and
`battery_soc`.

## Signs and island mode

- Grid positive = import, negative = export.
- Battery positive = charging, negative = discharging.
- Island mode is inferred when all grid phase voltages are near zero while PV or
  the battery is active. The grid card then shows `Inselbetrieb`.

## Flat JSON fallback

`src/warp/WarpParse.h` maps a flat JSON object to `WarpData`. Each field accepts
a few aliases; the first match wins.

| UI | JSON keys (first match) |
|---|---|
| grid L1/L2/L3 | `grid_power_l1..l3`, `grid_l1..l3` |
| grid exchange | `grid_exchange`, `grid_power`, `grid_power_total` |
| PV strings | `pv_power_a`, `pv_power_b` |
| PV total | `pv_power`, `pv_power_total` (else A+B) |
| external / S0 | `external_power`, `s0_power` |
| household L1/L2/L3 | `household_power_l1..l3`, `house_power_l1..l3` |
| household total | `household_power`, `house_power` (else sum) |
| battery | `battery_soc`, `battery_power`, `battery_voltage`, `battery_current` |
| island | `island_flag`, `island`, `island_mode` |
| energy totals | `pv_energy_wh`, `house_energy_wh`, `grid_import_wh`, `grid_export_wh`, `battery_charge_wh`, `battery_discharge_wh`, `charger_energy_wh` / `wallbox_energy_wh` |
| today (legacy) | `day_generation_wh`, `day_load_wh`, `day_feed_in_wh`, `day_grid_draw_wh`, `day_charger_wh` / `day_wallbox_wh` |
| autarky | `autarky`, `autarkie`, `self_sufficiency` (otherwise calculated from the energy totals) |
| forecast | `wh_today`, `forecast_today_wh`, `solar_forecast_today_wh`; remaining from `wh_today_remaining`, `forecast_today_remaining_wh` |
| identity | `device_name`, `firmware_version` |

The flat parser also calculates grid/PV/household totals from phase values when
the total is missing. Today's autarky comes from the WARP daily-history
endpoint; the flat document can still supply the legacy `day_*` counters.

## Query strings

A query string on the configured URL, such as `?scenario=island`, is kept and
appended to every module request. The emulator uses this to select a mock
scenario without restarting the mock; see [emulator.md](emulator.md#scenarios).
A trailing `/json` in the configured URL is removed before the module paths are
built.
