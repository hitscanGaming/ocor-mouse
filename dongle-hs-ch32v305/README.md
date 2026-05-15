# dongle-hs-ch32v305

High-speed USB dongle firmware for the hitscan mouse. WCH CH32V305GBU6 (RISC-V).

## Build

Inside the CH32 toolchain container (see `.github/actions/setup-ch32/`):

    make           # debug
    make release   # adds -DNDEBUG

Outputs land in `build/dongle-hs.hex` and `build/dongle-hs.bin`.

## Flash (WCH-Link)

    openocd -f openocd.cfg -c "program build/dongle-hs.hex verify reset exit"

## Notes

- This source tree was migrated from a MounRiver Studio project (`.wvproj`/`.launch`).
  The `Makefile` here replaces the IDE-driven build for CI reproducibility.
- If the WCH HAL drops new files, append to the `C_SRCS` wildcard list — the Makefile picks them up automatically.
- Helper scripts (USB descriptor patching, debug encoding) live under `scripts/`.
