#!/usr/bin/env python3
"""Read-only inspection of an acquired X32/M32 SD-card prefix.

Usage:
    python3 usb-dumper/verify-dump.py loader-prefix.img
    python3 usb-dumper/verify-dump.py loader-prefix.img --manifest manifest.txt --json

This tool never writes, mounts, decrypts or executes anything. It reads a
regular file and reports structure. It cannot prove provenance or authenticity.

Identity fields (SN=, MAC=) are redacted unless --show-identifiers is given.
"""

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

EXPECTED_LENGTH = 4_194_304
EXPECTED_START_LBA = 2
EXPECTED_SECTORS = 8190

# i.MX25 flash-header field order, as used by the on-chip ROM at media offset
# 0x400. Names follow Freescale's `struct flash_header`. Interpretation is
# reported as *candidate*: the ROM layout is documented, but a vendor image
# could place additional data here.
FLASH_HEADER_FIELDS = [
    ("app_code_jump_vector", "branch or jump target executed first"),
    ("app_code_barker", "ROM marker, expected 0xB1"),
    ("app_code_csf", "code signing / CSF pointer (0 if unsigned)"),
    ("dcd_ptr_ptr", "pointer to the DCD pointer"),
    ("super_root_key", "SRK pointer (0 if unsigned)"),
    ("dcd_ptr", "device configuration data pointer"),
    ("app_dest_ptr", "destination address of the image in RAM"),
]

RAM_LO = 0x8000_0000
RAM_HI = 0x8800_0000   # generous upper bound for a 64 MiB part


def le32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def parse_mbr(data):
    out = {"signature_ok": data[510:512] == b"\x55\xaa", "partitions": []}
    for i in range(4):
        base = 0x1BE + i * 16
        status, ptype = data[base], data[base + 4]
        start, count = le32(data, base + 8), le32(data, base + 12)
        out["partitions"].append(
            {"index": i + 1, "status": status, "type": ptype,
             "start_lba": start, "sectors": count,
             "empty": status == 0 and ptype == 0 and start == 0 and count == 0}
        )
    return out


def parse_cfg(data, show_identifiers=False):
    """Return the :CFG block at 0x200 with identifiers redacted."""
    block = data[0x200:0x280]
    if not block.startswith(b":CFG"):
        return {"present": False}
    text = block.split(b"\x00")[0].decode("ascii", "replace").strip()
    fields = {}
    for token in text.split(":"):
        if "=" in token:
            key, _, val = token.partition("=")
            fields[key] = val
    redacted = {}
    for key, val in fields.items():
        keep = key in ("MDL", "DATE", "DBG") or show_identifiers
        redacted[key] = val if keep else "<redacted>"
    shown = []
    for token in text.split(":"):
        if token.startswith("CFG") or (token and "=" not in token):
            shown.append(token)
        elif "=" in token:
            key, _, _ = token.partition("=")
            shown.append(f"{key}={redacted.get(key, '<redacted>')}")
    return {
        "present": True,
        "raw_redacted": ":".join(shown),
        "model": fields.get("MDL"),
        "has_serial": "SN" in fields,
        "has_mac": "MAC" in fields,
        "fields": redacted,
    }


def parse_flash_header(data):
    hdr = data[0x400:0x400 + 28]
    words = [le32(hdr, i * 4) for i in range(7)]
    fields = []
    for (name, meaning), value in zip(FLASH_HEADER_FIELDS, words):
        fields.append({"name": name, "value": value, "hex": f"0x{value:08x}",
                       "meaning": meaning})
    barker = words[1]
    dest = words[6]
    jump = words[0]
    notes = []
    if barker == 0xB1:
        notes.append("barker 0xB1 present: consistent with an i.MX25 ROM boot header")
    elif barker == 0:
        notes.append("barker is 0: this is NOT a ROM-bootable header "
                     "(an independently built miniloader also has 0 here)")
    else:
        notes.append(f"barker is 0x{barker:02x}, not the documented 0xB1")
    for label, addr in (("app_dest_ptr", dest),):
        if addr == 0:
            notes.append(f"{label} is 0")
        elif not (RAM_LO <= addr < RAM_HI):
            notes.append(f"{label} 0x{addr:08x} is outside the plausible SDRAM window")
    if jump and not (RAM_LO <= jump < RAM_HI):
        # A branch instruction encodes as 0xEA...... ; either is plausible.
        if (jump >> 24) not in (0xEA, 0xEB):
            notes.append(f"app_code_jump_vector 0x{jump:08x} is neither a branch "
                         "instruction nor a SDRAM address")
    return {"offset": "0x400", "words": fields, "notes": notes}


def looks_like_arm_code(data, off):
    """Very weak heuristic: nonzero and not all-identical bytes."""
    chunk = data[off:off + 256]
    return len(set(chunk)) > 8


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("--manifest", help="manifest.txt to cross-check")
    ap.add_argument("--sha256-file", help="loader-prefix.sha256 to cross-check")
    ap.add_argument("--show-identifiers", action="store_true",
                    help="do not redact SN= / MAC= (use with care)")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    path = Path(args.image)
    if not path.is_file():
        print(f"error: not a regular file: {path}", file=sys.stderr)
        return 1
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()

    report = {
        "file": str(path),
        "length": len(data),
        "sha256": digest,
        "checks": [],
        "warnings": [],
    }

    def check(name, ok, detail=""):
        report["checks"].append({"check": name, "ok": bool(ok), "detail": detail})
        if not ok:
            report["warnings"].append(f"{name}: {detail}" if detail else name)

    check("length_is_4MiB", len(data) == EXPECTED_LENGTH,
          f"{len(data)} bytes, expected {EXPECTED_LENGTH}")

    if len(data) < 0x400:
        print("error: file shorter than the flash header", file=sys.stderr)
        return 1

    # --- hashes -------------------------------------------------------------
    sha_file = Path(args.sha256_file) if args.sha256_file else path.with_name("loader-prefix.sha256")
    if sha_file.is_file():
        parts = sha_file.read_text().split()
        declared = parts[0] if parts else ""
        check("sha256_matches_sha256_file", declared == digest,
              f"declared {declared[:16]}..., computed {digest[:16]}...")
        report["declared_sha256"] = declared
    else:
        report["warnings"].append("no loader-prefix.sha256 found next to the image")

    manifest = {}
    man_path = Path(args.manifest) if args.manifest else path.with_name("manifest.txt")
    if man_path.is_file():
        for line in man_path.read_text().splitlines():
            if "=" in line:
                k, _, v = line.partition("=")
                manifest[k.strip()] = v.strip()
        report["manifest"] = manifest
        if manifest.get("sha256"):
            check("sha256_matches_manifest", manifest["sha256"] == digest,
                  "manifest sha256 differs from the file")
        if manifest.get("image_length"):
            check("manifest_length_matches", manifest["image_length"] == str(len(data)),
                  f"manifest says {manifest['image_length']}")

    # --- partition table ----------------------------------------------------
    mbr = parse_mbr(data)
    report["mbr"] = mbr
    check("mbr_signature", mbr["signature_ok"], "0x55AA not found at offset 510")
    p1 = mbr["partitions"][0]
    check("partition1_start_lba", p1["start_lba"] == EXPECTED_START_LBA,
          f"start LBA {p1['start_lba']}, expected {EXPECTED_START_LBA}")
    check("partition1_sectors", p1["sectors"] == EXPECTED_SECTORS,
          f"{p1['sectors']} sectors, expected {EXPECTED_SECTORS}")

    covered_lbas = len(data) // 512
    p1_end = p1["start_lba"] + p1["sectors"]
    check("partition1_fully_captured", p1_end <= covered_lbas,
          f"partition 1 ends at LBA {p1_end}, capture covers 0..{covered_lbas - 1}")

    # --- config block -------------------------------------------------------
    cfg = parse_cfg(data, args.show_identifiers)
    if not args.show_identifiers and cfg.get("present"):
        cfg.pop("fields", None)
    report["config"] = cfg
    check("cfg_block_present", cfg["present"], "no :CFG marker at offset 0x200")
    if cfg.get("present"):
        check("cfg_model_determinable", bool(cfg.get("model")),
              "no MDL= token in the config block")
        if manifest.get("model") and cfg.get("model"):
            check("model_matches_manifest", manifest["model"] == cfg["model"],
                  f"manifest {manifest['model']} vs card {cfg['model']}")

    # --- flash header -------------------------------------------------------
    fh = parse_flash_header(data)
    report["flash_header"] = fh
    check("flash_header_at_0x400", looks_like_arm_code(data, 0x400),
          "0x400..0x500 looks like padding or zeros")
    check("flash_header_barker_0xB1", le32(data, 0x404) == 0xB1,
          "barker is not 0xB1; see flash_header.notes")

    # --- what this cannot tell you -----------------------------------------
    report["cannot_conclude"] = [
        "whether these bytes came from a genuine manufacturer card",
        "whether the loader is the same revision as any other unit",
        "whether the code decrypts anything, or how",
        "whether the image would boot if written back",
    ]

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"file      {path}")
        print(f"length    {len(data)} bytes")
        print(f"sha256    {digest}")
        print()
        for c in report["checks"]:
            print(f"  {'PASS' if c['ok'] else 'WARN'}  {c['check']}"
                  + (f"  ({c['detail']})" if not c["ok"] and c["detail"] else ""))
        print()
        print("partition table")
        for p in mbr["partitions"]:
            if p["empty"]:
                continue
            print(f"  #{p['index']} type=0x{p['type']:02x} start_lba={p['start_lba']} "
                  f"sectors={p['sectors']}")
        print()
        print("flash header @ 0x400 (candidate i.MX25 layout)")
        for f in fh["words"]:
            print(f"  {f['hex']}  {f['name']:<20} {f['meaning']}")
        for note in fh["notes"]:
            print(f"  note: {note}")
        if cfg.get("present"):
            print()
            print(f"config    {cfg['raw_redacted']}")
        print()
        print("this tool cannot conclude:")
        for c in report["cannot_conclude"]:
            print(f"  - {c}")

    return 0 if not report["warnings"] else 2


if __name__ == "__main__":
    sys.exit(main())
