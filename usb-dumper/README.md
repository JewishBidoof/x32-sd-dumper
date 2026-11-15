# usb-dumper

Minimal USB-bootable, **read-only** SD-card acquisition tool for Behringer X32 /
Midas M32 consoles (NXP i.MX25, ARM926EJ-S).

It boots a stock-compatible OpenX32 Linux initramfs via the console's original
DCP USB-boot mechanism and copies the first 4,194,304 bytes of the internal SD
card to a USB stick, so the manufacturer's SD-resident DCP bootloader can be
analysed offline.

This is **not** an installer. No `.update` image is produced, and nothing is
written to internal storage.

## Contents

| Path | Purpose |
|---|---|
| `build.sh` | Host entry point: builds the container, then `baseline` or `dumper` |
| `Dockerfile` | Debian trixie build environment |
| `scripts/build-in-container.sh` | Full build pipeline and assertions |
| `src/x32sdump.c` | The entire userspace: PID 1, static ARM binary |
| `tests/smoke-test.sh` | 13 offline cases, no hardware |
| `config/imx25-pdk-dumper.dts` | Reduced device tree |
| `patches/` | U-Boot late-init patch |
| `SAFETY_AUDIT.md` | Evidence, residual risks, readiness gate |
| `USAGE.md` | Build, deploy, failure modes, recovery |
| `HARDWARE_TEST.md` | One-page runbook for the authorised console test |
| `RECOVERY.md` | Front-panel-only recovery paths, no disassembly |
| `preflight-usb.sh` | Refuses a stick that could install instead of boot |
| `verify-dump.py` | Read-only structural verification; captures stay private |

## Quick start

```sh
./usb-dumper/build.sh dumper
./usb-dumper/tests/smoke-test.sh
python3 tools/dcp_inspect.py usb-dumper/out/x32-sd-dumper.run

# before a console test
./usb-dumper/preflight-usb.sh /Volumes/YOURSTICK
# after a capture
python3 usb-dumper/verify-dump.py x32-dump/loader-prefix.img --manifest x32-dump/manifest.txt
```

## Design in one paragraph

The stock loader reads the `.run` into RAM at `0x80000000` and never mounts it
as a filesystem, so the USB stick is free as an output target. U-Boot is
rebuilt without any MMC write path (`ENV_IS_NOWHERE`, no `MMC_WRITE`; `saveenv`,
`mmc_write` and `blk_dwrite` are absent from the binary) and without its
surface-UART / Ethernet-PHY late init. The device tree disables DSP, FPGA,
audio, Ethernet, NAND, the surface UART and the OTG port, leaving only the
internal SD, the USB host, the LCD, UART5 for the console, and I²C. The
initramfs contains one statically linked binary as `/init` — no shell, no
BusyBox, no libraries. It identifies the internal card from sysfs, refuses
anything ambiguous, gates on the reported MBR geometry, reads exactly 4 MiB with
`pread`, closes the card, then mounts the USB stick, writes, fsyncs, re-reads
and verifies the SHA-256, and writes the manifest last so it can never claim
success for unverified output.
