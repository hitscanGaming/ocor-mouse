---
id: REQ-USB-001
title: USB HID descriptor passes USB-IF compliance
status: implemented
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

## Description
The HID descriptor presented over USB (both via wired mouse and via dongle) SHALL pass the USB-IF HID Descriptor Tool's conformance check.

## Acceptance criteria
- Zero errors from the HID Descriptor Tool.
- Mouse enumerates as a HID device on Windows, macOS, and Linux without custom drivers.
