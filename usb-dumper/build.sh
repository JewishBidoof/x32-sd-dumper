#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$REPO_ROOT"

MODE="${1:-baseline}"
if [ "$MODE" != "baseline" ] && [ "$MODE" != "dumper" ]; then
    echo "Usage: $0 [baseline|dumper]" >&2
    exit 1
fi

echo "=== [usb-dumper host build runner] (MODE: $MODE) ==="
echo "Building container image 'openx32-builder'..."
docker build -t openx32-builder "$SCRIPT_DIR"

echo "Running build inside container..."
docker run --rm \
    -v "$REPO_ROOT:$REPO_ROOT" \
    -w "$REPO_ROOT" \
    openx32-builder \
    bash usb-dumper/scripts/build-in-container.sh "$MODE"

REPORT_FILE="$REPO_ROOT/usb-dumper/out/${MODE}-build-report.json"
if [ -f "$REPORT_FILE" ]; then
    python3 - "$REPORT_FILE" "$MODE" << 'PYEOF'
import json, sys

with open(sys.argv[1]) as f:
    r = json.load(f)

fr = r.get("final_run", {})
mode = sys.argv[2]

print("")
print("=" * 80)
print(f"                     FINAL BUILD SUMMARY ({mode.upper()})")
print("=" * 80)
print(f"  .run path:            {fr.get('path')}")
print(f"  .run size:            {fr.get('size_bytes')} bytes")
print(f"  .run sha256:          {fr.get('sha256')}")

if "initramfs" in r:
    print("  initramfs members:")
    for m in r["initramfs"].get("files", []):
        print(f"    - {m}")
    print(f"  no_forbidden_members: {r['initramfs'].get('no_forbidden_members')}")
    ib = r["initramfs"].get("init_binary", {})
    if ib:
        print(f"  init binary size:     {ib.get('size_bytes')} bytes")
        print(f"  init binary sha256:   {ib.get('sha256')}")
elif mode == "baseline":
    print("  initramfs:            stock (busybox + glibc runtime)")

print("=" * 80)
print("")
PYEOF
fi

echo "=== Host build completed successfully! ==="
