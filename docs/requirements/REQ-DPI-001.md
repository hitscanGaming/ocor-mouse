---
id: REQ-DPI-001
title: DPI adjustable 100-32000 in 50-step increments
status: implemented
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

## Description
The mouse SHALL support DPI values from 100 to 32000, stepped by 50 (i.e. valid values are 100, 150, 200, ..., 32000).

## Acceptance criteria
- Driver-set DPI is applied within 5 ms of receipt over the configured transport (USB or ESB).
- DPI persists across power cycles via the settings subsystem.
- Invalid DPI values (out of range or not divisible by 50) are rejected.

## Notes
Sensor: PAW3395. Driver: `nrf/drivers/sensor/paw3395`.
