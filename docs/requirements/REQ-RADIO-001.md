---
id: REQ-RADIO-001
title: 2.4 GHz ESB pairing between mouse and dongle
status: implemented
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

## Description
The mouse SHALL pair with a paired dongle over Nordic's Enhanced ShockBurst (ESB) protocol.

## Acceptance criteria
- A fresh pair completes within 10 s of entering pairing mode.
- Once paired, the mouse reconnects automatically on power-up.
- Either the low-speed or high-speed dongle may be paired (one at a time).
