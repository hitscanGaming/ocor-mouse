---
id: REQ-DFU-002
title: Failed DFU recovers to previous image
status: approved
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

## Description
An interrupted DFU (power cut, USB unplug, image-integrity failure) SHALL leave the device in a bootable state on the previous image.

## Acceptance criteria
- Power cut at any point during DFU still results in a bootable device.
- MCUboot reverts to the previous image if the new image fails its integrity check (for NCS targets; the CH32 IAP equivalent applies to the high-speed dongle).
