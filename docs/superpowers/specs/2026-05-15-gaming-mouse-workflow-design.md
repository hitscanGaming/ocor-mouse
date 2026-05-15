# Gaming Mouse Development Workflow — Design Spec

**Date:** 2026-05-15
**Status:** Draft, pending user review
**Scope:** End-to-end automation from markdown requirements through signed software release for a Nordic-based gaming mouse product line (current: nRF52; forward: nRF54).

---

## 1. Context

A 2–5 person firmware team is building a gaming mouse product called **hitscan**. Current product comprises three firmware targets shipping together:

- **Mouse** — nRF52840, 2.4 GHz proprietary radio (ESB) + wired USB fallback.
- **Low-speed dongle** — nRF52820, USB HID bridge for the mouse's 2.4 GHz link.
- **High-speed dongle** — CH32V305 (WCH RISC-V), USB HID with higher polling rate.

Plus a browser-based configurator (WebHID) for end users.

Existing code today lives inside the NCS install tree (`C:\ncs\v3.0.2\nrf\applications\gaming_mouse\`) and uses Nordic's CAF (Common Application Framework) plus the PAW3395 sensor driver. The CH32 dongle and web configurator currently sit outside any unified workflow.

**Goal:** stand up an automated workflow that takes markdown requirements as input and produces a versioned, traceability-reported release bundle.

## 2. Goals & non-goals

**Goals**
- Markdown requirements with stable IDs (REQ-AREA-NNN) are the single source of truth.
- Every PR is built, linted, and traceability-checked automatically.
- Tagged releases produce a complete artifact bundle (firmware × 3, web-config bundle, traceability matrix, changelog, manifest).
- Hardware QA is manual but driven by an auto-generated, requirement-derived checklist.
- nRF54 migration is additive (new board target), not a rewrite.

**Non-goals (this iteration)**
- Hardware-in-loop CI on physical boards (HW testing stays manual per checklist).
- Multiple product variants/SKUs (single product first; variant infrastructure deferred).
- Production-grade signing via HSM/KMS (dev-key signing only; KMS migration documented but deferred).
- Native desktop config tool (web configurator only; native deferred).
- Cross-release backport branches (no long-lived release branches until needed).

## 3. Repository layout

Single GitHub monorepo `ocor-mouse`, organized as:

```
ocor-mouse/
├── west.yml                                     # pins ncs=v3.0.2
├── .github/workflows/
│   ├── pr.yml                                   # PR validation
│   ├── nightly.yml                              # scheduled full builds
│   ├── release.yml                              # tag-triggered release
│   └── promote-release.yml                      # QA-issue-driven publish
├── .github/actions/
│   ├── setup-ncs/                               # composite: west init/update + cache
│   └── setup-ch32/                              # composite: WCH risc-v-gcc container
├── applications/
│   └── gaming_mouse/                            # out-of-tree NCS application
│       ├── CMakeLists.txt, Kconfig
│       ├── src/                                 # migrated from C:\ncs\v3.0.2\...
│       └── configuration/
│           ├── hitscan_nrf52840/
│           │   ├── prj_esb.conf                 # mouse debug
│           │   ├── prj_esb_release.conf         # mouse release
│           │   └── app.overlay
│           └── hitscan52820_nrf52820/
│               ├── prj.conf                     # dongle-ls debug
│               ├── prj_release.conf             # dongle-ls release
│               └── app.overlay
├── dongle-hs-ch32v305/                          # WCH RISC-V firmware (separate toolchain)
│   ├── Makefile                                 # or platformio.ini (decided in impl)
│   ├── src/, inc/, lib/                         # migrated from C:\Users\Jiawe\dongle\CH32V305GBU6
│   └── linker/, openocd.cfg
├── web-config/                                  # migrated from C:\Users\Jiawe\Desktop\hitscan\webDriver\
│   ├── index.html, src/                         # WebHID configurator
│   ├── package.json
│   └── dist/                                    # build output (gitignored)
├── host-tool/                                   # RESERVED — not built in v0.x (see §2 non-goals)
│   └── README.md
├── docs/
│   ├── requirements/                            # REQ-*.md files (source of truth)
│   ├── architecture/                            # design docs incl. this spec
│   └── QA/
│       └── checklist-template.md
├── tools/
│   └── reqtool/                                 # Python CLI for requirements + traceability
│       ├── pyproject.toml
│       └── reqtool/
└── tests/
    └── native_sim/                              # Zephyr ztests for shared protocol code
```

**Branching:** trunk-based on `main`. Short-lived feature branches → PR → squash-merge. Release tags `v<MAJOR>.<MINOR>.<PATCH>` on `main` only. Conventional Commits enforced via PR title check (used by `git-cliff` for changelog).

**Versioning:** one repo-wide version applies to all artifacts in a release bundle. Avoids cross-component compatibility ambiguity.

## 4. Requirements format

One markdown file per requirement in `docs/requirements/`, named `REQ-<AREA>-<NNN>.md`. Areas (initial set, extensible): `SENSOR`, `DPI`, `BUTTON`, `RGB`, `RADIO`, `USB`, `POWER`, `DFU`.

YAML frontmatter (required fields):

```yaml
---
id: REQ-DPI-001
title: DPI adjustable 100–32000 in 50-step increments
status: approved            # draft | approved | implemented | verified | deprecated
verification: HIL           # code-review | unit-test | native-sim | HIL
priority: P0                # P0 | P1 | P2
owner: fw-team
products: [hitscan]
---
```

Body: free-form markdown. Conventional sections: `## Description`, `## Acceptance criteria`, `## Notes`.

Code/test references use the bare REQ-ID string, e.g.:

```c
/* REQ-DPI-001: enforce step size */
if (dpi % 50 != 0) return -EINVAL;
```

```c
ZTEST(dpi, test_REQ_DPI_001_rejects_unaligned_dpi) { ... }
```

## 5. `reqtool` — requirements & traceability CLI

Python 3 CLI at `tools/reqtool/`. Distributed via `pip install -e tools/reqtool` for local use; CI runs it from source. Advisory by default (only fails CI on syntax/schema errors, never on coverage gaps).

| Subcommand | Behavior | Used in |
|---|---|---|
| `reqtool lint` | Validates frontmatter schema, unique IDs, allowed enum values. Fails on schema errors. | `pr.yml`, `nightly.yml` |
| `reqtool trace` | Greps tree for `REQ-*-*` strings; emits `traceability-matrix.md` (REQ → files → tests → status). | `release.yml` |
| `reqtool trace --diff` | Compares trace results between base and head; posts PR comment summarizing added/removed/changed REQ references. | `pr.yml` |
| `reqtool orphans` | Reports `status: implemented` reqs with zero code refs, and dangling code refs to non-existent reqs. Advisory only. | `nightly.yml` |
| `reqtool checklist --tag <v>` | Emits markdown checklist of all `verification: HIL` reqs scoped to products shipped in this release. | `release.yml` |
| `reqtool report --tag <v>` | Full release traceability report; included in release bundle. | `release.yml` |

**Strictness policy:** advisory only for coverage gaps; strict (fail-build) for schema errors and dangling REQ-ID references.

## 6. CI/CD pipelines

### 6.1 `pr.yml` (target: <10 min)

Triggered on every PR to `main`.

1. `reqtool lint` (strict) and `reqtool trace --diff` (advisory; posts PR comment).
2. `clang-format --dry-run` on changed C files.
3. Build matrix (parallel jobs, all required to pass):
   - `mouse-debug`: `west build applications/gaming_mouse -b hitscan_nrf52840 -- -DCONF_FILE=prj_esb.conf`
   - `dongle-ls-debug`: `west build applications/gaming_mouse -b hitscan52820_nrf52820 -- -DCONF_FILE=prj.conf`
   - `dongle-hs-debug`: WCH RISC-V build for CH32V305 in a pinned container image
   - `web-config-build`: `npm ci && npm run build` in `web-config/`
4. `native_sim` ztests for shared protocol code under `tests/native_sim/`.
5. Upload artifacts with 7-day retention (engineers can download to flash locally for manual smoke testing).

Caching: `~/.cache/west`, `~/.cache/zephyr`, NCS module checkouts, npm cache, ccache.

### 6.2 `nightly.yml` (target: <30 min)

Triggered on schedule at 02:00 UTC and on workflow-dispatch.

- Same as PR pipeline, **plus** release-config builds (`prj_esb_release.conf`, `prj_release.conf`) to catch release-only regressions early.
- `reqtool orphans` advisory report posted as a GitHub issue if non-empty (auto-closed when empty again).
- 30-day artifact retention.

### 6.3 `release.yml` (triggered on `git tag v*`)

1. Verify tag is on `main` and matches `v<MAJOR>.<MINOR>.<PATCH>`. (GPG-signed tags optional; revisit when adopting KMS for image signing.)
2. Run all release-config builds:
   - `mouse-fw.hex` from `prj_esb_release.conf`
   - `mouse-fw-update.bin` (MCUboot-signed-with-dev-key image)
   - `dongle-ls-fw.hex` from `prj_release.conf`
   - `dongle-ls-fw-update.bin` (MCUboot-signed-with-dev-key image)
   - `dongle-hs-fw.bin` from CH32 release build
3. Build web-config production bundle; deploy `dist/` to GitHub Pages.
4. `reqtool report --tag <v>` → `traceability-matrix.md`.
5. `git-cliff` → `CHANGELOG.md` from Conventional Commits since previous tag.
6. Generate `manifest.json`:

   ```json
   {
     "release": "v1.0.0",
     "git_sha": "abc123",
     "build_time": "2026-05-15T12:00:00Z",
     "artifacts": {
       "mouse-fw.hex":         {"sha256": "...", "size": 0},
       "mouse-fw-update.bin":  {"sha256": "...", "size": 0, "signed_with": "dev-key"},
       "dongle-ls-fw.hex":     {"sha256": "...", "size": 0},
       "dongle-ls-fw-update.bin": {"sha256": "...", "size": 0, "signed_with": "dev-key"},
       "dongle-hs-fw.bin":     {"sha256": "...", "size": 0}
     },
     "compatibility": {"protocol_version": 3}
   }
   ```

7. Bundle artifacts + `manifest.json` + `CHANGELOG.md` + `traceability-matrix.md` into `ocor-mouse-v<MAJOR>.<MINOR>.<PATCH>.zip`. `protocol_version` is read from a header in `shared/` protocol code (e.g., `shared/include/protocol_version.h`); bumping it is a deliberate dev action gated by code review.
8. Create GitHub Release in **draft** state with all artifacts attached.
9. Open a GitHub issue from the `reqtool checklist --tag <v>` template (the QA gate — see §7).

### 6.4 `promote-release.yml` (triggered on issue close)

Listens for `issues.closed` events. If the closed issue title matches `^Release QA: v\d+\.\d+\.\d+$` and **all** checkboxes in its body are ticked, flips the matching GitHub Release from draft → published. Otherwise, posts a comment listing the unticked items and reopens the issue.

## 7. Hardware QA workflow

Manual, but driven by an auto-generated checklist. Per release:

```markdown
# Release QA: v1.0.0

Auto-generated from `reqtool checklist --tag v1.0.0`. Tick boxes after verification on physical hardware. Close this issue only after FW lead + QA lead sign-off; that triggers `promote-release.yml`.

## HIL verification
- [ ] REQ-DPI-001: DPI step size 50, range 100–32000
- [ ] REQ-RADIO-001: 2.4 GHz pairing & re-pairing
- [ ] REQ-USB-001: HID descriptor passes USB-IF conformance
- [ ] REQ-POWER-001: Battery life > 70 h at 1000 Hz
- [ ] REQ-DFU-001: DFU completes within 30 s over USB
- [ ] REQ-DFU-002: Failed DFU recovers to previous image

## Sign-off
- [ ] QA lead approval (@username)
- [ ] FW lead approval (@username)
```

If any HIL test fails, QA opens a bug issue referencing the failing REQ-ID; that bug becomes a blocker tracked against the release.

## 8. DFU strategy

| Target | Bootloader | Update channel | Release artifact |
|---|---|---|---|
| Mouse (nRF52840) | MCUboot, dev key | USB SMP when wired; ESB-tunneled when wireless | `mouse-fw-update.bin` |
| Dongle-LS (nRF52820) | MCUboot, dev key | USB SMP | `dongle-ls-fw-update.bin` |
| Dongle-HS (CH32V305) | WCH built-in USB IAP | USB IAP entered via button-hold or vendor USB cmd | `dongle-hs-fw.bin` |

The web configurator detects connected device VID/PID and offers the correct update artifact. Update flow per device is owned by `web-config/src/dfu/`.

**Future:** swap dev key for KMS-managed production key. No build flow change required — only the key source moves from in-repo to KMS-OIDC.

## 9. nRF54 forward-compatibility

The migration path when nRF54 silicon arrives:

| Today | At nRF54 |
|---|---|
| `west.yml`: ncs=v3.0.2 | Bump to NCS version supporting nRF54L/H |
| Boards: `hitscan_nrf52840`, `hitscan52820_nrf52820` | Add `hitscan_nrf54l<x>` board overlay under `applications/gaming_mouse/boards/` |
| CI matrix: nRF52 targets | Add nRF54 row to PR + nightly matrices |
| ESB API | Unchanged (NCS supports ESB on nRF54L/H) |
| MCUboot | Unchanged |
| Sensor driver (paw3395) | Unchanged (sensor is external) |

To stay ready, application code must remain SoC-agnostic:

- No hardcoded peripheral addresses; use devicetree aliases.
- No direct `NRF_*` register writes; use Zephyr/NRFX APIs.
- Build system separates "application" from "board target" (already the case with NCS).

## 10. Migration plan (first implementation phase)

The implementation is multi-phase. `writing-plans` will decompose this into ordered, reviewable steps with explicit verification at each phase.

1. **Bootstrap repo** — create `ocor-mouse` GitHub repo; add `west.yml` pinning `ncs/v3.0.2`; verify `west update` produces a working tree.
2. **Move mouse + dongle-ls app out-of-tree** — copy `C:\ncs\v3.0.2\nrf\applications\gaming_mouse\hitscan_nrf52840\` and `hitscan52820_nrf52820\` plus shared `src/` into `applications/gaming_mouse/` in the repo. Verify both apps build via `west build` from the repo with NCS pulled by west.
3. **Move CH32 dongle code** — copy `C:\Users\Jiawe\dongle\CH32V305GBU6` into `dongle-hs-ch32v305/`. Add Makefile or platformio.ini. Containerize the WCH toolchain (Dockerfile in `.github/actions/setup-ch32/`).
4. **Move web configurator** — copy `C:\Users\Jiawe\Desktop\hitscan\webDriver\` contents into `web-config/`. Add `package.json` and a build step.
5. **Bootstrap reqtool** — scaffold `tools/reqtool/` (pyproject, click-based CLI, frontmatter schema, lint + trace + checklist subcommands).
6. **Author initial REQ set** — seed `docs/requirements/` with the requirements implicit in the existing code (DPI range, polling rates, button count, radio modes, DFU behavior, battery life). Tag each existing source file with the appropriate REQ-ID comments.
7. **Bootstrap CI** — `pr.yml`, `nightly.yml`, composite setup actions. Verify a green build on a noop PR.
8. **Bootstrap release** — `release.yml`, `promote-release.yml`, MCUboot dev keys in `keys/`, manifest schema, git-cliff config. Cut a `v0.1.0` test release.
9. **Wire up QA** — checklist issue template, QA close → promote flow. Run a dry-run release on `v0.1.0`.

## 11. Open items / explicitly deferred

- **CH32 build tooling:** `Makefile` (WCH `riscv-none-elf-gcc` + OpenOCD) vs PlatformIO `platformio.ini` (community `ch32v` platform). Decision deferred to implementation; both produce the same artifact. Containerized either way.
- **Signing (KMS):** dev keys for now. Migrate before first customer ship.
- **HIL CI:** start with manual checklist; revisit when team grows or release cadence requires it.
- **Product variants/SKUs:** add a variant Kconfig layer when the second SKU is in scope.
- **Native desktop config tool:** `host-tool/` reserved; build out if WebHID coverage proves insufficient.
- **Cross-release backports:** add release branches only when an old release needs patching.

## 12. Approval

- Brainstorm phase: this document is the output of the design-approval step.
- Implementation: follow-on `writing-plans` skill produces the executable implementation plan from this spec.
