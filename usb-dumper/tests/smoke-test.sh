#!/bin/bash
set -euo pipefail

# If on macOS, re-exec inside docker container
if [ "$(uname)" = "Darwin" ]; then
    REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
    IMAGE="debian:trixie-slim"
    if docker image inspect x32-smoke-base:latest >/dev/null 2>&1; then
        IMAGE="x32-smoke-base:latest"
    fi
    exec docker run --rm -v "$REPO_DIR:/work" -w /work "$IMAGE" /work/usb-dumper/tests/smoke-test.sh "$@"
fi

# Ensure compilers are available
if ! command -v gcc >/dev/null 2>&1 || ! command -v arm-linux-gnueabi-gcc >/dev/null 2>&1; then
    echo "Installing build-essential and gcc-arm-linux-gnueabi..."
    apt-get update -qq && apt-get install -y -qq build-essential gcc-arm-linux-gnueabi >/dev/null
fi

REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$REPO_DIR/usb-dumper/src/x32sdump.c"
SCRATCH="$REPO_DIR/.local/build/dumper-prog"
mkdir -p "$SCRATCH"

NATIVE_BIN="$SCRATCH/x32sdump-native"
ARM_BIN="$SCRATCH/x32sdump-arm"

echo "=== Compiling native and ARM static binaries ==="
gcc -std=c99 -Wall -Wextra -Werror -O2 -o "$NATIVE_BIN" "$SRC"
arm-linux-gnueabi-gcc -mcpu=arm926ej-s -Os -static -Wall -Wextra -Werror -o "$ARM_BIN" "$SRC"

echo "ARM static binary size:"
size "$ARM_BIN"
ls -lh "$ARM_BIN"

# Helper to generate fake SD image
cat << 'EOF' > "$SCRATCH/gen_sd.c"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 5) return 1;
    const char *path = argv[1];
    uint32_t start_lba = (uint32_t)strtoul(argv[2], NULL, 10);
    uint32_t sectors = (uint32_t)strtoul(argv[3], NULL, 10);
    size_t size = (size_t)strtoull(argv[4], NULL, 10);

    FILE *f = fopen(path, "wb");
    if (!f) return 1;

    uint8_t *buf = calloc(1, size);
    if (!buf) { fclose(f); return 1; }

    if (size >= 512) {
        buf[510] = 0x55;
        buf[511] = 0xAA;
        buf[0x1BE + 4] = 0x83;
        buf[0x1BE + 8]  = (uint8_t)(start_lba & 0xff);
        buf[0x1BE + 9]  = (uint8_t)((start_lba >> 8) & 0xff);
        buf[0x1BE + 10] = (uint8_t)((start_lba >> 16) & 0xff);
        buf[0x1BE + 11] = (uint8_t)((start_lba >> 24) & 0xff);
        buf[0x1BE + 12] = (uint8_t)(sectors & 0xff);
        buf[0x1BE + 13] = (uint8_t)((sectors >> 8) & 0xff);
        buf[0x1BE + 14] = (uint8_t)((sectors >> 16) & 0xff);
        buf[0x1BE + 15] = (uint8_t)((sectors >> 24) & 0xff);
    }
    if (size >= 0x200 + 64) {
        const char *cfg = ":CFG:MDL=X32:SN=S123456:MAC=001122334455:\n";
        memcpy(buf + 0x200, cfg, strlen(cfg));
    }
    fwrite(buf, 1, size, f);
    fclose(f);
    free(buf);
    return 0;
}
EOF
gcc -O2 -o "$SCRATCH/gen_sd" "$SCRATCH/gen_sd.c"

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT

setup_base_env() {
    local base="$1"
    mkdir -p "$base/sys/block/mmcblk0/device"
    echo "179:0" > "$base/sys/block/mmcblk0/dev"
    echo "0" > "$base/sys/block/mmcblk0/removable"
    echo "SD" > "$base/sys/block/mmcblk0/device/type"
    echo "16384" > "$base/sys/block/mmcblk0/size"

    mkdir -p "$base/sys/block/sda/sda1" "$base/sys/block/sda/device"
    echo "8:0" > "$base/sys/block/sda/dev"
    echo "1" > "$base/sys/block/sda/removable"
    echo "TestVendor" > "$base/sys/block/sda/device/vendor"
    echo "TestModel" > "$base/sys/block/sda/device/model"
    echo "8:1" > "$base/sys/block/sda/sda1/dev"

    mkdir -p "$base/dev" "$base/out"
}

run_dump() {
    local dir="$1"
    shift
    "$NATIVE_BIN" --sys-block "$dir/sys/block" --dev-dir "$dir/dev" --out "$dir/out" --no-mount "$@"
}

echo "=== Running Smoke Tests ==="

# Case 1: happy path
setup_base_env "$T/c1"
"$SCRATCH/gen_sd" "$T/c1/dev/mmcblk0" 2 8190 8388608
out_c1=$(run_dump "$T/c1" 2>&1) || { echo "FAIL: case 1 exit non-zero"; exit 1; }
echo "$out_c1" | grep -q "SUCCESS" || { echo "FAIL: case 1 missing SUCCESS"; exit 1; }
IMG="$T/c1/out/x32-dump/loader-prefix.img"
[ -f "$IMG" ] || { echo "FAIL: case 1 image missing"; exit 1; }
[ "$(stat -c %s "$IMG")" -eq 4194304 ] || { echo "FAIL: case 1 image wrong size"; exit 1; }
exp_sha=$(sha256sum "$IMG" | awk '{print $1}')
act_sha_line=$(cat "$T/c1/out/x32-dump/loader-prefix.sha256")
[ "$act_sha_line" = "$exp_sha  loader-prefix.img" ] || { echo "FAIL: case 1 sha256 mismatch"; exit 1; }
grep -q "^status=SUCCESS$" "$T/c1/out/x32-dump/manifest.txt" || { echo "FAIL: case 1 manifest status"; exit 1; }
grep -q "^model=X32$" "$T/c1/out/x32-dump/manifest.txt" || { echo "FAIL: case 1 manifest model"; exit 1; }
! grep -q "001122334455" "$T/c1/out/x32-dump/manifest.txt" || { echo "FAIL: case 1 leaked MAC"; exit 1; }
! grep -q "S123456" "$T/c1/out/x32-dump/manifest.txt" || { echo "FAIL: case 1 leaked SN"; exit 1; }
echo "PASS: case 1 (happy path)"

# Case 2: missing internal device
setup_base_env "$T/c2"
rm -rf "$T/c2/sys/block/mmcblk0"
! run_dump "$T/c2" >/dev/null 2>&1 || { echo "FAIL: case 2 expected non-zero exit"; exit 1; }
[ ! -f "$T/c2/out/x32-dump/loader-prefix.img" ] || { echo "FAIL: case 2 wrote image"; exit 1; }
echo "PASS: case 2 (missing internal device)"

# Case 3: two mmcblk SD devices
setup_base_env "$T/c3"
cp -r "$T/c3/sys/block/mmcblk0" "$T/c3/sys/block/mmcblk1"
echo "179:16" > "$T/c3/sys/block/mmcblk1/dev"
"$SCRATCH/gen_sd" "$T/c3/dev/mmcblk0" 2 8190 8388608
"$SCRATCH/gen_sd" "$T/c3/dev/mmcblk1" 2 8190 8388608
! run_dump "$T/c3" >/dev/null 2>&1 || { echo "FAIL: case 3 expected non-zero exit"; exit 1; }
[ ! -f "$T/c3/out/x32-dump/loader-prefix.img" ] || { echo "FAIL: case 3 wrote image"; exit 1; }
echo "PASS: case 3 (two mmcblk SD devices)"

# Case 4: geometry mismatch (partition1 start 2048)
setup_base_env "$T/c4"
"$SCRATCH/gen_sd" "$T/c4/dev/mmcblk0" 2048 8190 8388608
! run_dump "$T/c4" >/dev/null 2>&1 || { echo "FAIL: case 4 expected non-zero exit"; exit 1; }
[ ! -f "$T/c4/out/x32-dump/loader-prefix.img" ] || { echo "FAIL: case 4 wrote image"; exit 1; }
grep -q "^status=GEOMETRY_MISMATCH$" "$T/c4/out/x32-dump/manifest.txt" || { echo "FAIL: case 4 manifest mismatch"; exit 1; }
echo "PASS: case 4 (geometry mismatch)"

# Case 5: truncated source (file smaller than 4 MiB)
setup_base_env "$T/c5"
"$SCRATCH/gen_sd" "$T/c5/dev/mmcblk0" 2 8190 2097152
! run_dump "$T/c5" >/dev/null 2>&1 || { echo "FAIL: case 5 expected non-zero exit"; exit 1; }
[ ! -f "$T/c5/out/x32-dump/loader-prefix.img" ] || { echo "FAIL: case 5 wrote image"; exit 1; }
echo "PASS: case 5 (truncated source)"

# Case 6: no USB device
setup_base_env "$T/c6"
"$SCRATCH/gen_sd" "$T/c6/dev/mmcblk0" 2 8190 8388608
rm -rf "$T/c6/sys/block/sda"
! run_dump "$T/c6" >/dev/null 2>&1 || { echo "FAIL: case 6 expected non-zero exit"; exit 1; }
echo "PASS: case 6 (no USB device)"

# Case 7: insufficient destination space (use --max-bytes)
setup_base_env "$T/c7"
"$SCRATCH/gen_sd" "$T/c7/dev/mmcblk0" 2 8190 8388608
! run_dump "$T/c7" --max-bytes 1048576 >/dev/null 2>&1 || { echo "FAIL: case 7 expected non-zero exit"; exit 1; }
[ ! -f "$T/c7/out/x32-dump/loader-prefix.img" ] || { echo "FAIL: case 7 wrote image"; exit 1; }
echo "PASS: case 7 (insufficient destination space)"

# Case 8: internal device marked removable is not the internal card
setup_base_env "$T/c8"
"$SCRATCH/gen_sd" "$T/c8/dev/mmcblk0" 2 8190 8388608
echo "1" > "$T/c8/sys/block/mmcblk0/removable"
out_c8=$(run_dump "$T/c8" 2>&1) && { echo "FAIL: case 8 expected non-zero exit"; exit 1; }
echo "$out_c8" | grep -q "INTERNAL_SD_REMOVABLE" || { echo "FAIL: case 8 wrong reason"; exit 1; }
[ ! -f "$T/c8/out/x32-dump/loader-prefix.img" ] || { echo "FAIL: case 8 wrote image"; exit 1; }
echo "PASS: case 8 (removable internal device rejected)"

# Case 9: test-only flags are refused in production mode (before any mount is attempted)
setup_base_env "$T/c9"
"$SCRATCH/gen_sd" "$T/c9/dev/mmcblk0" 2 8190 8388608
! "$NATIVE_BIN" --sys-block "$T/c9/sys/block" --dev-dir "$T/c9/dev" --max-bytes 1048576 >/dev/null 2>&1 \
    || { echo "FAIL: case 9 expected usage error"; exit 1; }
! "$NATIVE_BIN" --sys-block "$T/c9/sys/block" --dev-dir "$T/c9/dev" --length 1024 >/dev/null 2>&1 \
    || { echo "FAIL: case 9 expected usage error for --length"; exit 1; }
echo "PASS: case 9 (test-only flags rejected in production mode)"

# Case 10: read-back verification failure must never leave a SUCCESS manifest
setup_base_env "$T/c10"
"$SCRATCH/gen_sd" "$T/c10/dev/mmcblk0" 2 8190 8388608
gcc -std=c99 -Wall -Wextra -Werror -O2 -DX32SDUMP_TEST_FORCE_VERIFY_FAIL \
    -o "$SCRATCH/x32sdump-verifyfail" "$SRC"
! "$SCRATCH/x32sdump-verifyfail" --sys-block "$T/c10/sys/block" --dev-dir "$T/c10/dev" \
    --out "$T/c10/out" --no-mount >/dev/null 2>&1 || { echo "FAIL: case 10 expected non-zero exit"; exit 1; }
grep -q "^status=VERIFY_FAILED$" "$T/c10/out/x32-dump/manifest.txt" \
    || { echo "FAIL: case 10 manifest must say VERIFY_FAILED"; exit 1; }
! grep -q "^status=SUCCESS$" "$T/c10/out/x32-dump/manifest.txt" \
    || { echo "FAIL: case 10 manifest falsely claims SUCCESS"; exit 1; }
echo "PASS: case 10 (verify failure cannot produce a SUCCESS manifest)"

# Case 11: USB device appears only after asynchronous enumeration (retry loop)
setup_base_env "$T/c11"
rm -rf "$T/c11/sys/block/sda"
"$SCRATCH/gen_sd" "$T/c11/dev/mmcblk0" 2 8190 8388608
(
  sleep 0.4
  mkdir -p "$T/c11/sys/block/sda/device" "$T/c11/sys/block/sda/sda1"
  echo "8:0" > "$T/c11/sys/block/sda/dev"
  echo "1"   > "$T/c11/sys/block/sda/removable"
  echo "8:1" > "$T/c11/sys/block/sda/sda1/dev"
) &
LATE_PID=$!
out_c11=$("$NATIVE_BIN" --sys-block "$T/c11/sys/block" --dev-dir "$T/c11/dev" \
    --out "$T/c11/out" --no-mount --wait-ms 5000 2>&1) || { wait "$LATE_PID"; echo "FAIL: case 11 exit non-zero"; exit 1; }
wait "$LATE_PID"
echo "$out_c11" | grep -q "SUCCESS" || { echo "FAIL: case 11 missing SUCCESS"; exit 1; }
echo "PASS: case 11 (waits for asynchronous USB enumeration)"

# Case 12: USB drive reporting removable=0, identified via USB sysfs ancestry
setup_base_env "$T/c12"
rm -rf "$T/c12/sys/block/sda"
DEV="$T/c12/sys/devices/platform/usb1/1-1/1-1:1.0/host0/target0:0:0/0:0:0:0"
mkdir -p "$DEV" "$T/c12/sys/block/sdb/sdb1"
ln -sfn "$DEV" "$T/c12/sys/block/sdb/device"
echo "8:16" > "$T/c12/sys/block/sdb/dev"
echo "0"    > "$T/c12/sys/block/sdb/removable"
echo "8:17" > "$T/c12/sys/block/sdb/sdb1/dev"
"$SCRATCH/gen_sd" "$T/c12/dev/mmcblk0" 2 8190 8388608
out_c12=$(run_dump "$T/c12" 2>&1) || { echo "FAIL: case 12 exit non-zero"; exit 1; }
echo "$out_c12" | grep -q "SUCCESS" || { echo "FAIL: case 12 missing SUCCESS"; exit 1; }
echo "PASS: case 12 (removable=0 USB drive found via USB ancestry)"

# Case 13: non-USB fixed sd device with removable=0 must still be rejected
setup_base_env "$T/c13"
rm -rf "$T/c13/sys/block/sda"
mkdir -p "$T/c13/sys/block/sdc/device" "$T/c13/sys/block/sdc/sdc1"
echo "8:32" > "$T/c13/sys/block/sdc/dev"
echo "0"    > "$T/c13/sys/block/sdc/removable"
echo "8:33" > "$T/c13/sys/block/sdc/sdc1/dev"
"$SCRATCH/gen_sd" "$T/c13/dev/mmcblk0" 2 8190 8388608
! run_dump "$T/c13" >/dev/null 2>&1 || { echo "FAIL: case 13 expected non-zero exit"; exit 1; }
[ ! -f "$T/c13/out/x32-dump/loader-prefix.img" ] || { echo "FAIL: case 13 wrote image"; exit 1; }
echo "PASS: case 13 (non-USB fixed disk not accepted as destination)"

echo "=== All smoke tests passed successfully! ==="
