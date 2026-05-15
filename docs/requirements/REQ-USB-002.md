---
id: REQ-USB-002
title: WebHID configurator vendor protocol
status: implemented
verification: code-review
priority: P0
owner: fw-team
products: [hitscan]
---

## Description
The mouse and dongles SHALL expose a vendor-defined HID report channel used by the web configurator for DPI, polling, RGB, and button-mapping configuration.

## Acceptance criteria
- Vendor report IDs are documented in `shared/protocol/`.
- `protocol_version` in `shared/include/protocol_version.h` increments on breaking changes.
