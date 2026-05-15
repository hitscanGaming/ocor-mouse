# host-tool — reserved

This directory is intentionally empty in v0.x. Per the workflow spec (§2 non-goals, §11 deferred), the native desktop configurator is not built initially — `web-config/` (WebHID) is the only end-user surface.

Build this out only if WebHID coverage proves insufficient. When built, mirror the structure of `web-config/`: own `package.json`, own CI job in `pr.yml`, separate installer artifact in `release.yml`.
