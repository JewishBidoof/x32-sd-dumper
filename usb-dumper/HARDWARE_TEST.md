# HARDWARE TEST — one page

Historical single-unit runbook; included for documentation, not authorization for a new test. Read [RECOVERY.md](RECOVERY.md) first; every
recovery path there is front-panel only.

**Image:** `usb-dumper/out/x32-sd-dumper.run`
**SHA-256:** `f733fce154d350a89eccce274ed9da71005bdc1c537a0175247a88ab467eb2b7`

---

## 0. Before you touch the console

- [ ] Console is out of service and will stay out of service for ~10 minutes.
- [ ] Note the model (X32 / M32) and the FPGA generation shown on the boot
      screen (Xilinx or Lattice). Record both.
- [ ] The official `.update` for **this exact model** is downloaded and on hand.
- [ ] You accept that audio, DSP, FPGA and the surface are dead while the tool
      runs, and that the console needs a power cycle afterwards.

## 1. Prepare the stick

- [ ] FAT32, MBR, single partition, ≥ 64 MiB free.
- [ ] Copy `x32-sd-dumper.run` to the **root**, naming the USB copy `dcp_corefs_x32_sd_dumper.run`.
- [ ] Run the preflight and do not proceed unless its file checks pass:

```sh
./usb-dumper/preflight-usb.sh /Volumes/YOURSTICK
```

The preflight refuses any stick containing a `*.update` file. That check is the
single most important step in this document.

- [ ] Eject cleanly. Do not open the stick in Finder or an editor afterwards.

## 2. Boot

- [ ] Console powered **off**.
- [ ] Insert the stick.
- [ ] Press and hold the **VIEW button beside the top-panel USB recorder port** while powering on. Keep it held until the DCP bootloader appears, then release. This is the USB-recorder VIEW button, not a channel VIEW button.
- [ ] Watch the screen, and capture serial if you have it.

A normal power-on can go straight to stock without scanning the runner. If the DCP bootloader never appears, the dumper has not been shown to execute. Upstream documents this button sequence at https://github.com/OpenMixerProject/OpenX32/releases.

Serial is UART5, exposed on the MIDI ports at **115200 8N1** (see the OpenX32
README for the adapter note). Capture is optional but it is the only place the
U-Boot lines appear:

```
Loading Ramdisk to 0x83......
## Transferring control to Linux
```

If you see `Loading Ramdisk to`, relocation worked and the ramdisk is safe from
kernel decompression. That is the one boot-time fact worth recording.

## 3. Expected sequence

```
X32 SD DUMPER
INTERNAL SD FOUND
READING...
VERIFYING...
SUCCESS
```

Takes a few seconds. Then the console sits idle — that is correct, the tool
halts by design rather than exiting.

## 4. Abort criteria

Stop and go to [RECOVERY.md](RECOVERY.md) Tier 1 if:

- nothing appears within 60 s;
- the screen shows anything other than the sequence above;
- `FAILURE: NO_USB` appears **immediately** (would suggest the 15 s enumeration
  wait did not engage — worth reporting);
- the console reboots in a loop.

`FAILURE: GEOMETRY_MISMATCH` is not a fault. It means this unit's card layout
differs from the reported one; the tool wrote no image by design. Record the
manifest and stop.

## 5. After

- [ ] Confirm `SUCCESS` is on screen before removing the stick.
- [ ] Remove the stick, power-cycle, confirm the console boots stock normally.
- [ ] Copy `x32-dump/` off the stick.
- [ ] Verify the capture:

```sh
python3 usb-dumper/verify-dump.py x32-dump/loader-prefix.img \
        --manifest x32-dump/manifest.txt
```

Exit 0 = structurally consistent. Exit 2 = read the `WARN` lines; they are
findings, not necessarily failures. The tool redacts `SN=` and `MAC=` unless you
pass `--show-identifiers`.

- [ ] Keep the raw image private — it contains console identity data.

## 6. Record

| Field | Value |
|---|---|
| Model / FPGA generation | |
| Console firmware before test | |
| Screen output | |
| Serial capture (y/n), `Loading Ramdisk to` seen | |
| `status=` in manifest | |
| SHA-256 of `loader-prefix.img` | |
| Partition 1 start LBA / sectors | |
| Barker at `0x404` (0xB1?) | |
| Time to `SUCCESS` | |
| Anything unexpected | |

## 7. What this test can and cannot establish

It can establish: the loader accepts the image, the kernel boots, USB enumerates,
the card is identified, the read completes, the write verifies, and the unit
recovers.

It cannot establish: that the captured loader is authentic, what its cipher
does, or that the geometry generalises to other units. Those are separate
questions for the analysis phase.

## Runner discovery observation

The operator reported a successful test on an X32 Compact after placing identical runner bytes at the USB root as `dcp_corefs_x32_sd_dumper.run`. The original name `x32-sd-dumper.run` yielded "no update file". A 4 MiB capture and its checksums were verified locally. This is an observation for that unit and test, not a naming rule established for all X32/M32 models. No new hardware test is authorized by this repository. Never rename the runner to `.update`.
