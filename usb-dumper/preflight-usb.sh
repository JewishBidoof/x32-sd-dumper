#!/bin/bash
# Preflight check for the x32-sd-dumper USB stick.
#
#   ./usb-dumper/preflight-usb.sh /Volumes/STICK [expected-sha256]
#
# Refuses a stick that could install instead of boot, or that carries the wrong
# image. Runs on macOS or Linux. Never writes to the stick.
set -euo pipefail

VOL="${1:-}"
if [ -z "$VOL" ]; then
    echo "usage: $0 /Volumes/STICK [expected-sha256]" >&2
    exit 2
fi
if [ ! -d "$VOL" ]; then
    echo "FAIL: not a directory: $VOL" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXPECT="${2:-}"
if [ -z "$EXPECT" ]; then
    REPORT="$SCRIPT_DIR/out/dumper-build-report.json"
    if [ -f "$REPORT" ]; then
        EXPECT=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["final_run"]["sha256"])' "$REPORT")
    fi
fi

FAILED=0
pass() { printf '  PASS  %s\n' "$1"; }
fail() { printf '  FAIL  %s\n' "$1"; FAILED=1; }
warn() { printf '  WARN  %s\n' "$1"; }

echo "preflight: $VOL"
[ -n "$EXPECT" ] && echo "expected sha256: $EXPECT"
echo

# --- 1. no .update anywhere: the destructive path must not exist -----------
UPDATES=$(find "$VOL" -iname '*.update' -type f 2>/dev/null || true)
if [ -z "$UPDATES" ]; then
    pass "no .update file anywhere on the stick"
else
    fail "found .update file(s) — the loader would INSTALL, not boot:"
    echo "$UPDATES" | sed 's/^/          /'
fi

# --- 2. exactly one .run at the root, correctly named ---------------------
# bash 3.2 on macOS has no mapfile; collect with a newline list instead.
RUNS=$(find "$VOL" -maxdepth 1 -iname '*.run' -type f 2>/dev/null || true)
if [ -z "$RUNS" ]; then
    RUN_COUNT=0
else
    RUN_COUNT=$(printf '%s\n' "$RUNS" | wc -l | tr -d ' ')
fi
RUN_ONE=$(printf '%s\n' "$RUNS" | head -1)

if [ "$RUN_COUNT" -eq 0 ]; then
    fail "no .run at the root of the stick"
elif [ "$RUN_COUNT" -gt 1 ]; then
    fail "more than one .run at the root; the loader's choice is not predictable:"
    printf '%s\n' "$RUNS" | sed 's/^/          /'
else
    BASE=$(basename "$RUN_ONE")
    if [ "$BASE" = "dcp_corefs_x32_sd_dumper.run" ]; then
        pass "exactly one .run, named dcp_corefs_x32_sd_dumper.run"
    else
        warn "one .run found but named '$BASE' (deployment name: dcp_corefs_x32_sd_dumper.run)"
        pass "exactly one .run at the root"
    fi
fi

# --- 3. hash --------------------------------------------------------------
if [ "$RUN_COUNT" -eq 1 ] && [ -n "$EXPECT" ]; then
    if command -v shasum >/dev/null 2>&1; then
        ACTUAL=$(shasum -a 256 "$RUN_ONE" | awk '{print $1}')
    else
        ACTUAL=$(sha256sum "$RUN_ONE" | awk '{print $1}')
    fi
    if [ "$ACTUAL" = "$EXPECT" ]; then
        pass "sha256 matches"
    else
        fail "sha256 mismatch: got $ACTUAL"
    fi
elif [ "$RUN_COUNT" -eq 1 ]; then
    warn "no expected sha256 available (build report missing); skipping hash check"
fi

# --- 4. filesystem and space ---------------------------------------------
if command -v diskutil >/dev/null 2>&1; then
    INFO=$(diskutil info "$VOL" 2>/dev/null || true)
    if [ -n "$INFO" ]; then
        if echo "$INFO" | grep -qi "FAT32\|MS-DOS"; then
            pass "filesystem is FAT32"
        else
            fail "filesystem is not FAT32: $(echo "$INFO" | grep -i 'File System Personality' | cut -d: -f2- | xargs)"
        fi
        # Read the exact byte count. Parsing the human-readable line yields
        # "129" out of "129.1 GB" and produces a false NO_SPACE. The plist key
        # is FreeSpace (not VolumeFreeSpace).
        AVAIL=$(diskutil info -plist "$VOL" 2>/dev/null | plutil -extract FreeSpace raw -o - - 2>/dev/null || true)
        case "$AVAIL" in
            ''|*[!0-9]*)
                AVAIL=$(echo "$INFO" | sed -n 's/.*(\([0-9][0-9]*\) Bytes).*/\1/p' | head -1) ;;
        esac
        case "$AVAIL" in
            ''|*[!0-9]*)
                warn "could not read free space; check manually (need >= 8 MiB)" ;;
            *)
                if [ "$AVAIL" -lt 8388608 ]; then
                    fail "less than 8 MiB free ($AVAIL bytes)"
                else
                    pass "free space ok ($((AVAIL / 1024 / 1024)) MiB)"
                fi ;;
        esac
    else
        warn "not a mounted volume; skipping filesystem check"
    fi
else
    FSTYPE=$(stat -f -c %T "$VOL" 2>/dev/null || echo unknown)
    case "$FSTYPE" in
        *fat*|*msdos*) pass "filesystem looks like FAT ($FSTYPE)" ;;
        *) fail "filesystem is not FAT: $FSTYPE" ;;
    esac
fi

# --- 5. other files at the root ------------------------------------------
OTHERS=$(find "$VOL" -maxdepth 1 -type f ! -iname '*.run' 2>/dev/null | wc -l | tr -d ' ')
if [ "$OTHERS" -gt 0 ]; then
    warn "$OTHERS other file(s) at the root; harmless, but a clean stick is easier to reason about"
else
    pass "nothing else at the root"
fi

echo
if [ "$FAILED" -ne 0 ]; then
    echo "RESULT: DO NOT BOOT THIS STICK"
    exit 1
fi
echo "RESULT: USB file checks passed; hardware boot compatibility unverified"
echo
echo "Before inserting: confirm the console is out of service, and that you have"
echo "the official .update for this exact model on hand (see RECOVERY.md)."
