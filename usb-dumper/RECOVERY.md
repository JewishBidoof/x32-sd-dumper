# RECOVERY — no disassembly required

Every path below is done from the front panel and a USB stick. **None of them
requires opening the console.**

---

## Why recovery is easy here

The stock loader chooses its behaviour from the **file extension**:

| File on the stick | What the loader does |
|---|---|
| `*.run` | reads it into RAM and boots it. Internal storage untouched. |
| `*.update` | **installs** it to internal SD. |

Our image is a `.run`, and it contains no path that can write to internal
storage:

- U-Boot has no MMC write code (`saveenv`, `mmc_write`, `blk_dwrite` are
  absent from the binary);
- the kernel never mounts the internal card;
- userspace opens it `O_RDONLY` and uses `pread` only.

So after any run — success, failure, or hang — the internal SD is byte-for-byte
what it was. Removing the stick and power-cycling returns the console to its
stock firmware.

That is the whole recovery story in the normal case.

---

## Tier 0 — before booting: make the destructive case impossible

Run this against the mounted stick:

```sh
./usb-dumper/preflight-usb.sh /Volumes/YOURSTICK
```

It refuses the stick unless:

- exactly one `*.run` exists at the root and it is named `x32-sd-dumper.run`;
- **no `*.update` file exists anywhere on it**;
- its SHA-256 matches the built image;
- the filesystem is FAT32 and there is space for the output.

This is the only guard that matters. Everything below assumes it passed.

---

## Tier 1 — power cycle (no tools)

Use when the screen shows `SUCCESS` or `FAILURE`, or the console appears stuck.

1. Power the console off.
2. **Remove the USB stick.**
3. Power it on.

Expected: normal stock boot, under a minute.

The tool halts deliberately after printing its final line (it does not exit,
because exiting PID 1 would panic the kernel), so a blank or frozen screen
after the status line is normal, not a fault.

---

## Tier 2 — official firmware re-provision (no tools)

Use if Tier 1 does not restore normal operation.

1. Obtain the official Behringer/Midas `.update` for **this exact model**
   (X32 and M32 files differ; so do FPGA generations).
2. Copy it to the root of a FAT32 stick. Nothing else on the stick.
3. Insert, power on, and let the loader install it.
4. Power-cycle when it reports completion.

This is the manufacturer's own update mechanism and does not touch the loader
in partition 1 — an `.update` replaces the application archive. It is therefore
expected to recover a unit even if the dumper had been installed as an
`.update` by mistake. *Expected, not verified here.*

---

## Tier 3 — USB serial download (no disassembly, needs a laptop)

Use only if Tiers 1 and 2 both fail.

OpenX32 drives the i.MX25 on-chip ROM over USB using the Serial Download
Protocol, loading a binary into RAM at `0x80000000` without writing flash. The
project ships this as `usb_boot.sh` / `software/pyatk`:

```
mx-toolkit.py run -b mx25 <image> 0x80000000 -i meminit.txt
```

This runs code from RAM only. It requires the OTG port to be reachable from
outside the chassis and the ROM to enter SDP mode — **confirm which connector
that is on your unit before relying on it.** Documented by OpenX32; not
verified on this hardware.

---

## Not part of this plan

Opening the console, removing the internal SD card, or attaching to internal
headers. Those remain available to a service technician and are explicitly
excluded here.

---

## Decision table

| Symptom | Action |
|---|---|
| `SUCCESS` on screen, console otherwise idle | Tier 1 |
| `FAILURE: …` on screen | Read the reason, then Tier 1. Nothing was written. |
| No display, no boot after 60 s | Power off, remove stick, power on (Tier 1) |
| Still no boot after Tier 1 | Tier 2 |
| Still no boot after Tier 2 | Tier 3, then service |
| You accidentally renamed the file to `.update` | Tier 2. Do not keep booting it. |

---

## What not to do

- Do not rename the file. `.update` installs; `.run` boots from RAM.
- Do not run `fsck`, `mkfs`, or any partitioning tool against the internal card.
- Do not repeatedly power-cycle a unit that is mid-install (Tier 2). Let it
  finish.
- Do not leave the stick in a computer that might rewrite it between the
  preflight check and the boot.
