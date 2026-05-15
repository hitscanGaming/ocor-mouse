---
id: REQ-DPI-002
title: At least four user-selectable DPI presets
status: implemented
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

## Description
The mouse SHALL store at least four user-configurable DPI presets, cycled by a dedicated DPI button.

## Acceptance criteria
- Preset cycling button advances the active preset within one polling interval.
- Each preset is independently settable via the web configurator.
- Presets are persisted across reboot.
