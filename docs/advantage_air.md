# Zoned AC airflow policy (AdvantageAir)

Ducted AC with zones needs enough open zones while the compressor runs. Controllers such as
AdvantageAir protect this with installer-configured constant zones, which open automatically
when too few zones are open. The optional `advantage_air` section of `config/home.yaml` adds a
conservative application guard on top, so a voice command never relies on that last-second
automatic reopen. Without the section, climate entities are controlled like any other device.

## Configuring a system

Describe each zoned system with read-only facts from your controller's diagnostics:

```yaml
advantage_air:
  systems:
  - entity: climate.ac              # main unit
    myzone_entity: select.ac_myzone # MyZone selector; voice may not change it
    zones:                          # every zone's climate entity
    - climate.ac_zone_1
    - climate.ac_zone_2
    constant_zones:                 # installer-configured constants, from diagnostics
    - climate.ac_zone_1
    minimum_open_zones: 1
    verified_preset: MyTemp         # operating preset the policy was verified under
```

There is no universal minimum number of open zones: it depends on system sizing and house
design. Use your installation's constant-zone count, not a guess. The
[AdvantageAir user manual, section 6](https://www.advantageair.com.au/wp-content/uploads/2021/03/MyAir-and-MyAir-User-Manual-v4.1.pdf)
describes constant zones, and the
[Home Assistant integration documentation](https://www.home-assistant.io/integrations/advantage_air/)
describes the main-system controls, temperature-controlled zones, manual vent covers and
MyZone/MyTemp/MyAuto.

## Enforcement

All commands use ordinary Home Assistant services, which go through the controller. There
are no raw controller writes, installer-setting changes, preset changes, or attempts to
disable pressure-relief or constant logic.

Before posting, fresh state is checked:

- A requested temperature, fan mode or HVAC mode must match the target's current
  capabilities, range and step. A zone's `heat_cool` means enabled and is not a request for
  the main unit to heat or cool automatically, so heating and cooling go to the main unit.
- Starting the main unit or changing its mode, and closing a zone while it runs, must leave
  `minimum_open_zones` and every constant zone open.
- Voice cannot close a constant zone while the main unit runs.
- Closing zones while the main unit is off is allowed; it then refuses to start until the
  zone conditions are satisfied again.
- A changed preset, or a missing or unavailable zone, blocks airflow-changing commands. The
  main unit can still be turned off when available.
- MyZone changes are blocked, so voice never changes which zone has constant responsibility.

The [Home Assistant zone implementation](https://github.com/home-assistant/core/blob/dev/homeassistant/components/advantage_air/climate.py)
forwards open and close commands to the controller without calculating a safe number of
zones, so acceptance by Home Assistant is not proof that these conditions hold.

These checks add a guard; they do not replace the controller's protection or validate duct
design or actual airflow. Another client can change state after a check, and Home Assistant
polls the controller. If the installer changes constants, zone layout, grouping or operating
preset, verify the configuration again and update the policy. The software does not invent a
new minimum or force zones open to get around a rejected command.