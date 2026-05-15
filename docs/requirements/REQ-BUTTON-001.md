---
id: REQ-BUTTON-001
title: Minimum six independently mappable buttons
status: implemented
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

## Description
The mouse SHALL provide at least six buttons (left, right, middle, two side, DPI cycle) with independent mapping via the web configurator.

## Acceptance criteria
- Each button generates a distinct HID event.
- Remapping persists across power cycles.
