# SAFETY_AUDIT — x32-sd-dumper

Audit date: 2026-11-14. Applies to `usb-dumper/out/x32-sd-dumper.run`
(SHA-256 `f733fce154d350a89eccce274ed9da71005bdc1c537a0175247a88ab467eb2b7`,
3,723,264 bytes).

**Status: historical offline audit plus one operator-reported successful X32 Compact test.** Captured length and checksums were verified locally. No capture is included. Other units/models remain unverified; this audit does not authorize further hardware interactions. The hash above identifies the original private build, not a promise about future standalone builds.

This document records the original image audit. The standalone fetch pipeline has not been rebuilt in this extraction session.

## Trust basis

Upstream OpenX32 is treated as a reliable source: its documented boot chain,
USB `.run` boot, and SD layout are accepted without independent
re-verification. That does **not** extend to unit-specific state — in
particular whether a given console's card geometry matches the layout reported
in issue #46. The tool gates on that at runtime and aborts (`GEOMETRY_MISMATCH`)
rather than capturing the wrong region.

---

## 1. What the image is

A plaintext DCP container (`.run`) built by the upstream
`software/dcpapp/dcp_compiler.pl`, containing exactly one member,
`binary/dcpapp.bin`, which is a contiguous boot blob:

| Offset | Content | Size |
|---|---|---|
| `0x000000` | OpenX32 miniloader | 472 B |
| `0x0000C0` | U-Boot 2020.10 (`mx25pdk`) | 283,888 B |
| `0x060000` | Linux 6.18.25 kernel uImage (`load=entry=0x80060000`) | 2,756,616 B |
| `0x330000` | Device tree blob | 18,150 B |
| `0x340000` | Initramfs uImage (`x32-sd-dumper`) | 314,651 B |

The stock loader places the blob at `0x80000000`; the miniloader jumps to
U-Boot at `0x800000C0`; U-Boot runs
`bootm 0x80060000 0x80340000 0x80330000`.

### Merge offsets differ from upstream `compile.sh` — deliberately

Upstream `compile.sh` writes the DTB at `0x300000` and the ramdisk at
`0x310000`. Both are wrong for this kernel:

- the kernel uImage ends at `0x300EF0`, so a DTB at `0x300000` would have its
  first 3,824 bytes overwritten by kernel data;
- `CONFIG_BOOTCOMMAND` and `bootargs initrd=` both name `0x80330000` /
  `0x80340000`, i.e. file offsets `0x330000` / `0x340000`, not `0x310000`.

This build uses `0x330000` / `0x340000`, which is self-consistent with the
U-Boot configuration that ships in the same tree.

### Kernel decompression does not clobber them

The decompressed kernel is 8,225,664 B and lands at `0x80008000`, reaching
`~0x807E0800` — far above the DTB/ramdisk *source* addresses. Safety depends on
U-Boot relocating both. That is compiled in and was verified by symbol:

```
$ arm-linux-gnueabi-nm u-boot | grep -wE 'boot_relocate_fdt|boot_ramdisk_high'
80011eec T boot_relocate_fdt
80011bb4 T boot_ramdisk_high
```

**Hardware-validation item:** confirm U-Boot prints `Loading Ramdisk to
0x83……` on the serial console. If it does not, the ramdisk will be destroyed
by decompression.

---

## 2. U-Boot: storage-write paths removed

Upstream ships two latent write paths to the internal SD. Neither is reached by
`bootcmd`, but both are eliminated in dumper mode.

| Setting | Upstream | Dumper build |
|---|---|---|
| `CONFIG_ENV_IS_IN_MMC` | `y` (offset `0x60000`, dev 0) | **not set** |
| `CONFIG_ENV_IS_NOWHERE` | — | `y` |
| `CONFIG_MMC_WRITE` | `y` | **not set** |

Verified against the built ELF, not the config alone:

```
saveenv          ABSENT
mmc_write        ABSENT
blk_dwrite       ABSENT
mmc_initialize   present   (read path retained)
```

There is therefore no compiled code path in this U-Boot that can write to the
MMC device.

### Surface/fader and Ethernet initialisation removed

`board_late_init()` upstream resets the Ethernet PHY and pushes LCD/LED
commands to the surface controller over UART2. Both are removed by
`patches/0001-uboot-disable-surface-and-fec-late-init.patch`; the function now
only returns 0. The surface helpers remain defined but unused.

`board_init()` is **unchanged** on purpose: it enables USB host power
(`GPIO4_11`), the display backlight PWM, the MCU LED, and releases
`SURFACE_RESET`. USB power is required; the backlight is required for the
framebuffer console.

---

## 3. Kernel: no storage writes, no audio/FPGA binding

No kernel configuration was changed. The stock OpenX32 config already provides
`MMC_BLOCK`, `BLK_DEV_SD`, `SCSI`, `USB_STORAGE`, `VFAT_FS`, `NLS` (437 +
ISO8859-1), `DEVTMPFS_MOUNT`, `FB_IMX`, `FRAMEBUFFER_CONSOLE`.

The device tree is reduced (`config/imx25-pdk-dumper.dts`), verified by
decompiling the built DTB:

| Disabled | Enabled |
|---|---|
| `sound`, `ssi1`, `audmux`, `spi1`, `spi3`, `fec`, `nfc`, `tsc`, `tscadc`, `uart1`, `uart2`, `uart3`, `uart4`, `usbotg` | `esdhc1`, `usbhost1`, `lcdc`, `uart5`, `i2c1`, `gpio-leds` |

Consequences:

- **`spi1` / `spi3` disabled** → the DSP and FPGA buses have no driver bound.
  No DSP load, no FPGA configuration is possible.
- **`sound` / `ssi1` / `audmux` disabled** → no audio path is instantiated.
- **`uart2` disabled** → the surface controller (faders, LEDs, channel LCDs) is
  not addressed by Linux at all.
- **`fec` disabled** → no Ethernet, no DHCP, no SSH surface.
- **`nfc` disabled** → no NAND probe.
- **`esdhc1` enabled, `non-removable`** → the internal card is
  `/dev/mmcblk0`; the driver only reads unless asked to write.
- **`uart5` enabled** → `console=ttymxc4` still resolves
  (`serial@5002c000` is the only enabled UART), so serial output works.

A kernel cannot write to the internal card unless userspace issues a write
syscall or mounts it. Neither happens (see §4).

---

## 4. Userspace: one static binary, no shell

The initramfs contains exactly:

```
.
dev
init
mnt
proc
sys
```

- `/init` is the acquisition program: ELF 32-bit LSB, ARM EABI5,
  **statically linked**, 698,412 B, SHA-256
  `d9d5ef8c9483755c38349a5b639c268b9299e5ca1a0eb682de81280e59979df4`.
- No BusyBox, no `/bin`, `/sbin`, `/lib`, no shell, no `omc`, no Dropbear, no
  networking configuration, no scripts. The build asserts this
  (`no_forbidden_members`).
- The kernel executes `/init` directly as PID 1; no shell exists to run an
  unintended command.

The program itself:

- opens the internal device `O_RDONLY` and uses **`pread` only**;
- **never mounts** the internal card, never runs `fsck`, `mkfs`, partitioning
  or any media-modifying tool, and never decrypts or decompresses;
- **closes the internal device before the USB stick is mounted**, so the two
  are never open simultaneously;
- writes only under `<usb>/x32-dump/`.

---

## 5. Internal SD identification (no hardcoded device node)

`/dev/mmcblk0` is not assumed. The program requires:

1. exactly one `/sys/block/mmcblk*` with `device/type == "SD"`;
2. that device must not report `removable == 1`;
3. exactly one `/sys/block/sd*` that is **either** `removable == 1` **or**
   sits below a `usbN` node in its sysfs ancestry;
4. the two to resolve to different `major:minor` **and** different real paths.

Any ambiguity aborts before a single byte is read.

Two details matter on real hardware:

- **Async enumeration.** `/init` runs milliseconds after the kernel starts, but
  USB host probe, SCSI inquiry and `sd` registration take seconds. A single
  scan would report `NO_USB` on working hardware, so the scan is retried every
  200 ms for up to 15 s in production mode (0 in test mode). It stops early as
  soon as the result is unambiguous, and also stops early on ambiguity.
- **`removable` is not a reliable USB test.** Many flash drives and USB-SSD
  bridges report the SCSI RMB bit as 0, so `removable` is `0` on the USB bus.
  The ancestry check exists for exactly that case. Conversely, upstream Linux
  never sets `GENHD_FL_REMOVABLE` for MMC block devices, so check 2 is
  defence-in-depth only and cannot prove non-removability — the real guarantee
  is the DTB's `&esdhc1 non-removable` plus the absence of any other MMC host
  on this board.

The USB stick is mounted by trying each existing partition (`sdX1`…`sdX15`)
before a superfloppy fallback. `fstype` is pinned to `vfat`, so a non-FAT
partition fails to mount instead of being mounted read-write by another
filesystem driver.

### Geometry gate

Before reading, LBA 0 is parsed. The build requires:

- `0x55AA` signature at offset 510;
- partition entry 1: `start_lba == 2`, `sectors == 8190`.

On mismatch the program writes **only** `manifest.txt` with
`status=GEOMETRY_MISMATCH`, writes **no** image, and fails. It does not guess,
resize, or fall back to another device.

These values come from upstream issue reports and are treated as an
expectation, not a law: a different revision aborts rather than capturing
something unexpected.

---

## 6. Acquisition invariants

- Exactly **4,194,304 bytes** from offset 0. Any short read, `EIO`, or `EOF`
  aborts with `status=SHORT_READ` and writes no image.
- Bytes are preserved verbatim — no interpretation during capture.
- SHA-256 is computed over the in-RAM buffer.
- Output is written to `loader-prefix.img.tmp`, `fsync`ed, renamed, and the
  directory `fsync`ed.
- The image is then re-opened from the USB stick, re-hashed, and compared.
- **`manifest.txt` is written last**, so its `status` can never say `SUCCESS`
  for an image that failed read-back. This was a defect in the first
  implementation and is covered by smoke-test case 10.
- `sync()` then `umount2(MNT_DETACH)` before the final status line.
- On completion the process `pause()`s forever instead of exiting, because
  exiting PID 1 panics the kernel. No further I/O occurs.

Privacy: the manifest records `model` only (parsed from the `:MDL=` token of the
`:CFG` block). `SN=` and `MAC=` are never copied, and smoke-test case 1 asserts
their absence from all output.

---

## 7. Offline verification performed

`usb-dumper/tests/smoke-test.sh` — 13/13 pass:

| # | Case | Assertion |
|---|---|---|
| 1 | happy path | exit 0, `SUCCESS`, exactly 4,194,304 B, SHA-256 matches, `model=X32`, no SN/MAC leakage |
| 2 | no internal SD | non-zero, no image |
| 3 | two `mmcblk` SD devices | non-zero, no image |
| 4 | geometry mismatch | non-zero, no image, `status=GEOMETRY_MISMATCH` |
| 5 | truncated source | non-zero, no image |
| 6 | no USB device | non-zero |
| 7 | insufficient space | non-zero, no image |
| 8 | removable internal device | non-zero, `INTERNAL_SD_REMOVABLE` |
| 9 | test-only flags in production mode | usage error |
| 10 | forced verify failure | `status=VERIFY_FAILED`, never `SUCCESS` |
| 11 | USB appears after 400 ms | retry loop finds it, `SUCCESS` |
| 12 | USB drive reports `removable=0` | found via USB sysfs ancestry |
| 13 | non-USB fixed disk, `removable=0` | rejected as destination |

Container/DCP verification:

- `tools/dcp_inspect.py`: `count=1`, `directory_md5_valid=true`,
  `image_md5_valid=true`, `closes_at_eof=true`, member `binary/dcpapp.bin`,
  `raw_prefix_matches=true`, `raw_full_matches=true`.
- Build is **byte-reproducible**: two consecutive
  `./usb-dumper/build.sh dumper` runs produce the identical SHA-256.
  Achieved by pinning `SOURCE_DATE_EPOCH` (U-Boot embeds `U_BOOT_DATE/TIME`),
  `KBUILD_BUILD_TIMESTAMP`, cpio mtimes, and `gzip -n`.
- The embedded `build_commit` is pinned to the boot-chain source revision
  (`openx32.fd94919`), **not** the research-repo HEAD, so the published hash
  stays valid across documentation commits. The research-repo commit is
  recorded in `out/dumper-build-report.json`.

---

## 8. Residual risks (ranked)

1. **The stock loader may reject a plaintext DCP.** Upstream documents that a
   `.run` built this way boots from USB, and the container is structurally
   valid, but this is *not* verified here. It is the single largest
   assumption.
2. **USB host re-initialisation after handoff.** The stock loader owns the USB
   host while reading the `.run`; U-Boot has `CONFIG_USB` disabled; Linux
   brings the controller up from scratch. Untested.
3. **DTB/ramdisk relocation.** See §1. Watch for `Loading Ramdisk to` on
   serial.
4. **Device-identification heuristics.** USB detection falls back to sysfs
   ancestry when `removable` is 0; a hypothetical USB bridge that presents
   itself without a `usbN` ancestor would be missed (`NO_USB`). The internal
   card check relies on `device/type == "SD"`; the `removable` guard is inert
   on upstream kernels. Both are safe failures, not unsafe successes.
5. **Geometry assumption.** If a revision uses different partition geometry,
   acquisition aborts by design.
6. **`/dev/tty0` availability.** Framebuffer console output is best-effort; the
   serial console (`ttymxc4`, 115200) is the reliable channel.
7. **Hardware revision coverage.** X32 vs M32, and pre/post-2020 FPGA variants,
   were not distinguished. Nothing here proves boot compatibility for a
   specific unit.
8. **Enumeration timeout.** The USB wait is bounded at 15 s. A very slow stick
   or hub could exceed it and produce `NO_USB`; the failure is safe and the
   bound can be raised with `--wait-ms` (production builds use the default).

Recovery is front-panel only and requires no disassembly; see
[RECOVERY.md](RECOVERY.md). The primary path is structural rather than hopeful:
this image contains no write path to internal storage, and the loader
distinguishes `.run` (boot from RAM) from `.update` (install). Removing the
stick and power-cycling therefore returns the console to its stock firmware.
That reasoning applies to *this* image specifically; it is not a general claim
that power-cycling recovers a console from an arbitrary custom image.

---

## 9. Readiness gate

| # | Requirement | Status |
|---|---|---|
| 1 | DCP image structurally validated | **PASS** (both MD5s, single member, reproducible) |
| 2 | Kernel/DT/init/startup audited for storage writes | **PASS** (documented above) |
| 3 | Internal SD reads read-only at every app-controlled layer | **PASS offline**; operator-reported acquisition success; physical write behavior not measured |
| 4 | No DSP/FPGA/headamp/fader init | **PASS offline** (DTS disabled + U-Boot patch) |
| 5 | External USB output verified | **PASS offline** (13 cases) |
| 6 | Exact console model and boot compatibility established | X32 Compact operator-reported success only |
| 7 | Realistic recovery plan documented | **PASS** (`USAGE.md`) |
| 8 | Operator understands normal mixer operation is interrupted | **PASS** (`USAGE.md`) |

The operator reported a successful test on an X32 Compact after placing identical runner bytes at the USB root as `dcp_corefs_x32_sd_dumper.run`. The original name `x32-sd-dumper.run` yielded "no update file". A 4 MiB capture and its checksums were verified locally. This is an observation for that unit and test, not a naming rule established for all X32/M32 models. No new hardware test is authorized by this repository. Never rename the runner to `.update`.

Preflight checks USB files; it cannot establish hardware safety. Keep captures private. The verifier redacts identifiers by default.
