# Gaming Mouse Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the automated dev-to-release workflow specified in `docs/superpowers/specs/2026-05-15-gaming-mouse-workflow-design.md` — markdown requirements through signed firmware/web release for the hitscan gaming mouse line (nRF52840 mouse, nRF52820 low-speed dongle, CH32V305 high-speed dongle, WebHID configurator).

**Architecture:** GitHub monorepo `ocor-mouse`. NCS application moved out-of-tree, pinned via `west.yml` to ncs/v3.0.2. CH32 dongle migrated from MounRiver Studio to a Makefile build in a containerized `riscv-none-elf-gcc` toolchain. Python `reqtool` CLI provides traceability between markdown requirements and code/tests. Three GitHub Actions workflows (`pr`, `nightly`, `release`) plus a `promote-release` workflow that flips draft → published on QA issue close.

**Tech Stack:** nRF Connect SDK v3.0.2 (Zephyr-based, west, sysbuild, MCUboot), CAF, PAW3395 sensor driver, GCC arm-none-eabi (NCS toolchain) + WCH `riscv-none-elf-gcc` (CH32), Python 3.11 (`click`, `pyyaml`, `jinja2`) for reqtool, GitHub Actions, Docker (for CH32 toolchain image), npm/Node 20 for `web-config/`, `git-cliff` for changelogs, MCUboot dev keys (in-repo, integrity only).

**Phases:**
- Phase 1: Repo bootstrap (Tasks 1–3)
- Phase 2: Code migration (Tasks 4–8)
- Phase 3: `reqtool` CLI (Tasks 9–15)
- Phase 4: Initial requirements seed (Tasks 16–17)
- Phase 5: CI pipelines (Tasks 18–24)
- Phase 6: Release pipeline (Tasks 25–31)
- Phase 7: QA gate + dry-run release (Tasks 32–34)

Each task is independently committable. Tasks within a phase generally depend on earlier ones; phases generally depend on the previous phase.

---

## Phase 1 — Repo bootstrap

### Task 1: Initialize the monorepo

**Files:**
- Create: `README.md`
- Create: `.gitignore`
- Create: `LICENSE` (placeholder — confirm with user before final release)

- [ ] **Step 1: Initialize git**

```bash
cd C:/Users/Jiawe/OCorAgent
git init
git branch -M main
```

Expected: `Initialized empty Git repository`, branch renamed to `main`.

- [ ] **Step 2: Write `.gitignore`**

```
# build outputs
build/
build_*/
twister-out/
*.elf
*.hex
*.bin
*.map
*.zip

# west
.west/

# python
__pycache__/
*.pyc
*.egg-info/
.venv/
venv/

# node
node_modules/
dist/

# IDE
.vscode/
.idea/
*.code-workspace

# secrets that should never be in git (dev keys ARE in repo; production keys must NOT be)
*.key
!keys/*.dev.pem

# OS junk
.DS_Store
Thumbs.db
```

- [ ] **Step 3: Write minimal `README.md`**

```markdown
# ocor-mouse

Firmware, dongles, and configurator for the hitscan gaming mouse line.

See `docs/superpowers/specs/2026-05-15-gaming-mouse-workflow-design.md` for the architecture.

## Building

Local build requires nRF Connect SDK v3.0.2 plus the WCH RISC-V toolchain for the high-speed dongle. CI builds all three targets reproducibly — see `.github/workflows/`.
```

- [ ] **Step 4: Commit**

```bash
git add .gitignore README.md
git commit -m "chore: initialize monorepo"
```

---

### Task 2: Author `west.yml` pinning NCS v3.0.2

**Files:**
- Create: `west.yml`

- [ ] **Step 1: Write `west.yml`**

```yaml
manifest:
  version: "1.0"
  self:
    west-commands: ""
  remotes:
    - name: ncs
      url-base: https://github.com/nrfconnect
  projects:
    - name: sdk-nrf
      remote: ncs
      revision: v3.0.2
      path: nrf
      import:
        # pull all of nrf's manifest dependencies (zephyr, mcuboot, nrfxlib, etc.)
        # this gives us a fully reproducible build given a west.yml commit
        name-allowlist:
          - zephyr
          - nrfxlib
          - mcuboot
          - cmsis
          - hal_nordic
          - segger
          - tinycrypt
          - mbedtls
          - psa-arch-tests
          - tf-m-tests
          - trusted-firmware-m
```

- [ ] **Step 2: Verify the manifest parses**

In a Python venv (or system Python with west):

```bash
pip install west
mkdir -p /tmp/west-validate
cp west.yml /tmp/west-validate/west.yml
cd /tmp/west-validate
west init -l .
west manifest --validate
```

Expected: no errors. `west manifest --validate` prints nothing on success.

- [ ] **Step 3: Commit**

```bash
git add west.yml
git commit -m "chore(west): pin nRF Connect SDK v3.0.2"
```

---

### Task 3: Verify `west update` produces a working tree

**Files:**
- Modify: `.gitignore` (already excludes `.west/` and module checkouts)

- [ ] **Step 1: Run `west init` in the repo**

```bash
cd C:/Users/Jiawe/OCorAgent
west init -l .
```

Expected: creates `.west/config` referencing `west.yml`.

- [ ] **Step 2: Run `west update`**

```bash
west update --narrow -o=--depth=1
```

Expected: clones `nrf`, `zephyr`, `mcuboot`, etc. into the repo (sibling directories). Will take several minutes. `--narrow --depth=1` keeps the clone small.

- [ ] **Step 3: Verify versions**

```bash
cat nrf/VERSION
west list
```

Expected: `nrf/VERSION` shows `3.0.2`. `west list` shows all projects with their commits.

- [ ] **Step 4: Commit (no code added — module checkouts are gitignored)**

This step exists to confirm the bootstrap works; nothing new to stage. Move to Phase 2.

---

## Phase 2 — Code migration

### Task 4: Migrate mouse + dongle-ls application source

**Files:**
- Create: `applications/gaming_mouse/` (mirroring `C:\ncs\v3.0.2\nrf\applications\gaming_mouse\` structure but only the parts we own)

- [ ] **Step 1: Create the destination skeleton**

```bash
mkdir -p applications/gaming_mouse/src
mkdir -p applications/gaming_mouse/configuration
mkdir -p applications/gaming_mouse/dts
mkdir -p applications/gaming_mouse/doc
```

- [ ] **Step 2: Copy application root files**

Source: `C:\ncs\v3.0.2\nrf\applications\gaming_mouse\`
Destination: `applications/gaming_mouse/`

Copy these top-level files:
- `CMakeLists.txt`
- `Kconfig`
- `Kconfig.ble`
- `Kconfig.debug`
- `Kconfig.defaults`
- `Kconfig.hid`
- All other `Kconfig.*` files
- `*.rst` documentation files into `applications/gaming_mouse/doc/`

Do NOT copy:
- `build_*/` directories (build artifacts)
- `*.log` files (build logs from earlier runs)
- `*.code-workspace` files (IDE-specific)

Use:
```powershell
$src = "C:\ncs\v3.0.2\nrf\applications\gaming_mouse"
$dst = "C:\Users\Jiawe\OCorAgent\applications\gaming_mouse"
Get-ChildItem $src -File | Where-Object { $_.Name -notmatch '(\.log$|\.code-workspace$)' } | Copy-Item -Destination $dst
```

- [ ] **Step 3: Copy `src/` recursively**

```powershell
Copy-Item "$src\src\*" "$dst\src\" -Recurse
```

The `src/` tree contains `events/`, `hw_interface/`, `modules/`, `util/`, and `main.c`.

- [ ] **Step 4: Copy `dts/` and `configuration/common/`**

```powershell
Copy-Item "$src\dts\*" "$dst\dts\" -Recurse
Copy-Item "$src\configuration\common" "$dst\configuration\" -Recurse
```

- [ ] **Step 5: Copy the three board configurations we want**

```powershell
Copy-Item "$src\configuration\hitscan_nrf52840" "$dst\configuration\" -Recurse
Copy-Item "$src\configuration\hitscan52820_nrf52820" "$dst\configuration\" -Recurse
Copy-Item "$src\configuration\nrf52833dk_nrf52833" "$dst\configuration\" -Recurse
```

The third one (nrf52833 DK) is kept as a dev/test target — useful to verify the build system without needing the customer board.

- [ ] **Step 6: Commit migrated app code**

```bash
git add applications/
git commit -m "feat(app): migrate gaming_mouse application out-of-tree"
```

---

### Task 5: Verify the migrated application builds against pinned NCS

**Files:**
- (No new files; verifies Task 4 + Task 2 produced a buildable tree.)

- [ ] **Step 1: Activate NCS environment**

```bash
# nRF Connect for Desktop sets these; alternatively use the NCS toolchain manager
# the env vars must point to the NCS toolchain installed on the build host
```

Expected: `west --version` and `arm-zephyr-eabi-gcc --version` both run cleanly.

- [ ] **Step 2: Build the mouse (debug) from the out-of-tree app**

```bash
west build -p auto -b hitscan_nrf52840 applications/gaming_mouse \
    --sysbuild \
    -- -DCONF_FILE=prj_esb.conf
```

Expected: build succeeds, produces `build/gaming_mouse/zephyr/zephyr.hex`.

If the build fails because the app references files via `CONFIG_*` paths that assumed an in-tree NCS location, fix the path references — do not move the app back. Common fixes: `Kconfig` sourcing paths, `CMakeLists.txt` `target_include_directories`.

- [ ] **Step 3: Build the low-speed dongle (debug)**

```bash
west build -p auto -b hitscan52820_nrf52820 applications/gaming_mouse \
    --sysbuild \
    -- -DCONF_FILE=prj.conf
```

Expected: build succeeds.

- [ ] **Step 4: Build the DK dev target**

```bash
west build -p auto -b nrf52833dk_nrf52833 applications/gaming_mouse --sysbuild
```

Expected: build succeeds (confirms the app is portable to a stock DK board).

- [ ] **Step 5: Document the build invocations**

Append to `applications/gaming_mouse/doc/building.rst` (create if it does not exist):

```rst
Building out-of-tree
====================

Mouse (debug)::

   west build -p auto -b hitscan_nrf52840 applications/gaming_mouse \
       --sysbuild -- -DCONF_FILE=prj_esb.conf

Mouse (release)::

   west build -p auto -b hitscan_nrf52840 applications/gaming_mouse \
       --sysbuild -- -DCONF_FILE=prj_esb_release.conf

Low-speed dongle (debug)::

   west build -p auto -b hitscan52820_nrf52820 applications/gaming_mouse \
       --sysbuild -- -DCONF_FILE=prj.conf

Low-speed dongle (release)::

   west build -p auto -b hitscan52820_nrf52820 applications/gaming_mouse \
       --sysbuild -- -DCONF_FILE=prj_release.conf
```

- [ ] **Step 6: Commit any build-fix changes**

```bash
git add applications/
git commit -m "fix(app): adjust paths for out-of-tree build"
```

If no fixes were needed, skip the commit and proceed.

---

### Task 6: Migrate CH32V305 high-speed dongle to a portable Makefile build

**Files:**
- Create: `dongle-hs-ch32v305/Makefile`
- Create: `dongle-hs-ch32v305/src/`, `dongle-hs-ch32v305/inc/`, `dongle-hs-ch32v305/lib/`, `dongle-hs-ch32v305/Ld/`, `dongle-hs-ch32v305/Startup/`, `dongle-hs-ch32v305/Peripheral/`, `dongle-hs-ch32v305/User/`, `dongle-hs-ch32v305/Core/`
- Create: `dongle-hs-ch32v305/openocd.cfg`
- Create: `dongle-hs-ch32v305/scripts/` (with `debug_encoding.py`, `patch_usb.py`)

- [ ] **Step 1: Copy CH32 sources from MounRiver Studio project**

Source: `C:\Users\Jiawe\dongle\CH32V305GBU6\`
Destination: `dongle-hs-ch32v305/`

```powershell
$src = "C:\Users\Jiawe\dongle\CH32V305GBU6"
$dst = "C:\Users\Jiawe\OCorAgent\dongle-hs-ch32v305"
New-Item -ItemType Directory -Force $dst
Copy-Item "$src\Core" "$dst\" -Recurse
Copy-Item "$src\Ld" "$dst\" -Recurse
Copy-Item "$src\Startup" "$dst\" -Recurse
Copy-Item "$src\Peripheral" "$dst\" -Recurse
Copy-Item "$src\User" "$dst\" -Recurse
New-Item -ItemType Directory -Force "$dst\scripts"
Copy-Item "$src\debug_encoding.py" "$dst\scripts\"
Copy-Item "$src\patch_usb.py" "$dst\scripts\"
```

Do NOT copy: `obj/`, `Debug/`, `*.hex` build outputs, `*.launch`, `*.wvproj` (MounRiver Studio project files — replaced by Makefile).

- [ ] **Step 2: Write `dongle-hs-ch32v305/Makefile`**

```makefile
# CH32V305GBU6 high-speed USB bridge firmware
# Toolchain: WCH riscv-none-elf-gcc (container image; see .github/actions/setup-ch32/Dockerfile)

PROJECT  := dongle-hs
MCU      := CH32V305xx
TOOLCHAIN_PREFIX ?= riscv-none-elf-

CC      := $(TOOLCHAIN_PREFIX)gcc
OBJCOPY := $(TOOLCHAIN_PREFIX)objcopy
SIZE    := $(TOOLCHAIN_PREFIX)size

BUILD_DIR ?= build

CFLAGS  := -march=rv32imac -mabi=ilp32 -msmall-data-limit=8 \
           -mno-save-restore -Os -fmessage-length=0 \
           -fsigned-char -ffunction-sections -fdata-sections \
           -Wall -g
CFLAGS  += -D$(MCU)
CFLAGS  += -I Core -I User -I Peripheral/inc

LDFLAGS := -T Ld/Link.ld -nostartfiles \
           -Xlinker --gc-sections -Wl,-Map,$(BUILD_DIR)/$(PROJECT).map \
           --specs=nano.specs --specs=nosys.specs

SRCS := $(wildcard Core/*.c) \
        $(wildcard Peripheral/src/*.c) \
        $(wildcard Startup/*.S) \
        $(wildcard User/*.c)

OBJS := $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS:.c=.o)))
OBJS := $(OBJS:.S=.o)

VPATH := Core:Peripheral/src:Startup:User

.PHONY: all clean release

all: $(BUILD_DIR)/$(PROJECT).hex $(BUILD_DIR)/$(PROJECT).bin

release: CFLAGS += -DNDEBUG
release: all

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(PROJECT).elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

$(BUILD_DIR)/$(PROJECT).hex: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
```

Note: the actual paths under `Core/`, `Peripheral/`, etc. and exact `CFLAGS` depend on what MounRiver Studio generated. The Makefile above is a starting template; **before declaring this task complete, run a build (Step 4 below) and adjust the file lists and include paths until the build succeeds**.

- [ ] **Step 3: Write a minimal OpenOCD config**

`dongle-hs-ch32v305/openocd.cfg`:

```
# Stub for WCH-Link; the WCH OpenOCD fork has CH32V305 support
adapter driver wlink
adapter speed 6000
transport select sdi
target create ch32v305 riscv -dap [dap create ch32.dap -chain-position auto0.tap] -coreid 0
```

(The exact OpenOCD invocation depends on the user's debugger; this file documents intent. Final flashing scripts arrive in Phase 5.)

- [ ] **Step 4: Build locally to verify the Makefile**

Until the container image exists (Task 19), install `riscv-none-elf-gcc` locally to verify:

```bash
cd dongle-hs-ch32v305
make
```

Expected: produces `build/dongle-hs.hex` and `build/dongle-hs.bin`. Iterate on Makefile (file lists, include paths) until clean build.

If the user cannot easily install the toolchain locally, mark this step as "deferred to Task 19 (container build)" and proceed — the container build will surface any Makefile issues.

- [ ] **Step 5: Commit**

```bash
git add dongle-hs-ch32v305/
git commit -m "feat(dongle-hs): migrate CH32V305 sources with Makefile build"
```

---

### Task 7: Migrate web configurator

**Files:**
- Create: `web-config/`

- [ ] **Step 1: Copy web sources**

Source: `C:\Users\Jiawe\Desktop\hitscan\webDriver\`
Destination: `web-config/`

```powershell
$src = "C:\Users\Jiawe\Desktop\hitscan\webDriver"
$dst = "C:\Users\Jiawe\OCorAgent\web-config"
New-Item -ItemType Directory -Force $dst
Copy-Item "$src\*" $dst -Recurse
```

This copies `index.html`, `layout_webhid.html`, `preview_shaver.html`, `shaver.html`, plus any image files.

- [ ] **Step 2: Add minimal `package.json` for a static-site build**

`web-config/package.json`:

```json
{
  "name": "ocor-mouse-web-config",
  "version": "0.0.0",
  "private": true,
  "type": "module",
  "scripts": {
    "build": "node build.mjs",
    "preview": "npx serve dist -p 5173"
  },
  "devDependencies": {
    "esbuild": "^0.21.0"
  }
}
```

- [ ] **Step 3: Write a minimal `build.mjs` that copies sources into `dist/`**

`web-config/build.mjs`:

```javascript
import { mkdirSync, copyFileSync, readdirSync, statSync, rmSync } from 'node:fs';
import { join } from 'node:path';

const srcDir = '.';
const distDir = 'dist';

rmSync(distDir, { recursive: true, force: true });
mkdirSync(distDir, { recursive: true });

const skip = new Set(['dist', 'node_modules', 'build.mjs', 'package.json', 'package-lock.json']);

function copyTree(src, dst) {
  for (const entry of readdirSync(src)) {
    if (skip.has(entry)) continue;
    const s = join(src, entry);
    const d = join(dst, entry);
    if (statSync(s).isDirectory()) {
      mkdirSync(d, { recursive: true });
      copyTree(s, d);
    } else {
      copyFileSync(s, d);
    }
  }
}

copyTree(srcDir, distDir);
console.log(`Built to ${distDir}/`);
```

This is the simplest possible "build" — pass-through copy into `dist/`. When the web team needs bundling, they can replace `build.mjs` with esbuild/vite. The point right now is to have a `dist/` directory that GitHub Pages can deploy.

- [ ] **Step 4: Verify the build runs**

```bash
cd web-config
npm install
npm run build
ls dist/
```

Expected: `dist/index.html`, `dist/layout_webhid.html`, etc. all present.

- [ ] **Step 5: Add `web-config/.gitignore`**

```
node_modules/
dist/
```

- [ ] **Step 6: Commit**

```bash
git add web-config/
git commit -m "feat(web-config): migrate WebHID configurator with esbuild scaffold"
```

---

### Task 8: Create empty placeholder for deferred `host-tool/`

**Files:**
- Create: `host-tool/README.md`

- [ ] **Step 1: Write a placeholder README explaining the directory is reserved**

`host-tool/README.md`:

```markdown
# host-tool — reserved

This directory is intentionally empty in v0.x. Per the workflow spec (§2 non-goals, §11 deferred), the native desktop configurator is not built initially — `web-config/` (WebHID) is the only end-user surface.

Build this out only if WebHID coverage proves insufficient. When built, mirror the structure of `web-config/`: own `package.json`, own CI job in `pr.yml`, separate installer artifact in `release.yml`.
```

- [ ] **Step 2: Commit**

```bash
git add host-tool/README.md
git commit -m "docs(host-tool): reserve directory for future native config app"
```

---

## Phase 3 — `reqtool` CLI

### Task 9: Scaffold the Python package

**Files:**
- Create: `tools/reqtool/pyproject.toml`
- Create: `tools/reqtool/reqtool/__init__.py`
- Create: `tools/reqtool/reqtool/__main__.py`
- Create: `tools/reqtool/reqtool/cli.py`
- Create: `tools/reqtool/tests/__init__.py`
- Create: `tools/reqtool/README.md`

- [ ] **Step 1: Write `pyproject.toml`**

```toml
[project]
name = "reqtool"
version = "0.1.0"
description = "Requirements & traceability CLI for ocor-mouse"
requires-python = ">=3.11"
dependencies = [
    "click>=8.1",
    "pyyaml>=6.0",
    "jinja2>=3.1",
]

[project.scripts]
reqtool = "reqtool.cli:main"

[project.optional-dependencies]
dev = ["pytest>=8.0", "pytest-cov>=5.0"]

[build-system]
requires = ["setuptools>=61"]
build-backend = "setuptools.build_meta"

[tool.setuptools.packages.find]
where = ["."]
include = ["reqtool*"]
```

- [ ] **Step 2: Create empty `__init__.py` files**

```bash
touch tools/reqtool/reqtool/__init__.py
touch tools/reqtool/tests/__init__.py
```

- [ ] **Step 3: Write `reqtool/cli.py` with the CLI scaffold**

```python
"""reqtool — requirements & traceability CLI for ocor-mouse."""
from __future__ import annotations

import sys
import click


@click.group()
@click.version_option()
def main() -> None:
    """Requirements & traceability for the ocor-mouse monorepo."""


@main.command()
@click.option("--reqs-dir", default="docs/requirements", help="Requirements directory.")
def lint(reqs_dir: str) -> None:
    """Validate requirement frontmatter (schema, unique IDs, allowed enum values)."""
    click.echo(f"lint: {reqs_dir} (stub)")
    sys.exit(0)


@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".", help="Repo root to grep for REQ-ID references.")
@click.option("--diff/--no-diff", default=False, help="Output a diff vs. base ref.")
@click.option("--base", default="origin/main", help="Base ref for diff mode.")
def trace(reqs_dir: str, root: str, diff: bool, base: str) -> None:
    """Build traceability matrix linking REQs to code and tests."""
    click.echo(f"trace: {reqs_dir} root={root} diff={diff} base={base} (stub)")
    sys.exit(0)


@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".")
def orphans(reqs_dir: str, root: str) -> None:
    """Report implemented REQs with no code references, and dangling code refs."""
    click.echo(f"orphans: {reqs_dir} root={root} (stub)")
    sys.exit(0)


@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--tag", required=True, help="Release tag (e.g. v1.0.0).")
@click.option("--products", default="hitscan", help="Comma-separated products in this release.")
def checklist(reqs_dir: str, tag: str, products: str) -> None:
    """Emit a QA checklist (markdown) for verification=HIL REQs in this release."""
    click.echo(f"checklist: {reqs_dir} tag={tag} products={products} (stub)")
    sys.exit(0)


@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".")
@click.option("--tag", required=True)
def report(reqs_dir: str, root: str, tag: str) -> None:
    """Emit the full release traceability report (markdown)."""
    click.echo(f"report: {reqs_dir} root={root} tag={tag} (stub)")
    sys.exit(0)


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Write `reqtool/__main__.py`**

```python
from reqtool.cli import main

if __name__ == "__main__":
    main()
```

- [ ] **Step 5: Install and smoke-test**

```bash
python -m venv .venv
. .venv/bin/activate    # PowerShell: .venv\Scripts\Activate.ps1
pip install -e tools/reqtool[dev]
reqtool --help
```

Expected: shows the five subcommands.

- [ ] **Step 6: Commit**

```bash
git add tools/reqtool/
git commit -m "feat(reqtool): scaffold CLI with five subcommands"
```

---

### Task 10: Implement frontmatter schema (TDD)

**Files:**
- Create: `tools/reqtool/reqtool/schema.py`
- Create: `tools/reqtool/tests/test_schema.py`

- [ ] **Step 1: Write the failing test**

`tools/reqtool/tests/test_schema.py`:

```python
import pytest
from reqtool.schema import parse_requirement, RequirementError


VALID = """---
id: REQ-DPI-001
title: DPI adjustable 100-32000 in 50-step increments
status: approved
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

## Description
Body text.
"""


def test_parse_valid_requirement():
    req = parse_requirement(VALID, source="REQ-DPI-001.md")
    assert req.id == "REQ-DPI-001"
    assert req.title.startswith("DPI adjustable")
    assert req.status == "approved"
    assert req.verification == "HIL"
    assert req.priority == "P0"
    assert req.owner == "fw-team"
    assert req.products == ["hitscan"]
    assert "Body text" in req.body


def test_missing_id_raises():
    bad = VALID.replace("id: REQ-DPI-001\n", "")
    with pytest.raises(RequirementError, match="missing required field: id"):
        parse_requirement(bad, source="bad.md")


def test_unknown_status_raises():
    bad = VALID.replace("status: approved", "status: pending")
    with pytest.raises(RequirementError, match="invalid status"):
        parse_requirement(bad, source="bad.md")


def test_unknown_verification_raises():
    bad = VALID.replace("verification: HIL", "verification: vibes")
    with pytest.raises(RequirementError, match="invalid verification"):
        parse_requirement(bad, source="bad.md")


def test_id_format_enforced():
    bad = VALID.replace("REQ-DPI-001", "DPI-001")
    with pytest.raises(RequirementError, match="invalid id format"):
        parse_requirement(bad, source="bad.md")


def test_no_frontmatter_raises():
    with pytest.raises(RequirementError, match="missing frontmatter"):
        parse_requirement("plain body, no frontmatter", source="bad.md")
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
pytest tools/reqtool/tests/test_schema.py -v
```

Expected: all tests fail with `ImportError: cannot import name 'parse_requirement'`.

- [ ] **Step 3: Implement `schema.py`**

```python
"""Frontmatter parsing and validation for requirement markdown files."""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any

import yaml


ID_RE = re.compile(r"^REQ-[A-Z][A-Z0-9]+-\d{3}$")
ALLOWED_STATUS = {"draft", "approved", "implemented", "verified", "deprecated"}
ALLOWED_VERIFICATION = {"code-review", "unit-test", "native-sim", "HIL"}
ALLOWED_PRIORITY = {"P0", "P1", "P2"}
REQUIRED_FIELDS = ("id", "title", "status", "verification", "priority", "owner", "products")


class RequirementError(Exception):
    """Raised when a requirement file fails schema validation."""


@dataclass
class Requirement:
    id: str
    title: str
    status: str
    verification: str
    priority: str
    owner: str
    products: list[str]
    body: str
    source: str = ""
    extra: dict[str, Any] = field(default_factory=dict)


def parse_requirement(text: str, source: str) -> Requirement:
    if not text.startswith("---"):
        raise RequirementError(f"{source}: missing frontmatter (file must start with '---')")
    parts = text.split("---", 2)
    if len(parts) < 3:
        raise RequirementError(f"{source}: missing frontmatter close ('---')")
    front_raw, body = parts[1], parts[2]
    try:
        front = yaml.safe_load(front_raw) or {}
    except yaml.YAMLError as e:
        raise RequirementError(f"{source}: invalid YAML frontmatter: {e}") from e
    if not isinstance(front, dict):
        raise RequirementError(f"{source}: frontmatter must be a mapping")

    for fld in REQUIRED_FIELDS:
        if fld not in front:
            raise RequirementError(f"{source}: missing required field: {fld}")

    rid = str(front["id"])
    if not ID_RE.match(rid):
        raise RequirementError(
            f"{source}: invalid id format: '{rid}' (expected REQ-AREA-NNN)"
        )
    status = str(front["status"])
    if status not in ALLOWED_STATUS:
        raise RequirementError(
            f"{source}: invalid status '{status}' (allowed: {sorted(ALLOWED_STATUS)})"
        )
    verification = str(front["verification"])
    if verification not in ALLOWED_VERIFICATION:
        raise RequirementError(
            f"{source}: invalid verification '{verification}' "
            f"(allowed: {sorted(ALLOWED_VERIFICATION)})"
        )
    priority = str(front["priority"])
    if priority not in ALLOWED_PRIORITY:
        raise RequirementError(
            f"{source}: invalid priority '{priority}' (allowed: {sorted(ALLOWED_PRIORITY)})"
        )

    products = front["products"]
    if not isinstance(products, list) or not all(isinstance(p, str) for p in products):
        raise RequirementError(f"{source}: products must be a list of strings")

    known = set(REQUIRED_FIELDS)
    extra = {k: v for k, v in front.items() if k not in known}

    return Requirement(
        id=rid,
        title=str(front["title"]),
        status=status,
        verification=verification,
        priority=priority,
        owner=str(front["owner"]),
        products=products,
        body=body.lstrip("\n"),
        source=source,
        extra=extra,
    )
```

- [ ] **Step 4: Run tests; verify they pass**

```bash
pytest tools/reqtool/tests/test_schema.py -v
```

Expected: 6 passed.

- [ ] **Step 5: Commit**

```bash
git add tools/reqtool/reqtool/schema.py tools/reqtool/tests/test_schema.py
git commit -m "feat(reqtool): frontmatter schema + parser with tests"
```

---

### Task 11: Wire `lint` command to the schema (TDD)

**Files:**
- Modify: `tools/reqtool/reqtool/cli.py`
- Create: `tools/reqtool/reqtool/lint.py`
- Create: `tools/reqtool/tests/test_lint.py`
- Create: `tools/reqtool/tests/fixtures/valid_reqs/REQ-DPI-001.md`
- Create: `tools/reqtool/tests/fixtures/bad_reqs/REQ-BAD-001.md`
- Create: `tools/reqtool/tests/fixtures/dup_reqs/REQ-DUP-001.md` and `REQ-DUP-001-copy.md`

- [ ] **Step 1: Create fixtures**

`tools/reqtool/tests/fixtures/valid_reqs/REQ-DPI-001.md`:
```markdown
---
id: REQ-DPI-001
title: Valid one
status: approved
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

Valid.
```

`tools/reqtool/tests/fixtures/bad_reqs/REQ-BAD-001.md`:
```markdown
---
id: REQ-BAD-001
title: Bad
status: pending
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

Invalid status.
```

`tools/reqtool/tests/fixtures/dup_reqs/REQ-DUP-001.md` and `REQ-DUP-001-copy.md` — both with the same `id: REQ-DUP-001`:

```markdown
---
id: REQ-DUP-001
title: Duplicate
status: approved
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---
```

- [ ] **Step 2: Write the failing test**

`tools/reqtool/tests/test_lint.py`:

```python
from pathlib import Path

from reqtool.lint import lint_directory

FIX = Path(__file__).parent / "fixtures"


def test_lint_passes_valid():
    errors = lint_directory(FIX / "valid_reqs")
    assert errors == []


def test_lint_detects_bad_status():
    errors = lint_directory(FIX / "bad_reqs")
    assert len(errors) == 1
    assert "invalid status" in errors[0]


def test_lint_detects_duplicate_ids():
    errors = lint_directory(FIX / "dup_reqs")
    assert any("duplicate id" in e for e in errors)
```

- [ ] **Step 3: Run; verify failure**

```bash
pytest tools/reqtool/tests/test_lint.py -v
```

Expected: import error.

- [ ] **Step 4: Implement `reqtool/lint.py`**

```python
"""Lint command: validate frontmatter schema and uniqueness of IDs."""
from __future__ import annotations

from pathlib import Path

from reqtool.schema import RequirementError, parse_requirement


def lint_directory(reqs_dir: Path) -> list[str]:
    """Return a list of error strings. Empty list = lint passed."""
    errors: list[str] = []
    seen_ids: dict[str, Path] = {}

    if not reqs_dir.exists():
        return [f"{reqs_dir}: directory does not exist"]

    for md in sorted(reqs_dir.rglob("REQ-*.md")):
        try:
            req = parse_requirement(md.read_text(encoding="utf-8"), source=str(md))
        except RequirementError as e:
            errors.append(str(e))
            continue
        if req.id in seen_ids:
            errors.append(
                f"{md}: duplicate id '{req.id}' (also in {seen_ids[req.id]})"
            )
        else:
            seen_ids[req.id] = md
    return errors
```

- [ ] **Step 5: Wire `lint` subcommand in `cli.py`**

Replace the stub in `tools/reqtool/reqtool/cli.py`:

```python
@main.command()
@click.option("--reqs-dir", default="docs/requirements", help="Requirements directory.")
def lint(reqs_dir: str) -> None:
    """Validate requirement frontmatter (schema, unique IDs, allowed enum values)."""
    from pathlib import Path
    from reqtool.lint import lint_directory

    errors = lint_directory(Path(reqs_dir))
    if errors:
        for e in errors:
            click.echo(f"ERROR: {e}", err=True)
        click.echo(f"\n{len(errors)} error(s) in {reqs_dir}", err=True)
        sys.exit(1)
    click.echo(f"OK: {reqs_dir} (lint passed)")
```

- [ ] **Step 6: Run tests; verify pass**

```bash
pytest tools/reqtool/tests/test_lint.py -v
```

Expected: 3 passed.

- [ ] **Step 7: CLI smoke test**

```bash
reqtool lint --reqs-dir tools/reqtool/tests/fixtures/valid_reqs
reqtool lint --reqs-dir tools/reqtool/tests/fixtures/bad_reqs ; echo "exit=$?"
```

Expected: first prints `OK:`, second prints `ERROR: ... invalid status` and exits 1.

- [ ] **Step 8: Commit**

```bash
git add tools/reqtool/
git commit -m "feat(reqtool): implement lint command with schema + uniqueness checks"
```

---

### Task 12: Implement `trace` command (TDD)

**Files:**
- Create: `tools/reqtool/reqtool/trace.py`
- Create: `tools/reqtool/tests/test_trace.py`
- Modify: `tools/reqtool/reqtool/cli.py`
- Create: `tools/reqtool/tests/fixtures/sample_repo/` with reqs + code

- [ ] **Step 1: Create sample-repo fixture**

`tools/reqtool/tests/fixtures/sample_repo/docs/requirements/REQ-DPI-001.md`:
```markdown
---
id: REQ-DPI-001
title: DPI step size
status: implemented
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---
```

`tools/reqtool/tests/fixtures/sample_repo/docs/requirements/REQ-RGB-001.md`:
```markdown
---
id: REQ-RGB-001
title: RGB modes
status: approved
verification: code-review
priority: P1
owner: fw-team
products: [hitscan]
---
```

`tools/reqtool/tests/fixtures/sample_repo/src/dpi.c`:
```c
/* REQ-DPI-001: enforce step size */
int set_dpi(int dpi) { return dpi % 50 == 0 ? 0 : -1; }
```

`tools/reqtool/tests/fixtures/sample_repo/tests/test_dpi.c`:
```c
/* REQ-DPI-001 */
void test_step_size(void) {}
```

`tools/reqtool/tests/fixtures/sample_repo/src/dangling.c`:
```c
/* REQ-NOSUCH-999: this REQ does not exist */
int unused(void) { return 0; }
```

- [ ] **Step 2: Write the failing test**

`tools/reqtool/tests/test_trace.py`:

```python
from pathlib import Path

from reqtool.trace import build_traceability_matrix

FIX = Path(__file__).parent / "fixtures" / "sample_repo"


def test_trace_finds_code_references():
    matrix = build_traceability_matrix(
        reqs_dir=FIX / "docs" / "requirements",
        root=FIX,
    )
    dpi = matrix["REQ-DPI-001"]
    refs = {r.path.name for r in dpi.references}
    assert "dpi.c" in refs
    assert "test_dpi.c" in refs


def test_trace_marks_unreferenced_req():
    matrix = build_traceability_matrix(
        reqs_dir=FIX / "docs" / "requirements",
        root=FIX,
    )
    rgb = matrix["REQ-RGB-001"]
    assert rgb.references == []


def test_trace_returns_dangling_refs():
    matrix = build_traceability_matrix(
        reqs_dir=FIX / "docs" / "requirements",
        root=FIX,
    )
    assert "REQ-NOSUCH-999" in matrix.dangling
```

- [ ] **Step 3: Run; verify failure**

```bash
pytest tools/reqtool/tests/test_trace.py -v
```

Expected: import error.

- [ ] **Step 4: Implement `reqtool/trace.py`**

```python
"""Trace command: cross-reference REQ-IDs in code/tests against requirement files."""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

from reqtool.schema import Requirement, RequirementError, parse_requirement


REQ_ID_RE = re.compile(r"\bREQ-[A-Z][A-Z0-9]+-\d{3}\b")

SCAN_EXTS = {".c", ".h", ".cpp", ".hpp", ".rs", ".py", ".js", ".ts", ".mjs", ".html", ".md"}
SKIP_DIRS = {"build", "build_*", "node_modules", ".west", "dist", "__pycache__", ".git", ".venv"}


@dataclass
class Reference:
    path: Path
    line: int
    snippet: str


@dataclass
class RequirementRow:
    req: Requirement
    references: list[Reference] = field(default_factory=list)


@dataclass
class Matrix:
    rows: dict[str, RequirementRow] = field(default_factory=dict)
    dangling: dict[str, list[Reference]] = field(default_factory=dict)

    def __getitem__(self, key: str) -> RequirementRow:
        return self.rows[key]

    def __contains__(self, key: str) -> bool:
        return key in self.rows


def _iter_source_files(root: Path):
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        if p.suffix not in SCAN_EXTS:
            continue
        parts = set(p.parts)
        if parts & SKIP_DIRS:
            continue
        if "requirements" in p.parts:
            # don't grep the req files themselves as "references" to themselves
            continue
        yield p


def build_traceability_matrix(reqs_dir: Path, root: Path) -> Matrix:
    matrix = Matrix()
    for md in sorted(reqs_dir.rglob("REQ-*.md")):
        try:
            req = parse_requirement(md.read_text(encoding="utf-8"), source=str(md))
        except RequirementError:
            continue
        matrix.rows[req.id] = RequirementRow(req=req)

    for src in _iter_source_files(root):
        try:
            content = src.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        for lineno, line in enumerate(content.splitlines(), start=1):
            for match in REQ_ID_RE.finditer(line):
                rid = match.group(0)
                ref = Reference(path=src, line=lineno, snippet=line.strip())
                if rid in matrix.rows:
                    matrix.rows[rid].references.append(ref)
                else:
                    matrix.dangling.setdefault(rid, []).append(ref)
    return matrix
```

- [ ] **Step 5: Run tests; verify pass**

```bash
pytest tools/reqtool/tests/test_trace.py -v
```

Expected: 3 passed.

- [ ] **Step 6: Wire CLI to emit matrix**

Replace `trace` stub in `cli.py`:

```python
@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".", help="Repo root to grep for REQ-ID references.")
@click.option("--output", default="-", help="Output file (- for stdout).")
def trace(reqs_dir: str, root: str, output: str) -> None:
    """Build traceability matrix linking REQs to code and tests."""
    from pathlib import Path
    from reqtool.trace import build_traceability_matrix
    from reqtool.render import render_matrix_markdown

    matrix = build_traceability_matrix(Path(reqs_dir), Path(root))
    md = render_matrix_markdown(matrix)
    if output == "-":
        click.echo(md)
    else:
        Path(output).write_text(md, encoding="utf-8")
        click.echo(f"wrote {output}", err=True)
```

- [ ] **Step 7: Implement `reqtool/render.py`**

```python
"""Markdown rendering for traceability matrix and reports."""
from __future__ import annotations

from reqtool.trace import Matrix


def render_matrix_markdown(matrix: Matrix) -> str:
    lines = [
        "# Traceability Matrix",
        "",
        "| REQ ID | Title | Status | Verification | References |",
        "|---|---|---|---|---|",
    ]
    for rid in sorted(matrix.rows):
        row = matrix.rows[rid]
        req = row.req
        ref_count = len(row.references)
        ref_summary = f"{ref_count} ref(s)" if ref_count else "—"
        lines.append(
            f"| {req.id} | {req.title} | {req.status} | {req.verification} | {ref_summary} |"
        )

    if matrix.dangling:
        lines += ["", "## Dangling references (REQ-IDs in code with no matching requirement)", ""]
        for rid in sorted(matrix.dangling):
            for ref in matrix.dangling[rid]:
                lines.append(f"- `{rid}` in `{ref.path}:{ref.line}`")
    return "\n".join(lines) + "\n"
```

- [ ] **Step 8: CLI smoke test**

```bash
reqtool trace --reqs-dir tools/reqtool/tests/fixtures/sample_repo/docs/requirements \
              --root tools/reqtool/tests/fixtures/sample_repo
```

Expected: prints a markdown table with REQ-DPI-001 (2 refs), REQ-RGB-001 (— refs), plus a dangling section for REQ-NOSUCH-999.

- [ ] **Step 9: Commit**

```bash
git add tools/reqtool/
git commit -m "feat(reqtool): trace command builds traceability matrix"
```

---

### Task 13: Implement `orphans` and `checklist` commands

**Files:**
- Create: `tools/reqtool/reqtool/orphans.py`
- Create: `tools/reqtool/reqtool/checklist.py`
- Create: `tools/reqtool/tests/test_orphans.py`
- Create: `tools/reqtool/tests/test_checklist.py`
- Modify: `tools/reqtool/reqtool/cli.py`

- [ ] **Step 1: Write the failing test for orphans**

`tools/reqtool/tests/test_orphans.py`:

```python
from pathlib import Path
from reqtool.orphans import find_orphans
from reqtool.trace import build_traceability_matrix

FIX = Path(__file__).parent / "fixtures" / "sample_repo"


def test_orphans_finds_unreferenced_implemented():
    """REQ-DPI-001 is implemented and has refs; REQ-RGB-001 is approved and has none."""
    # We need a req that is status=implemented AND has zero refs.
    # The fixture has implemented REQ-DPI-001 (referenced) and approved REQ-RGB-001 (unreferenced).
    # So we should get an empty orphan list here.
    matrix = build_traceability_matrix(FIX / "docs" / "requirements", FIX)
    result = find_orphans(matrix)
    assert result.unreferenced_implemented == []


def test_orphans_finds_dangling():
    matrix = build_traceability_matrix(FIX / "docs" / "requirements", FIX)
    result = find_orphans(matrix)
    assert "REQ-NOSUCH-999" in result.dangling_ids
```

- [ ] **Step 2: Write the failing test for checklist**

`tools/reqtool/tests/test_checklist.py`:

```python
from pathlib import Path
from reqtool.checklist import build_checklist
from reqtool.trace import build_traceability_matrix

FIX = Path(__file__).parent / "fixtures" / "sample_repo"


def test_checklist_includes_hil_reqs_only():
    matrix = build_traceability_matrix(FIX / "docs" / "requirements", FIX)
    md = build_checklist(matrix, tag="v0.1.0", products=["hitscan"])
    assert "REQ-DPI-001" in md  # verification=HIL
    assert "REQ-RGB-001" not in md  # verification=code-review
    assert "v0.1.0" in md
    assert "- [ ]" in md


def test_checklist_filters_by_product():
    matrix = build_traceability_matrix(FIX / "docs" / "requirements", FIX)
    md = build_checklist(matrix, tag="v0.1.0", products=["nonexistent"])
    assert "REQ-DPI-001" not in md  # not in product 'nonexistent'
```

- [ ] **Step 3: Run; verify both fail**

```bash
pytest tools/reqtool/tests/test_orphans.py tools/reqtool/tests/test_checklist.py -v
```

Expected: import errors.

- [ ] **Step 4: Implement `reqtool/orphans.py`**

```python
"""Orphan / dangling-reference detection."""
from __future__ import annotations

from dataclasses import dataclass, field

from reqtool.trace import Matrix


@dataclass
class OrphanReport:
    unreferenced_implemented: list[str] = field(default_factory=list)
    dangling_ids: list[str] = field(default_factory=list)


def find_orphans(matrix: Matrix) -> OrphanReport:
    report = OrphanReport()
    for rid, row in matrix.rows.items():
        if row.req.status == "implemented" and not row.references:
            report.unreferenced_implemented.append(rid)
    report.dangling_ids = sorted(matrix.dangling.keys())
    return report
```

- [ ] **Step 5: Implement `reqtool/checklist.py`**

```python
"""Per-release QA checklist generator (HIL-verification REQs only)."""
from __future__ import annotations

from reqtool.trace import Matrix


def build_checklist(matrix: Matrix, tag: str, products: list[str]) -> str:
    relevant = [
        row for row in matrix.rows.values()
        if row.req.verification == "HIL"
        and any(p in row.req.products for p in products)
    ]
    relevant.sort(key=lambda r: r.req.id)

    lines = [
        f"# Release QA: {tag}",
        "",
        f"Auto-generated by `reqtool checklist --tag {tag}`. Tick boxes after manual",
        "verification on physical hardware. Close this issue (with all boxes ticked)",
        "to promote the GitHub Release from draft to published.",
        "",
        "## HIL verification",
        "",
    ]
    if not relevant:
        lines.append("_No HIL-verification requirements apply to this release._")
    else:
        for row in relevant:
            lines.append(f"- [ ] {row.req.id}: {row.req.title}")

    lines += [
        "",
        "## Sign-off",
        "",
        "- [ ] QA lead approval",
        "- [ ] FW lead approval",
        "",
    ]
    return "\n".join(lines) + "\n"
```

- [ ] **Step 6: Run tests; verify pass**

```bash
pytest tools/reqtool/tests/ -v
```

Expected: all tests in the suite pass (schema + lint + trace + orphans + checklist).

- [ ] **Step 7: Wire CLI commands in `cli.py`**

Replace the `orphans` and `checklist` stubs:

```python
@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".")
def orphans(reqs_dir: str, root: str) -> None:
    """Report implemented REQs with no code references, and dangling code refs."""
    from pathlib import Path
    from reqtool.trace import build_traceability_matrix
    from reqtool.orphans import find_orphans

    matrix = build_traceability_matrix(Path(reqs_dir), Path(root))
    rep = find_orphans(matrix)
    if rep.unreferenced_implemented:
        click.echo("Implemented but unreferenced:")
        for rid in rep.unreferenced_implemented:
            click.echo(f"  - {rid}")
    if rep.dangling_ids:
        click.echo("Dangling REQ-IDs in code (no matching requirement):")
        for rid in rep.dangling_ids:
            click.echo(f"  - {rid}")
    if not rep.unreferenced_implemented and not rep.dangling_ids:
        click.echo("OK: no orphans")


@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".")
@click.option("--tag", required=True)
@click.option("--products", default="hitscan")
@click.option("--output", default="-")
def checklist(reqs_dir: str, root: str, tag: str, products: str, output: str) -> None:
    """Emit a QA checklist (markdown) for verification=HIL REQs in this release."""
    from pathlib import Path
    from reqtool.trace import build_traceability_matrix
    from reqtool.checklist import build_checklist

    matrix = build_traceability_matrix(Path(reqs_dir), Path(root))
    md = build_checklist(matrix, tag=tag, products=products.split(","))
    if output == "-":
        click.echo(md)
    else:
        Path(output).write_text(md, encoding="utf-8")
        click.echo(f"wrote {output}", err=True)
```

- [ ] **Step 8: Commit**

```bash
git add tools/reqtool/
git commit -m "feat(reqtool): orphans + checklist commands"
```

---

### Task 14: Implement `report` command and `trace --diff` mode

**Files:**
- Create: `tools/reqtool/reqtool/report.py`
- Create: `tools/reqtool/reqtool/diff.py`
- Create: `tools/reqtool/tests/test_report.py`
- Create: `tools/reqtool/tests/test_diff.py`
- Modify: `tools/reqtool/reqtool/cli.py`

- [ ] **Step 1: Write failing tests**

`tools/reqtool/tests/test_report.py`:

```python
from pathlib import Path
from reqtool.report import build_report
from reqtool.trace import build_traceability_matrix

FIX = Path(__file__).parent / "fixtures" / "sample_repo"


def test_report_has_header_and_counts():
    matrix = build_traceability_matrix(FIX / "docs" / "requirements", FIX)
    md = build_report(matrix, tag="v0.1.0")
    assert "# Traceability Report — v0.1.0" in md
    assert "## Summary" in md
    assert "## Per-requirement" in md
    assert "REQ-DPI-001" in md
```

`tools/reqtool/tests/test_diff.py`:

```python
from reqtool.diff import diff_matrices


def test_diff_detects_added_ref():
    before = {"REQ-A-001": {"a.c:1"}}
    after  = {"REQ-A-001": {"a.c:1", "b.c:5"}}
    d = diff_matrices(before, after)
    assert "REQ-A-001" in d.changed
    assert "b.c:5" in d.changed["REQ-A-001"].added


def test_diff_detects_removed_req():
    before = {"REQ-A-001": {"a.c:1"}, "REQ-B-001": {"b.c:1"}}
    after  = {"REQ-A-001": {"a.c:1"}}
    d = diff_matrices(before, after)
    assert "REQ-B-001" in d.removed
```

- [ ] **Step 2: Run; verify failure**

```bash
pytest tools/reqtool/tests/test_report.py tools/reqtool/tests/test_diff.py -v
```

Expected: import errors.

- [ ] **Step 3: Implement `reqtool/report.py`**

```python
"""Full release traceability report — bundled into release artifacts."""
from __future__ import annotations

from collections import Counter

from reqtool.trace import Matrix


def build_report(matrix: Matrix, tag: str) -> str:
    status_counts: Counter[str] = Counter()
    verif_counts: Counter[str] = Counter()
    referenced = 0
    for row in matrix.rows.values():
        status_counts[row.req.status] += 1
        verif_counts[row.req.verification] += 1
        if row.references:
            referenced += 1
    total = len(matrix.rows)

    lines = [
        f"# Traceability Report — {tag}",
        "",
        "## Summary",
        "",
        f"- Total requirements: **{total}**",
        f"- Referenced from code/tests: **{referenced}**",
        f"- Dangling REQ-IDs in code: **{len(matrix.dangling)}**",
        "",
        "### By status",
        "",
    ]
    for s in sorted(status_counts):
        lines.append(f"- {s}: {status_counts[s]}")
    lines += ["", "### By verification method", ""]
    for v in sorted(verif_counts):
        lines.append(f"- {v}: {verif_counts[v]}")

    lines += ["", "## Per-requirement", ""]
    for rid in sorted(matrix.rows):
        row = matrix.rows[rid]
        req = row.req
        lines += [
            f"### {req.id} — {req.title}",
            "",
            f"- Status: `{req.status}` · Verification: `{req.verification}` · "
            f"Priority: `{req.priority}` · Owner: `{req.owner}`",
            f"- Products: {', '.join(req.products) or '—'}",
            f"- References: {len(row.references)}",
            "",
        ]
        for ref in row.references[:20]:
            lines.append(f"  - `{ref.path}:{ref.line}` — {ref.snippet[:80]}")
        if len(row.references) > 20:
            lines.append(f"  - ... ({len(row.references) - 20} more)")
        lines.append("")

    if matrix.dangling:
        lines += ["## Dangling references", ""]
        for rid in sorted(matrix.dangling):
            for ref in matrix.dangling[rid]:
                lines.append(f"- `{rid}` in `{ref.path}:{ref.line}`")
    return "\n".join(lines) + "\n"
```

- [ ] **Step 4: Implement `reqtool/diff.py`**

```python
"""Diff two traceability snapshots (used by `trace --diff` in PR CI)."""
from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class RefDiff:
    added: set[str] = field(default_factory=set)
    removed: set[str] = field(default_factory=set)


@dataclass
class MatrixDiff:
    added: list[str] = field(default_factory=list)
    removed: list[str] = field(default_factory=list)
    changed: dict[str, RefDiff] = field(default_factory=dict)


def diff_matrices(before: dict[str, set[str]], after: dict[str, set[str]]) -> MatrixDiff:
    d = MatrixDiff()
    d.added = sorted(set(after) - set(before))
    d.removed = sorted(set(before) - set(after))
    for rid in sorted(set(before) & set(after)):
        added = after[rid] - before[rid]
        removed = before[rid] - after[rid]
        if added or removed:
            d.changed[rid] = RefDiff(added=added, removed=removed)
    return d


def matrix_to_refset(matrix) -> dict[str, set[str]]:
    """Flatten Matrix → {req_id: {'path:line', ...}} for diffing."""
    return {
        rid: {f"{r.path}:{r.line}" for r in row.references}
        for rid, row in matrix.rows.items()
    }
```

- [ ] **Step 5: Run tests; verify pass**

```bash
pytest tools/reqtool/tests/ -v
```

Expected: all tests pass.

- [ ] **Step 6: Wire `report` and `trace --diff` in CLI**

Replace stubs in `cli.py`:

```python
@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".")
@click.option("--tag", required=True)
@click.option("--output", default="-")
def report(reqs_dir: str, root: str, tag: str, output: str) -> None:
    """Emit the full release traceability report (markdown)."""
    from pathlib import Path
    from reqtool.trace import build_traceability_matrix
    from reqtool.report import build_report

    matrix = build_traceability_matrix(Path(reqs_dir), Path(root))
    md = build_report(matrix, tag=tag)
    if output == "-":
        click.echo(md)
    else:
        Path(output).write_text(md, encoding="utf-8")
        click.echo(f"wrote {output}", err=True)
```

Extend the `trace` command from Task 12 to support `--diff` (compares against a git ref). Replace the trace command body with:

```python
@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".")
@click.option("--diff/--no-diff", default=False)
@click.option("--base", default="origin/main")
@click.option("--output", default="-")
def trace(reqs_dir: str, root: str, diff: bool, base: str, output: str) -> None:
    """Build traceability matrix linking REQs to code and tests."""
    from pathlib import Path
    import subprocess
    import tempfile
    from reqtool.trace import build_traceability_matrix
    from reqtool.render import render_matrix_markdown
    from reqtool.diff import diff_matrices, matrix_to_refset

    matrix_after = build_traceability_matrix(Path(reqs_dir), Path(root))

    if not diff:
        out = render_matrix_markdown(matrix_after)
    else:
        # git-worktree out the base ref to a temp dir, build matrix there, diff
        with tempfile.TemporaryDirectory() as td:
            subprocess.check_call(["git", "worktree", "add", "--detach", td, base])
            try:
                matrix_before = build_traceability_matrix(
                    Path(td) / reqs_dir, Path(td)
                )
            finally:
                subprocess.check_call(["git", "worktree", "remove", "--force", td])
        d = diff_matrices(matrix_to_refset(matrix_before), matrix_to_refset(matrix_after))
        out = _render_diff_markdown(d, base)

    if output == "-":
        click.echo(out)
    else:
        Path(output).write_text(out, encoding="utf-8")
        click.echo(f"wrote {output}", err=True)


def _render_diff_markdown(d, base: str) -> str:
    lines = [f"# Traceability diff vs `{base}`", ""]
    if d.added:
        lines += ["## New requirements referenced", ""]
        for rid in d.added:
            lines.append(f"- `{rid}`")
        lines.append("")
    if d.removed:
        lines += ["## Requirements no longer referenced", ""]
        for rid in d.removed:
            lines.append(f"- `{rid}`")
        lines.append("")
    if d.changed:
        lines += ["## Reference counts changed", ""]
        for rid, rd in d.changed.items():
            lines.append(f"### {rid}")
            for a in sorted(rd.added):
                lines.append(f"- ✚ `{a}`")
            for r in sorted(rd.removed):
                lines.append(f"- ✖ `{r}`")
            lines.append("")
    if not (d.added or d.removed or d.changed):
        lines.append("_No traceability changes in this PR._")
    return "\n".join(lines) + "\n"
```

- [ ] **Step 7: Commit**

```bash
git add tools/reqtool/
git commit -m "feat(reqtool): report + trace --diff for PR comments"
```

---

### Task 15: Add `reqtool` `README.md` and Makefile shortcuts

**Files:**
- Create: `tools/reqtool/README.md`
- Create: `Makefile` (top-level)

- [ ] **Step 1: Write `tools/reqtool/README.md`**

```markdown
# reqtool

Requirements & traceability CLI for the ocor-mouse monorepo.

## Install

    pip install -e tools/reqtool[dev]

## Commands

    reqtool lint                                     # validate frontmatter
    reqtool trace                                    # markdown matrix to stdout
    reqtool trace --diff --base origin/main          # PR-comment-ready diff
    reqtool orphans                                  # implementation gaps
    reqtool checklist --tag v0.1.0                   # QA issue body
    reqtool report --tag v0.1.0 --output trace.md    # release report

## Tests

    pytest tools/reqtool/tests -v
```

- [ ] **Step 2: Write top-level `Makefile`**

```makefile
.PHONY: help reqtool-install reqtool-test reqtool-lint reqtool-trace web-build web-preview

help:
	@echo "Available targets:"
	@echo "  reqtool-install   - install reqtool into local venv"
	@echo "  reqtool-test      - run reqtool unit tests"
	@echo "  reqtool-lint      - lint docs/requirements/"
	@echo "  reqtool-trace     - print traceability matrix"
	@echo "  web-build         - build web-config/dist/"
	@echo "  web-preview       - preview web-config locally"

reqtool-install:
	pip install -e tools/reqtool[dev]

reqtool-test:
	pytest tools/reqtool/tests -v

reqtool-lint:
	reqtool lint

reqtool-trace:
	reqtool trace

web-build:
	cd web-config && npm ci && npm run build

web-preview:
	cd web-config && npm run preview
```

- [ ] **Step 3: Commit**

```bash
git add tools/reqtool/README.md Makefile
git commit -m "docs(reqtool): README and top-level Makefile shortcuts"
```

---

## Phase 4 — Initial requirements seed

### Task 16: Seed an initial set of requirements

**Files:**
- Create: `docs/requirements/REQ-DPI-001.md` through `REQ-DPI-003.md`
- Create: `docs/requirements/REQ-RADIO-001.md` through `REQ-RADIO-003.md`
- Create: `docs/requirements/REQ-USB-001.md`, `REQ-USB-002.md`
- Create: `docs/requirements/REQ-BUTTON-001.md`
- Create: `docs/requirements/REQ-POWER-001.md`
- Create: `docs/requirements/REQ-DFU-001.md`, `REQ-DFU-002.md`

- [ ] **Step 1: Author initial seed requirements**

These reflect behavior implicit in the existing code. Adjust numeric thresholds (DPI range, polling, battery life) to match what the actual hardware supports — the engineer should sanity-check against `prj_*.conf` defaults.

`docs/requirements/REQ-DPI-001.md`:

```markdown
---
id: REQ-DPI-001
title: DPI adjustable 100-32000 in 50-step increments
status: implemented
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

## Description
The mouse SHALL support DPI values from 100 to 32000, stepped by 50 (i.e. valid values are 100, 150, 200, ..., 32000).

## Acceptance criteria
- Driver-set DPI is applied within 5 ms of receipt over the configured transport (USB or ESB).
- DPI persists across power cycles via the settings subsystem.
- Invalid DPI values (out of range or not divisible by 50) are rejected.

## Notes
Sensor: PAW3395. Driver: `nrf/drivers/sensor/paw3395`.
```

`docs/requirements/REQ-DPI-002.md`:

```markdown
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
```

`docs/requirements/REQ-DPI-003.md`:

```markdown
---
id: REQ-DPI-003
title: Independent X/Y DPI per preset
status: approved
verification: HIL
priority: P1
owner: fw-team
products: [hitscan]
---

## Description
Each DPI preset SHALL allow independent X-axis and Y-axis DPI values.

## Acceptance criteria
- The web configurator accepts asymmetric X/Y values.
- The sensor honors the asymmetric resolution settings.
```

`docs/requirements/REQ-RADIO-001.md`:

```markdown
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
```

`docs/requirements/REQ-RADIO-002.md`:

```markdown
---
id: REQ-RADIO-002
title: Effective 2.4 GHz range > 5 m line-of-sight
status: approved
verification: HIL
priority: P1
owner: fw-team
products: [hitscan]
---

## Description
The mouse SHALL maintain a stable HID connection at distances up to 5 m line-of-sight from the dongle.

## Acceptance criteria
- No dropped reports during a 60-second test at 5 m distance.
```

`docs/requirements/REQ-RADIO-003.md`:

```markdown
---
id: REQ-RADIO-003
title: Polling rate selectable up to 8000 Hz with high-speed dongle
status: approved
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

## Description
With the CH32V305 high-speed dongle, the mouse SHALL support polling rates of 1000, 2000, 4000, and 8000 Hz. With the low-speed nRF52820 dongle, only 1000 Hz is required.

## Acceptance criteria
- USB analyzer confirms requested polling interval matches actual interval within ±5 %.
```

`docs/requirements/REQ-USB-001.md`:

```markdown
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
```

`docs/requirements/REQ-USB-002.md`:

```markdown
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
```

`docs/requirements/REQ-BUTTON-001.md`:

```markdown
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
```

`docs/requirements/REQ-POWER-001.md`:

```markdown
---
id: REQ-POWER-001
title: Battery life at 1000 Hz polling > 70 h
status: approved
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

## Description
With a fully charged battery and 1000 Hz polling over ESB, the mouse SHALL run for at least 70 hours of continuous use.

## Acceptance criteria
- Bench test draws representative usage current and reports run-time > 70 h.
```

`docs/requirements/REQ-DFU-001.md`:

```markdown
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
- Web configurator's DFU flow from "Update" click to "complete" message takes ≤ 30 s.
```

`docs/requirements/REQ-DFU-002.md`:

```markdown
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
```

- [ ] **Step 2: Verify lint passes on the seed**

```bash
reqtool lint
```

Expected: `OK: docs/requirements (lint passed)`.

- [ ] **Step 3: Build and inspect the traceability matrix**

```bash
reqtool trace
```

Expected: 12-row table, all `— refs` (because no code has been tagged yet — that happens in Task 17).

- [ ] **Step 4: Commit**

```bash
git add docs/requirements/
git commit -m "docs(req): seed initial 12 requirements (DPI, RADIO, USB, BUTTON, POWER, DFU)"
```

---

### Task 17: Tag existing source code with REQ-IDs

**Files:**
- Modify: selected files under `applications/gaming_mouse/src/`

- [ ] **Step 1: Identify the existing code modules that implement each seeded REQ**

For each REQ in Task 16, locate the file(s) that implement it and add a comment with the REQ-ID. Example locations (the engineer should verify by grepping the actual code — these are starting hints):

| REQ-ID | Likely file (verify) |
|---|---|
| REQ-DPI-001 | `applications/gaming_mouse/src/modules/motion_sensor.c` |
| REQ-DPI-002 | `applications/gaming_mouse/src/modules/dpi_state.c` (or equivalent) |
| REQ-RADIO-001 | `applications/gaming_mouse/src/modules/esb_*.c` |
| REQ-USB-001 | `applications/gaming_mouse/src/hw_interface/usb_state.c` |
| REQ-BUTTON-001 | `applications/gaming_mouse/src/hw_interface/buttons*.c` |
| REQ-POWER-001 | `applications/gaming_mouse/src/modules/battery_*.c` |
| REQ-DFU-001 / 002 | Look for `dfu`, `mcumgr`, or `smp` in module/Kconfig names |

For each, insert a comment near the relevant code:

```c
/* REQ-DPI-001: enforce 50-step DPI alignment */
if (new_dpi % 50 != 0) {
    return -EINVAL;
}
```

- [ ] **Step 2: Run `reqtool trace`**

```bash
reqtool trace
```

Expected: table now shows ≥ 1 reference for each tagged REQ.

- [ ] **Step 3: Run `reqtool orphans`**

```bash
reqtool orphans
```

Expected: lists any REQ with `status: implemented` and zero refs (i.e., REQs you couldn't find an implementation for). Update the REQ's `status:` to `approved` (intent only, not implemented yet) OR find the actual implementation file.

- [ ] **Step 4: Commit**

```bash
git add applications/gaming_mouse/src/
git add docs/requirements/    # status changes from Step 3
git commit -m "docs(traceability): tag existing modules with REQ-IDs"
```

---

## Phase 5 — CI pipelines

### Task 18: Composite action `setup-ncs`

**Files:**
- Create: `.github/actions/setup-ncs/action.yml`

- [ ] **Step 1: Write the composite action**

```yaml
name: Set up nRF Connect SDK
description: Installs the NCS toolchain, runs west init/update with caching.

runs:
  using: composite
  steps:
    - name: Cache west modules
      id: west-cache
      uses: actions/cache@v4
      with:
        path: |
          .west/
          bootloader/
          modules/
          nrf/
          nrfxlib/
          test/
          tools/
          zephyr/
        key: west-ncs-v3.0.2-${{ runner.os }}-${{ hashFiles('west.yml') }}

    - name: Set up Python
      uses: actions/setup-python@v5
      with:
        python-version: "3.11"

    - name: Install west
      shell: bash
      run: pip install west

    - name: Install Zephyr build deps
      shell: bash
      run: |
        pip install -r https://raw.githubusercontent.com/nrfconnect/sdk-zephyr/main/scripts/requirements-base.txt
        pip install pyelftools

    - name: West init + update
      if: steps.west-cache.outputs.cache-hit != 'true'
      shell: bash
      run: |
        west init -l .
        west update --narrow -o=--depth=1
        west zephyr-export

    - name: Use NCS Docker toolchain (arm-zephyr-eabi)
      uses: nrfconnect/action-build-toolchain@v1
      with:
        toolchain: "arm-zephyr-eabi"
```

(Note: `nrfconnect/action-build-toolchain` is illustrative; if Nordic publishes a different recommended action, swap to it. The alternative is to install the Zephyr SDK directly via `https://github.com/zephyrproject-rtos/sdk-ng/releases`.)

- [ ] **Step 2: Commit**

```bash
git add .github/actions/setup-ncs/
git commit -m "ci(actions): composite action setup-ncs"
```

---

### Task 19: Composite action `setup-ch32` (container image)

**Files:**
- Create: `.github/actions/setup-ch32/action.yml`
- Create: `.github/actions/setup-ch32/Dockerfile`

- [ ] **Step 1: Write the Dockerfile**

`.github/actions/setup-ch32/Dockerfile`:

```dockerfile
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        xz-utils \
        make \
        python3 \
        python3-pip \
        git \
    && rm -rf /var/lib/apt/lists/*

# xPack RISC-V Embedded GCC (works for CH32V; alternative is WCH's own riscv-none-elf-gcc fork)
ARG TOOLCHAIN_VERSION=14.2.0-3
RUN curl -L -o /tmp/riscv-toolchain.tar.gz \
        "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v${TOOLCHAIN_VERSION}/xpack-riscv-none-elf-gcc-${TOOLCHAIN_VERSION}-linux-x64.tar.gz" \
    && mkdir -p /opt/riscv-toolchain \
    && tar -xzf /tmp/riscv-toolchain.tar.gz -C /opt/riscv-toolchain --strip-components=1 \
    && rm /tmp/riscv-toolchain.tar.gz

ENV PATH=/opt/riscv-toolchain/bin:$PATH

# sanity check the prefix the Makefile uses
RUN riscv-none-elf-gcc --version
```

- [ ] **Step 2: Write the composite action**

`.github/actions/setup-ch32/action.yml`:

```yaml
name: Set up CH32 RISC-V toolchain
description: Builds and runs the WCH RISC-V build in a pinned container.

inputs:
  build-args:
    description: "Extra args for `make`."
    default: ""

runs:
  using: composite
  steps:
    - name: Build inside CH32 container
      shell: bash
      run: |
        docker build -t ch32-toolchain:local .github/actions/setup-ch32
        docker run --rm -v "$PWD:/work" -w /work/dongle-hs-ch32v305 \
            ch32-toolchain:local \
            make ${{ inputs.build-args }}
```

- [ ] **Step 3: Commit**

```bash
git add .github/actions/setup-ch32/
git commit -m "ci(actions): composite action setup-ch32 with toolchain Dockerfile"
```

---

### Task 20: PR workflow — build matrix

**Files:**
- Create: `.github/workflows/pr.yml`

- [ ] **Step 1: Write `pr.yml`**

```yaml
name: pr

on:
  pull_request:
    branches: [main]

permissions:
  contents: read
  pull-requests: write

concurrency:
  group: pr-${{ github.ref }}
  cancel-in-progress: true

jobs:
  reqtool:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0

      - uses: actions/setup-python@v5
        with:
          python-version: "3.11"

      - name: Install reqtool
        run: pip install -e tools/reqtool

      - name: Lint requirements
        run: reqtool lint

      - name: Traceability diff vs base
        run: |
          git fetch origin ${{ github.base_ref }}
          reqtool trace --diff --base origin/${{ github.base_ref }} \
                       --output /tmp/trace-diff.md

      - name: Post PR comment
        uses: marocchino/sticky-pull-request-comment@v2
        with:
          path: /tmp/trace-diff.md

  build-ncs:
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
      matrix:
        include:
          - target: mouse-debug
            board: hitscan_nrf52840
            conf: prj_esb.conf
          - target: dongle-ls-debug
            board: hitscan52820_nrf52820
            conf: prj.conf
          - target: dev-dk
            board: nrf52833dk_nrf52833
            conf: prj.conf
    steps:
      - uses: actions/checkout@v4
      - uses: ./.github/actions/setup-ncs
      - name: Build ${{ matrix.target }}
        run: |
          west build -p auto -b ${{ matrix.board }} applications/gaming_mouse \
              --sysbuild --build-dir build/${{ matrix.target }} \
              -- -DCONF_FILE=${{ matrix.conf }}
      - name: Upload artifacts
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: ${{ matrix.target }}
          path: build/${{ matrix.target }}/zephyr/zephyr.{hex,bin,map}
          retention-days: 7

  build-ch32:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: ./.github/actions/setup-ch32
      - uses: actions/upload-artifact@v4
        with:
          name: dongle-hs-debug
          path: dongle-hs-ch32v305/build/*.bin
          retention-days: 7

  build-web:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: "20"
      - run: npm ci
        working-directory: web-config
      - run: npm run build
        working-directory: web-config
      - uses: actions/upload-artifact@v4
        with:
          name: web-config
          path: web-config/dist/
          retention-days: 7
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/pr.yml
git commit -m "ci(pr): build matrix for mouse, dongle-ls, dongle-hs, web-config + reqtool"
```

---

### Task 21: Add C-side linting (clang-format) and verify locally

**Files:**
- Create: `.clang-format`
- Modify: `.github/workflows/pr.yml`

- [ ] **Step 1: Write `.clang-format`**

```yaml
# Based on Zephyr's style (kernel-like)
BasedOnStyle: LLVM
IndentWidth: 8
UseTab: Always
TabWidth: 8
BreakBeforeBraces: Linux
AllowShortIfStatementsOnASingleLine: false
AllowShortLoopsOnASingleLine: false
ColumnLimit: 100
SortIncludes: false
```

- [ ] **Step 2: Add the lint job to `pr.yml`**

Insert this job before `build-ncs`:

```yaml
  clang-format:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0
      - name: Install clang-format
        run: sudo apt-get update && sudo apt-get install -y clang-format
      - name: Check changed C/H files
        run: |
          git fetch origin ${{ github.base_ref }}
          CHANGED=$(git diff --name-only origin/${{ github.base_ref }}...HEAD -- '*.c' '*.h')
          if [ -z "$CHANGED" ]; then
            echo "No C/H changes."
            exit 0
          fi
          echo "$CHANGED" | xargs clang-format --dry-run --Werror
```

- [ ] **Step 3: Commit**

```bash
git add .clang-format .github/workflows/pr.yml
git commit -m "ci(pr): clang-format dry-run on changed C/H files"
```

---

### Task 22: Native-sim ztests for shared protocol code

**Files:**
- Create: `shared/include/protocol_version.h`
- Create: `shared/src/protocol.c` (placeholder pure-C implementation)
- Create: `tests/native_sim/protocol/CMakeLists.txt`
- Create: `tests/native_sim/protocol/prj.conf`
- Create: `tests/native_sim/protocol/src/main.c`
- Modify: `.github/workflows/pr.yml`

- [ ] **Step 1: Write `shared/include/protocol_version.h`**

```c
#ifndef OCOR_PROTOCOL_VERSION_H
#define OCOR_PROTOCOL_VERSION_H

/* REQ-USB-002: increments on breaking changes to the vendor HID protocol */
#define OCOR_PROTOCOL_VERSION 3

#endif /* OCOR_PROTOCOL_VERSION_H */
```

- [ ] **Step 2: Write `shared/src/protocol.c`**

This is a deliberately minimal placeholder so we have something to test against. Real protocol code arrives in follow-on work.

```c
/* REQ-USB-002: vendor HID protocol — encode/decode helpers */
#include <stddef.h>
#include <stdint.h>
#include "protocol_version.h"

uint8_t ocor_protocol_version(void) {
    return OCOR_PROTOCOL_VERSION;
}

/* REQ-DPI-001: validate DPI value before applying */
int ocor_dpi_is_valid(uint16_t dpi) {
    if (dpi < 100 || dpi > 32000) return 0;
    if (dpi % 50 != 0) return 0;
    return 1;
}
```

- [ ] **Step 3: Write `tests/native_sim/protocol/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(protocol_tests)

target_sources(app PRIVATE
    src/main.c
    ../../../shared/src/protocol.c
)

target_include_directories(app PRIVATE ../../../shared/include)
```

- [ ] **Step 4: Write `tests/native_sim/protocol/prj.conf`**

```
CONFIG_ZTEST=y
```

- [ ] **Step 5: Write `tests/native_sim/protocol/src/main.c`**

```c
#include <zephyr/ztest.h>
#include "protocol_version.h"

extern int ocor_dpi_is_valid(unsigned short dpi);
extern unsigned char ocor_protocol_version(void);

ZTEST_SUITE(protocol, NULL, NULL, NULL, NULL, NULL);

ZTEST(protocol, test_protocol_version_nonzero)
{
    zassert_true(ocor_protocol_version() > 0, "protocol version should be > 0");
}

/* REQ-DPI-001 */
ZTEST(protocol, test_dpi_valid_within_range)
{
    zassert_true(ocor_dpi_is_valid(800), "800 should be valid");
    zassert_true(ocor_dpi_is_valid(1600), "1600 should be valid");
    zassert_true(ocor_dpi_is_valid(32000), "32000 (max) should be valid");
    zassert_true(ocor_dpi_is_valid(100), "100 (min) should be valid");
}

/* REQ-DPI-001 */
ZTEST(protocol, test_dpi_invalid_step)
{
    zassert_false(ocor_dpi_is_valid(123), "123 not aligned to 50");
}

/* REQ-DPI-001 */
ZTEST(protocol, test_dpi_out_of_range)
{
    zassert_false(ocor_dpi_is_valid(50), "below min");
    zassert_false(ocor_dpi_is_valid(40000), "above max");
}
```

- [ ] **Step 6: Build and run the test locally**

```bash
west build -p auto -b native_sim tests/native_sim/protocol --build-dir build/native_sim
build/native_sim/zephyr/zephyr.exe
```

Expected: ztest output shows 4 passed.

- [ ] **Step 7: Add `native-sim` job to `pr.yml`**

Append to `pr.yml`:

```yaml
  native-sim:
    runs-on: ubuntu-latest
    needs: build-ncs
    steps:
      - uses: actions/checkout@v4
      - uses: ./.github/actions/setup-ncs
      - name: Run native_sim ztests
        run: |
          west build -p auto -b native_sim tests/native_sim/protocol \
              --build-dir build/native_sim
          build/native_sim/zephyr/zephyr.exe
```

- [ ] **Step 8: Commit**

```bash
git add shared/ tests/native_sim/ .github/workflows/pr.yml
git commit -m "test(native_sim): protocol tests for REQ-DPI-001 and protocol version"
```

---

### Task 23: Nightly workflow

**Files:**
- Create: `.github/workflows/nightly.yml`

- [ ] **Step 1: Write `nightly.yml`**

```yaml
name: nightly

on:
  schedule:
    - cron: "0 2 * * *"
  workflow_dispatch:

permissions:
  contents: read
  issues: write

jobs:
  build-release-configs:
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
      matrix:
        include:
          - target: mouse-release
            board: hitscan_nrf52840
            conf: prj_esb_release.conf
          - target: dongle-ls-release
            board: hitscan52820_nrf52820
            conf: prj_release.conf
    steps:
      - uses: actions/checkout@v4
      - uses: ./.github/actions/setup-ncs
      - name: Build ${{ matrix.target }}
        run: |
          west build -p auto -b ${{ matrix.board }} applications/gaming_mouse \
              --sysbuild --build-dir build/${{ matrix.target }} \
              -- -DCONF_FILE=${{ matrix.conf }}
      - uses: actions/upload-artifact@v4
        with:
          name: ${{ matrix.target }}
          path: build/${{ matrix.target }}/zephyr/zephyr.{hex,bin,map}
          retention-days: 30

  build-ch32-release:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: ./.github/actions/setup-ch32
        with:
          build-args: release
      - uses: actions/upload-artifact@v4
        with:
          name: dongle-hs-release
          path: dongle-hs-ch32v305/build/*.bin
          retention-days: 30

  orphans-report:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.11"
      - run: pip install -e tools/reqtool
      - name: Run orphans
        id: orph
        run: |
          reqtool orphans > /tmp/orphans.txt
          {
            echo 'body<<EOF'
            cat /tmp/orphans.txt
            echo EOF
          } >> "$GITHUB_OUTPUT"

      - name: Manage orphans issue
        uses: peter-evans/create-or-update-issue@v3
        with:
          title: "Requirements orphans report"
          body: ${{ steps.orph.outputs.body }}
          state: ${{ contains(steps.orph.outputs.body, 'OK: no orphans') && 'closed' || 'open' }}
```

(Note: `peter-evans/create-or-update-issue` is an example; if it's not available, substitute with a `gh issue` script using `gh-cli`.)

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/nightly.yml
git commit -m "ci(nightly): release-config builds + orphans issue"
```

---

### Task 24: Verify PR pipeline against a noop PR

**Files:**
- (No source changes; this task verifies CI on an empty PR.)

- [ ] **Step 1: Push `main` to the remote**

(This assumes the GitHub repo has been created. If not, create it first via `gh repo create ocor-mouse --private --source=. --push`.)

```bash
git push -u origin main
```

- [ ] **Step 2: Open a noop PR**

```bash
git checkout -b ci-smoke-test
echo "# ci smoke" > docs/architecture/ci-smoke.md
git add docs/architecture/ci-smoke.md
git commit -m "test(ci): smoke test"
git push -u origin ci-smoke-test
gh pr create --base main --title "test(ci): smoke" --body "Verify PR pipeline."
```

- [ ] **Step 3: Watch the run**

```bash
gh pr checks --watch
```

Expected: all jobs green (`reqtool`, `clang-format`, `build-ncs[mouse-debug]`, `build-ncs[dongle-ls-debug]`, `build-ncs[dev-dk]`, `build-ch32`, `build-web`, `native-sim`).

The reqtool job should leave a sticky PR comment with the (empty) traceability diff.

- [ ] **Step 4: Close the smoke PR without merging**

```bash
gh pr close --delete-branch
```

---

## Phase 6 — Release pipeline

### Task 25: MCUboot dev keys

**Files:**
- Create: `keys/README.md`
- Create: `keys/mcuboot.dev.pem`
- Create: `keys/mcuboot.dev.pub.pem`

- [ ] **Step 1: Generate a dev key pair**

```bash
mkdir -p keys
imgtool keygen -k keys/mcuboot.dev.pem -t ecdsa-p256
openssl ec -in keys/mcuboot.dev.pem -pubout -out keys/mcuboot.dev.pub.pem
```

(If `imgtool` isn't installed: `pip install imgtool`.)

- [ ] **Step 2: Write `keys/README.md`**

```markdown
# Signing keys

The keys in this directory are **development keys, checked into git**. They provide image-integrity validation, NOT security. Anyone with this repo can sign images.

When you move to production:
1. Generate a new key in a hardware-backed KMS (AWS KMS / GCP Cloud KMS / YubiHSM).
2. Replace the `signature_key_file` reference in the MCUboot Kconfig fragment with the KMS-OIDC-fetched key in CI.
3. Rotate device public keys via the next signed DFU image. Pre-production devices using this dev key cannot be field-upgraded to the new key chain (intentional — they're not production).

Until then, treat any release as INTEGRITY-CHECKED but NOT AUTHENTICATED.
```

- [ ] **Step 3: Configure MCUboot to use the dev key**

Create `applications/gaming_mouse/configuration/hitscan_nrf52840/sysbuild/mcuboot.conf`:

```
CONFIG_BOOT_SIGNATURE_KEY_FILE="../../../../keys/mcuboot.dev.pem"
```

Repeat for `hitscan52820_nrf52820/sysbuild/mcuboot.conf`.

(The exact sysbuild override directory may differ — check the existing `sysbuild_*.conf` files in each board config for the right place to point at the key.)

- [ ] **Step 4: Commit**

```bash
git add keys/ applications/gaming_mouse/configuration/*/sysbuild/
git commit -m "feat(signing): MCUboot dev keys (integrity only — see keys/README.md)"
```

---

### Task 26: Release workflow — build + hash

**Files:**
- Create: `.github/workflows/release.yml`
- Create: `tools/manifest.py`

- [ ] **Step 1: Write `tools/manifest.py`**

```python
#!/usr/bin/env python3
"""Produce a release manifest.json from a directory of artifacts."""
import argparse
import hashlib
import json
import sys
from pathlib import Path
from datetime import datetime, timezone


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", required=True, help="Release tag, e.g. v1.0.0")
    ap.add_argument("--git-sha", required=True)
    ap.add_argument("--protocol-version", required=True, type=int)
    ap.add_argument("--artifacts-dir", required=True)
    ap.add_argument("--output", required=True)
    args = ap.parse_args()

    art_dir = Path(args.artifacts_dir)
    artifacts: dict[str, dict] = {}
    for f in sorted(art_dir.rglob("*")):
        if not f.is_file():
            continue
        rel = f.relative_to(art_dir).as_posix()
        artifacts[rel] = {
            "sha256": sha256(f),
            "size": f.stat().st_size,
            "signed_with": "dev-key" if rel.endswith("-update.bin") else None,
        }

    manifest = {
        "release": args.release,
        "git_sha": args.git_sha,
        "build_time": datetime.now(timezone.utc).isoformat(),
        "protocol_version": args.protocol_version,
        "artifacts": artifacts,
    }

    Path(args.output).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {args.output}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Write `.github/workflows/release.yml` (skeleton, no signing/upload yet)**

```yaml
name: release

on:
  push:
    tags:
      - "v*.*.*"

permissions:
  contents: write
  pages: write
  id-token: write
  issues: write

jobs:
  build:
    runs-on: ubuntu-latest
    outputs:
      version: ${{ steps.ver.outputs.version }}
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0
      - id: ver
        run: echo "version=${GITHUB_REF#refs/tags/}" >> "$GITHUB_OUTPUT"

      - uses: ./.github/actions/setup-ncs
      - name: Build mouse release
        run: |
          west build -p auto -b hitscan_nrf52840 applications/gaming_mouse \
              --sysbuild --build-dir build/mouse \
              -- -DCONF_FILE=prj_esb_release.conf

      - name: Build dongle-ls release
        run: |
          west build -p auto -b hitscan52820_nrf52820 applications/gaming_mouse \
              --sysbuild --build-dir build/dongle-ls \
              -- -DCONF_FILE=prj_release.conf

      - uses: ./.github/actions/setup-ch32
        with:
          build-args: release

      - name: Collect artifacts
        run: |
          mkdir -p artifacts
          cp build/mouse/gaming_mouse/zephyr/zephyr.hex artifacts/mouse-fw.hex
          cp build/mouse/gaming_mouse/zephyr/zephyr.signed.bin artifacts/mouse-fw-update.bin
          cp build/dongle-ls/gaming_mouse/zephyr/zephyr.hex artifacts/dongle-ls-fw.hex
          cp build/dongle-ls/gaming_mouse/zephyr/zephyr.signed.bin artifacts/dongle-ls-fw-update.bin
          cp dongle-hs-ch32v305/build/dongle-hs.bin artifacts/dongle-hs-fw.bin

      - uses: actions/setup-python@v5
        with:
          python-version: "3.11"
      - run: pip install -e tools/reqtool

      - name: Read protocol version
        id: proto
        run: |
          PROTO=$(grep -oP 'OCOR_PROTOCOL_VERSION\s+\K\d+' shared/include/protocol_version.h)
          echo "version=$PROTO" >> "$GITHUB_OUTPUT"

      - name: Generate manifest
        run: |
          python tools/manifest.py \
              --release "${{ steps.ver.outputs.version }}" \
              --git-sha "${{ github.sha }}" \
              --protocol-version "${{ steps.proto.outputs.version }}" \
              --artifacts-dir artifacts \
              --output artifacts/manifest.json

      - name: Traceability report
        run: |
          reqtool report --tag "${{ steps.ver.outputs.version }}" \
                        --output artifacts/traceability-matrix.md

      - name: Changelog
        uses: orhun/git-cliff-action@v3
        with:
          config: cliff.toml
          args: --latest --strip header --output artifacts/CHANGELOG.md

      - name: Bundle zip
        run: |
          cd artifacts
          zip -r "../ocor-mouse-${{ steps.ver.outputs.version }}.zip" .

      - uses: actions/upload-artifact@v4
        with:
          name: release-bundle
          path: |
            artifacts/
            ocor-mouse-*.zip
```

(Subsequent tasks add GitHub Release creation, GitHub Pages deploy, and QA issue creation.)

- [ ] **Step 3: Add a minimal `cliff.toml` for `git-cliff`**

`cliff.toml`:

```toml
[changelog]
header = ""
body = """
{% if version %}## {{ version }} — {{ timestamp | date(format="%Y-%m-%d") }}{% endif %}
{% for group, commits in commits | group_by(attribute="group") %}
### {{ group | upper_first }}
{% for commit in commits %}
- {{ commit.message | upper_first }} ({{ commit.id | truncate(length=7, end="") }})
{% endfor %}
{% endfor %}
"""
trim = true

[git]
conventional_commits = true
filter_unconventional = true
commit_parsers = [
    { message = "^feat", group = "Features" },
    { message = "^fix", group = "Fixes" },
    { message = "^docs", group = "Docs" },
    { message = "^perf", group = "Performance" },
    { message = "^refactor", group = "Refactoring" },
    { message = "^test", group = "Tests" },
    { message = "^ci", group = "CI" },
    { message = "^chore", group = "Chores" },
]
filter_commits = false
tag_pattern = "v[0-9]*"
```

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/release.yml tools/manifest.py cliff.toml
git commit -m "feat(release): build + hash + manifest + changelog skeleton"
```

---

### Task 27: Release — GitHub Pages deploy for web-config

**Files:**
- Modify: `.github/workflows/release.yml`

- [ ] **Step 1: Add the deploy job**

Append to `release.yml`:

```yaml
  deploy-pages:
    needs: build
    runs-on: ubuntu-latest
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: "20"
      - run: npm ci
        working-directory: web-config
      - run: npm run build
        working-directory: web-config
      - uses: actions/upload-pages-artifact@v3
        with:
          path: web-config/dist
      - id: deployment
        uses: actions/deploy-pages@v4
```

- [ ] **Step 2: Enable GitHub Pages in the repo**

```bash
gh api -X POST "repos/$(gh repo view --json nameWithOwner -q .nameWithOwner)/pages" \
  -f source[branch]=gh-pages \
  -f source[path]=/ 2>/dev/null || echo "Pages may need manual setup in repo settings"
```

If the API call fails, enable via repo Settings → Pages → "GitHub Actions" source.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "feat(release): deploy web-config to GitHub Pages"
```

---

### Task 28: Release — draft GitHub Release with artifact upload

**Files:**
- Modify: `.github/workflows/release.yml`

- [ ] **Step 1: Add the `publish-draft` job**

Append to `release.yml`:

```yaml
  publish-draft:
    needs: build
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/download-artifact@v4
        with:
          name: release-bundle
          path: release-bundle/

      - name: Create draft release
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: |
          VERSION="${{ needs.build.outputs.version }}"
          gh release create "$VERSION" \
            --draft \
            --title "$VERSION" \
            --notes-file release-bundle/artifacts/CHANGELOG.md \
            release-bundle/ocor-mouse-*.zip \
            release-bundle/artifacts/mouse-fw.hex \
            release-bundle/artifacts/mouse-fw-update.bin \
            release-bundle/artifacts/dongle-ls-fw.hex \
            release-bundle/artifacts/dongle-ls-fw-update.bin \
            release-bundle/artifacts/dongle-hs-fw.bin \
            release-bundle/artifacts/manifest.json \
            release-bundle/artifacts/traceability-matrix.md
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "feat(release): create draft GitHub Release with full artifact bundle"
```

---

### Task 29: Release — open the QA tracking issue

**Files:**
- Modify: `.github/workflows/release.yml`

- [ ] **Step 1: Add the `qa-issue` job**

Append to `release.yml`:

```yaml
  qa-issue:
    needs: build
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.11"
      - run: pip install -e tools/reqtool

      - name: Generate checklist
        run: |
          reqtool checklist \
              --tag "${{ needs.build.outputs.version }}" \
              --products hitscan \
              --output /tmp/checklist.md

      - name: Open release QA issue
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: |
          VERSION="${{ needs.build.outputs.version }}"
          gh issue create \
              --title "Release QA: $VERSION" \
              --body-file /tmp/checklist.md \
              --label "release-qa"
```

- [ ] **Step 2: Ensure the `release-qa` label exists**

Add a one-shot bootstrap step (run once locally; documented in README):

```bash
gh label create release-qa --color FFD700 \
   --description "Release QA tracking issue" || true
```

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "feat(release): open Release QA tracking issue with HIL checklist"
```

---

### Task 30: `promote-release.yml` — flip draft to published

**Files:**
- Create: `.github/workflows/promote-release.yml`

- [ ] **Step 1: Write the workflow**

```yaml
name: promote-release

on:
  issues:
    types: [closed]

permissions:
  contents: write
  issues: write

jobs:
  promote:
    if: |
      startsWith(github.event.issue.title, 'Release QA: v') &&
      contains(join(github.event.issue.labels.*.name), 'release-qa')
    runs-on: ubuntu-latest
    steps:
      - name: Verify all checkboxes ticked
        id: check
        env:
          BODY: ${{ github.event.issue.body }}
        run: |
          UNCHECKED=$(printf '%s\n' "$BODY" | grep -c '^- \[ \]' || true)
          echo "unchecked=$UNCHECKED" >> "$GITHUB_OUTPUT"

      - name: Reopen with reminder if unchecked
        if: steps.check.outputs.unchecked != '0'
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: |
          NUM="${{ github.event.issue.number }}"
          gh issue reopen "$NUM"
          gh issue comment "$NUM" --body "Cannot promote release: ${{ steps.check.outputs.unchecked }} checkbox(es) still unchecked."
          exit 1

      - name: Extract version from issue title
        if: steps.check.outputs.unchecked == '0'
        id: ver
        env:
          TITLE: ${{ github.event.issue.title }}
        run: |
          VER=$(echo "$TITLE" | sed -n 's/^Release QA: \(v[0-9.]*\).*/\1/p')
          echo "version=$VER" >> "$GITHUB_OUTPUT"

      - name: Promote draft release
        if: steps.check.outputs.unchecked == '0'
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: |
          gh release edit "${{ steps.ver.outputs.version }}" --draft=false
          gh issue comment "${{ github.event.issue.number }}" \
              --body "Release ${{ steps.ver.outputs.version }} promoted from draft to published."
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/promote-release.yml
git commit -m "feat(release): promote draft to published on QA issue close"
```

---

### Task 31: Wire `release.yml` jobs together with `needs`

**Files:**
- Modify: `.github/workflows/release.yml`

- [ ] **Step 1: Verify all jobs depend on `build`**

Check that `publish-draft`, `deploy-pages`, and `qa-issue` all have `needs: build`. If any job is missing the dependency or the outputs reference, fix it.

- [ ] **Step 2: Add an overall summary job**

Append:

```yaml
  summary:
    needs: [build, publish-draft, deploy-pages, qa-issue]
    runs-on: ubuntu-latest
    steps:
      - run: |
          echo "Release ${{ needs.build.outputs.version }} built, draft published, web deployed, QA issue opened."
          echo "Next: QA closes the issue with all boxes ticked → promote-release.yml flips draft to published."
```

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci(release): wire job dependencies + summary"
```

---

## Phase 7 — QA gate + dry-run release

### Task 32: Author a starter `docs/QA/checklist-template.md`

**Files:**
- Create: `docs/QA/checklist-template.md`

- [ ] **Step 1: Document the QA workflow for new team members**

`docs/QA/checklist-template.md`:

```markdown
# QA checklist template

The per-release QA checklist is auto-generated by `reqtool checklist --tag <version>` from `docs/requirements/*.md` and posted as a GitHub issue when `release.yml` runs.

This file is **a reference** for how the auto-generated checklist looks, not a manually maintained checklist.

## Sample output (v0.1.0)

\`\`\`markdown
# Release QA: v0.1.0

Auto-generated by `reqtool checklist --tag v0.1.0`. Tick boxes after manual
verification on physical hardware. Close this issue (with all boxes ticked)
to promote the GitHub Release from draft to published.

## HIL verification

- [ ] REQ-DPI-001: DPI adjustable 100-32000 in 50-step increments
- [ ] REQ-DPI-002: At least four user-selectable DPI presets
- [ ] REQ-RADIO-001: 2.4 GHz ESB pairing between mouse and dongle
- ...

## Sign-off

- [ ] QA lead approval
- [ ] FW lead approval
\`\`\`

## QA process

1. Wait for `release.yml` to finish — it auto-opens the Release QA issue.
2. Flash the artifacts from the GitHub Release onto physical hardware:
   - `mouse-fw.hex` → mouse PCB (via Segger J-Link)
   - `dongle-ls-fw.hex` → nRF52820 dongle
   - `dongle-hs-fw.bin` → CH32V305 dongle (via WCH-Link)
3. Execute each HIL test on the issue. Tick the box only when the test passes on real hardware.
4. If a test fails, open a bug issue linking back to the failed REQ-ID. The bug becomes a blocker.
5. When all boxes are ticked and the bug list is empty, both leads tick the sign-off boxes and **close** the issue.
6. `promote-release.yml` automatically flips the draft Release to published.
```

- [ ] **Step 2: Commit**

```bash
git add docs/QA/checklist-template.md
git commit -m "docs(qa): document the release QA workflow"
```

---

### Task 33: Tag `v0.1.0` and run a dry-run release

**Files:**
- (No source changes; this task exercises the release pipeline end-to-end.)

- [ ] **Step 1: Ensure CI is green on main**

```bash
git checkout main
git pull
gh run list --workflow pr.yml --limit 1
```

Expected: latest run is green (from the smoke PR in Task 24 or subsequent work).

- [ ] **Step 2: Tag and push**

```bash
git tag -a v0.1.0 -m "v0.1.0 — initial workflow validation"
git push origin v0.1.0
```

- [ ] **Step 3: Watch the release run**

```bash
gh run watch
```

Expected: `release.yml` completes successfully. Jobs:
- `build`: produces all artifacts + manifest + traceability matrix + changelog.
- `publish-draft`: creates a draft GitHub Release.
- `deploy-pages`: deploys web-config.
- `qa-issue`: opens "Release QA: v0.1.0" issue with HIL checklist.
- `summary`: prints status.

- [ ] **Step 4: Inspect outputs**

```bash
gh release view v0.1.0
gh issue list --label release-qa --state open
```

Expected: a draft release with all five artifact files + zip + manifest.json + traceability-matrix.md + CHANGELOG.md. An open issue titled "Release QA: v0.1.0" with all HIL REQs listed.

- [ ] **Step 5: Tick all boxes and close the issue (dry-run promotion)**

In the GitHub UI, edit the issue, tick all checkboxes (HIL + sign-offs), then close.

- [ ] **Step 6: Verify `promote-release.yml` flipped the release**

```bash
gh release view v0.1.0 --json isDraft
```

Expected: `"isDraft": false`.

- [ ] **Step 7: Document the v0.1.0 outcome**

If anything failed, fix it, delete the tag, retry:

```bash
git push --delete origin v0.1.0
git tag -d v0.1.0
# ...fix...
git tag -a v0.1.0 -m "v0.1.0 — initial workflow validation"
git push origin v0.1.0
```

Continue iterating until the dry-run completes cleanly.

---

### Task 34: Final commit and workflow handoff

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update top-level README with the now-complete workflow**

Replace `README.md`:

```markdown
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

## Building locally

    make reqtool-install
    west init -l . && west update --narrow -o=--depth=1
    west build -p auto -b hitscan_nrf52840 applications/gaming_mouse \
        --sysbuild -- -DCONF_FILE=prj_esb.conf

For the CH32 dongle, `make` inside `dongle-hs-ch32v305/` (requires `riscv-none-elf-gcc`).

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
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: README reflecting completed workflow"
git push
```

The end-to-end workflow is now operational. Future feature work follows the loop documented in §5 of the spec.

---

## Self-review notes

After writing this plan, the following spec coverage was verified:

- **§3 (Repo layout):** Tasks 1, 4–8 build the directory tree.
- **§4 (Requirements format):** Tasks 10, 16 implement and exercise the schema.
- **§5 (reqtool):** Tasks 9–15 cover all five subcommands.
- **§6.1 (pr.yml):** Tasks 20, 21, 22.
- **§6.2 (nightly.yml):** Task 23.
- **§6.3 (release.yml):** Tasks 25–28, 31.
- **§6.4 (promote-release.yml):** Task 30.
- **§7 (QA workflow):** Tasks 29, 32, 33.
- **§8 (DFU strategy):** MCUboot dev keys in Task 25; the in-field DFU client code is **deferred** (spec §11 mentions "DFU flow per device is owned by `web-config/src/dfu/`" — that's follow-on work, not in this initial workflow plan).
- **§9 (nRF54 forward path):** No tasks — this is forward-looking guidance, not present-day work.
- **§10 (Migration plan):** This entire plan IS the migration.

**Deferred / open items the engineer should be aware of:**

- DFU client code in the web configurator (web-config/src/dfu/) — separate plan needed.
- KMS-backed production signing — separate plan when ready.
- Multi-SKU variant infrastructure — separate plan when needed.
- Native host-tool — separate plan if/when WebHID is insufficient.

**Known fragile spots:**

- Task 6 (CH32 Makefile) is a real toolchain swap from MounRiver Studio; the exact `CFLAGS` and source layout will need iteration until the first clean build.
- Task 5 (build the migrated app) may surface in-tree path assumptions that need fixing case-by-case.
- Task 18's `nrfconnect/action-build-toolchain` reference may need to be replaced with a working alternative if Nordic's action namespace changes.
