---
id: REQ-DFU-001
title: DFU completes within 30 s over USB
status: approved
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

## Description
Firmware update of either the mouse (when wired) or a dongle SHALL complete within 30 seconds when delivered over USB.

## Acceptance criteria
- Web configurator's DFU flow from "Update" click to "complete" message takes <= 30 s.
