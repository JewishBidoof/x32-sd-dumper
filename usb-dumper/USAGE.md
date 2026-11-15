# USAGE — x32-sd-dumper

Read-only acquisition of the first 4 MiB of a Behringer X32 / Midas M32 internal
SD card, for recovering the manufacturer's DCP bootloader.

**Read [SAFETY_AUDIT.md](SAFETY_AUDIT.md) first.** One X32 Compact test succeeded (operator reported); captured length and checksums were verified locally. Other units and models remain unverified.

---

## Build

Requires Docker. The host is never modified; all compilation happens in
`debian:trixie-slim`.

```sh
./usb-dumper/build.sh dumper      # produces x32-sd-dumper.run
./usb-dumper/build.sh baseline    # stock-equivalent reference image (optional)
```

Output:

```
usb-dumper/out/x32-sd-dumper.run
usb-dumper/out/dumper-build-report.json
```

The original build produced matching hashes on two consecutive runs. This standalone build fetches pinned sources, but Debian packages and the container tag are not pinned; byte identity across environments is not guaranteed. The extracted pipeline has not yet been rebuilt end to end.
Sources are pinned:

| Component | Revision |
|---|---|
| OpenX32 | `fd94919c1b2fbed21c7ff8e51ca316abf37dcf5f` (GPL-3.0) |
| U-Boot | `050acee119b3757fee3bd128f55d720fdd9bb890` (v2020.10) |
| Linux | `a256b1e6892e7fe840f0f9746316fa938e9a421f` (v6.18.25) |
| BusyBox | `5f07327251c93184dfcfc8d978fc35705930ec53` (baseline mode only) |

## Verify before deploying

```sh
python3 tools/dcp_inspect.py usb-dumper/out/x32-sd-dumper.run
./usb-dumper/tests/smoke-test.sh
```

Expected: `count=1`, `directory_md5_valid=true`, `image_md5_valid=true`,
member `binary/dcpapp.bin`; smoke tests 13/13.

## Deploy

1. Format a USB stick as **FAT32** (MBR, single partition). Keep it small
   (≥ 64 MiB free).
2. Copy `x32-sd-dumper.run` to the **root**, naming the USB copy `dcp_corefs_x32_sd_dumper.run` of the stick.
3. Eject cleanly.
4. With the console off and the stick inserted, hold **VIEW beside the USB recorder port** while powering on, until the DCP bootloader appears. Normal power-on may boot stock directly.

The console's stock loader reads the `.run` from USB into RAM. The stick is not
used as a filesystem by Linux, so it remains available as the output target.

## Expected behaviour

Console screen (800×480 framebuffer), and the same lines on serial
`ttymxc4` @ 115200:

```
X32 SD DUMPER
INTERNAL SD FOUND
READING...
VERIFYING...
SUCCESS
```

On the USB stick:

```
x32-dump/
  loader-prefix.img       4194304 bytes
  loader-prefix.sha256
  manifest.txt
```

`manifest.txt`:

```
utility_version=1.0.0
build_commit=<short sha>
timestamp_utc=<iso8601 | unreliable>
model=X32
storage_bytes=...
sector_size=512
partition1_start_lba=2
partition1_sectors=8190
image_length=4194304
sha256=<hex>
status=SUCCESS
```

No serial number, MAC address, scene or preset data is written.

The console does not return to normal operation after the dump. It halts with
`SUCCESS` on screen; power-cycle to leave the tool. **Power-cycling is not a
guaranteed recovery procedure for an untested custom image.**

## Failure modes

Every failure prints `FAILURE: <reason>` and writes no image.

| Reason | Meaning |
|---|---|
| `NO_INTERNAL_SD` | no `mmcblk*` with `device/type=SD` |
| `MULTIPLE_INTERNAL_SD` | more than one candidate; refusing to guess |
| `INTERNAL_SD_REMOVABLE` | candidate reports `removable=1`; not the internal card |
| `NO_USB` / `MULTIPLE_USB` | USB destination not unambiguous after a 15 s enumeration wait |
| `SAME_DEVICE` | internal and USB resolve to the same device |
| `GEOMETRY_MISMATCH` | MBR missing, or partition 1 is not LBA 2 / 8190 sectors. Manifest only |
| `SHORT_READ` | source shorter than 4 MiB or read error |
| `NO_SPACE` | under 8 MiB free on the stick |
| `MOUNT_FAILED` | USB partition is not mountable VFAT |
| `VERIFY_FAILED` | read-back SHA-256 mismatch; manifest records it |

`GEOMETRY_MISMATCH` is the expected outcome on a hardware revision whose card
layout differs from the reported one. Record the manifest and stop — do not
retry with a different device.

## Recovery plan

Before any authorised test:

1. Confirm the unit's model and FPGA generation, and that it currently boots
   normally from its internal card.
2. Have the serial console (UART5 / MIDI-port serial, 115200 8N1) connected and
   logging, so `Loading Ramdisk to 0x83……` and any abort are captured.
3. Keep the stock firmware `.update` for that exact model available, in case
   the unit needs to be re-provisioned through the normal update path.
4. Accept that the console is unusable as a mixer while the tool runs, and that
   an untested image may leave it requiring recovery.
5. Power-cycle only after `SUCCESS`/`FAILURE` is visible and the USB stick has
   been unmounted by the tool.

## What this does not do

- No full-card read (only the 4 MiB prefix).
- No decryption, decompression, or parsing of the captured bytes.
- No writing, formatting, or updating of internal storage.
- No audio, DSP, FPGA, fader, or network initialisation.

## Runner discovery observation

The operator reported a successful test on an X32 Compact after placing identical runner bytes at the USB root as `dcp_corefs_x32_sd_dumper.run`. The original name `x32-sd-dumper.run` yielded "no update file". A 4 MiB capture and its checksums were verified locally. This is an observation for that unit and test, not a naming rule established for all X32/M32 models. No new hardware test is authorized by this repository. Never rename the runner to `.update`.
