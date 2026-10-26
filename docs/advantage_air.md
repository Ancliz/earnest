# AdvantageAir: verified configuration and command constraints

Read-only inspection on 2026-10-26 found Home Assistant 2026.10.4 with these controls:

| Entity | Role | Example phrase |
| --- | --- | --- |
| `climate.ac` | Main unit; MyTemp preset, off when inspected | “Turn the air conditioner on”; “Cool the AC”; “Set the fan speed on the AC to low” |
| `climate.ac_master_bed` | Temperature-controlled zone | “Set master bedroom temperature to twenty two”; “Turn on master bedroom” |
| `climate.ac_alasdair_bed` | Temperature-controlled zone | “Set Alasdair bed temperature to twenty one” |
| `climate.ac_craft_ldy` | Temperature-controlled zone | “Turn on craft room”; “Set laundry temperature to twenty four” |
| `climate.ac_tv_rump` | Temperature-controlled zone; installer constant | “Turn on TV and rump”; “Set TV room temperature to twenty two” |
| `climate.ac_diningliving` | Temperature-controlled zone | “Turn on dining and living”; “Set living room temperature to twenty two” |
| `switch.ac_myfan` | Automatic fan control | “Turn on automatic AC fan” |
| `select.ac_myzone` | MyZone selection | Discovered, but voice changes are deliberately not configured |

No media-player, light, fan, or cover entity was returned by that state query.
Their general phrase rules remain available if those entities are discovered later.
Examples above were curated during development, not generated or inferred by the
running program. Physical AC operation was not tested or changed.

## What was verified

The controller's read-only HA diagnostics report five zones, `noOfConstants: 1`,
`constant1: 4`, and no second/third constant. Zone 4 is TV & Rump. MyTemp is enabled;
MyAuto is not running. The saved policy identifies those exact entities and the
verified preset. No diagnostics dump, credentials, device identifiers, or installer
access codes are stored in this document.

The [manufacturer's user manual, section 6](https://www.advantageair.com.au/wp-content/uploads/2021/03/MyAir-and-MyAir-User-Manual-v4.1.pdf)
explains that installer-configured constant zones may open automatically when too
few zones are open. Their configuration depends on system sizing and house design;
there is no universal minimum of two or three rooms. This installation's one
constant is the basis for the application's conservative `minimum_open_zones: 1`
check; it is not a general HVAC sizing rule or a measurement of sufficient airflow.

The [HA integration documentation](https://www.home-assistant.io/integrations/advantage_air/)
distinguishes main-system controls, temperature-controlled climate zones, manual
vent covers, and MyZone/MyTemp/MyAuto. Your main entity currently has no single
target-temperature capability in MyTemp; its zones do. Each reports 16–32°C in
whole-degree steps. Zone `heat_cool` means enabled and is not a request for the main
compressor to use automatic heating/cooling.

## Enforcement

All commands use ordinary Home Assistant services, which in turn go through the
AdvantageAir controller. There are no raw controller writes, installer-setting
changes, preset changes, or attempts to disable pressure-relief/constant logic.

Before posting, fresh state is checked:

- A requested temperature, fan mode, or HVAC mode must match the target's current
  capabilities, range, and step. “Cool master bedroom” is rejected because a zone
  does not offer the main unit's cooling mode. Use the main unit for heating/cooling.
- Main-unit start/mode changes and zone closures while running must leave the
  configured minimum zones enabled and all listed constant zones enabled.
- Voice cannot close TV & Rump while the main unit is running. This is stricter
  than the controller's automatic constant-zone logic and intentionally avoids
  relying on a last-second automatic reopen.
- Closing zones while the main unit is off is allowed; the main unit will then
  refuse to start until its required zone/constant conditions are satisfied.
- A changed preset, missing/unavailable zone, or missing installation policy blocks
  airflow-changing commands. The main unit can still be turned off when available.
- MyZone selector changes are blocked to avoid changing which zone has constant
  responsibility. Preset changes and installation/configuration services are not
  in the supported voice service set.

The [HA zone implementation](https://github.com/home-assistant/core/blob/dev/homeassistant/components/advantage_air/climate.py)
forwards ordinary open/close commands to the controller; it does not independently
calculate an installation-specific safe number of zones. We therefore do not treat
acceptance by HA as proof that our own airflow conditions have been satisfied.

The enabled-zone checks add a conservative application guard; they do not replace
the controller's protection or validate duct design/actual airflow. Another HA
client can change state after a check, and HA itself polls the controller. Keep the
controller's installer configuration intact. If the installer changes constants,
zone layout, grouping, or operating preset, reverify the configuration and update
the policy accordingly. The software does not invent a new minimum or force zones
open to get around a rejected command.