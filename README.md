# ocor-mouse

Firmware, dongles, and configurator for the hitscan gaming mouse line.

## Components

- `applications/gaming_mouse/` — Zephyr-based NCS application (mouse + low-speed dongle).
- `dongle-hs-ch32v305/` — High-speed USB dongle firmware (WCH RISC-V).
- `web-config/` — WebHID configurator.
- `shared/` — Protocol code shared between firmware targets.
- `tools/reqtool/` — Requirements & traceability CLI.
- `docs/requirements/` — Requirements (source of truth).
- `docs/superpowers/specs/` — Design specs.
- `docs/superpowers/plans/` — Implementation plans.

## Building locally

    make reqtool-install
    west init -l . && west update --narrow -o=--depth=1
    west build -p auto -b hitscan_nrf52840 applications/gaming_mouse \
        --sysbuild -- -DCONF_FILE=prj_esb.conf

For the CH32 dongle, `make` inside `dongle-hs-ch32v305/` (requires `riscv-none-elf-gcc` — see `.github/actions/setup-ch32/Dockerfile` for the container).

## Adding a requirement

1. Create `docs/requirements/REQ-AREA-NNN.md` (see `tools/reqtool/README.md`).
2. Reference the REQ-ID in code/test comments: `/* REQ-AREA-NNN: ... */`.
3. Run `reqtool lint && reqtool trace` to verify.

## Releasing

1. Push a tag `vX.Y.Z` on `main`.
2. `release.yml` builds all artifacts, opens a Release QA issue.
3. QA flashes hardware, ticks the checklist, closes the issue.
4. `promote-release.yml` flips the draft release to published.

See `docs/superpowers/specs/2026-05-15-gaming-mouse-workflow-design.md` for the full design.

## First-time repo setup

Once the repo is on GitHub, run this one-shot bootstrap:

```bash
gh label create release-qa --color FFD700 --description "Release QA tracking issue"
```

GitHub Pages must be enabled (Settings → Pages → Source = "GitHub Actions") for `release.yml`'s web-config deploy to publish.
