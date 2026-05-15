---
id: REQ-RADIO-003
title: Polling rate selectable up to 8000 Hz with high-speed dongle
status: approved
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

## Description
With the CH32V305 high-speed dongle, the mouse SHALL support polling rates of 1000, 2000, 4000, and 8000 Hz. With the low-speed nRF52820 dongle, only 1000 Hz is required.

## Acceptance criteria
- USB analyzer confirms requested polling interval matches actual interval within +/-5 %.
