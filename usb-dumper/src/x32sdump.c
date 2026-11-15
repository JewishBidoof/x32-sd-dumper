// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <sys/ioctl.h>

#ifndef UTILITY_VERSION
#define UTILITY_VERSION "1.0.0"
#endif

#ifndef BUILD_COMMIT
#define BUILD_COMMIT "unknown"
#endif

#ifndef MNT_DETACH
#define MNT_DETACH 2
#endif

#ifndef BLKGETSIZE64
#define BLKGETSIZE64 _IOR(0x12, 114, size_t)
#endif

#ifndef BLKSSZGET
#define BLKSSZGET _IO(0x12, 104)
#endif

#define PATH_BUF_LEN (PATH_MAX * 2)

/* --- SHA-256 implementation --- */

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} sha256_ctx;

static inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22))
#define EP1(x) (rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25))
#define SIG0(x) (rotr32(x, 7) ^ rotr32(x, 18) ^ ((x) >> 3))
#define SIG1(x) (rotr32(x, 17) ^ rotr32(x, 19) ^ ((x) >> 10))

static const uint32_t k256[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

static void sha256_transform(sha256_ctx *ctx, const uint8_t data[64]) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    for (int i = 0; i < 16; ++i) {
        m[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];
    for (int i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e, f, g) + k256[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(sha256_ctx *ctx) {
    ctx->state[0] = 0x6a09e667UL;
    ctx->state[1] = 0xbb67ae85UL;
    ctx->state[2] = 0x3c6ef372UL;
    ctx->state[3] = 0xa54ff53aUL;
    ctx->state[4] = 0x510e527fUL;
    ctx->state[5] = 0x9b05688cUL;
    ctx->state[6] = 0x1f83d9abUL;
    ctx->state[7] = 0x5be0cd19UL;
    ctx->count = 0;
}

static void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len) {
    size_t buf_idx = (size_t)(ctx->count % 64);
    ctx->count += len;
    size_t i = 0;
    if (buf_idx > 0) {
        size_t need = 64 - buf_idx;
        if (len < need) {
            memcpy(ctx->buffer + buf_idx, data, len);
            return;
        }
        memcpy(ctx->buffer + buf_idx, data, need);
        sha256_transform(ctx, ctx->buffer);
        i += need;
    }
    while (i + 64 <= len) {
        sha256_transform(ctx, data + i);
        i += 64;
    }
    if (i < len) {
        memcpy(ctx->buffer, data + i, len - i);
    }
}

static void sha256_final(sha256_ctx *ctx, uint8_t hash[32]) {
    uint8_t pad = 0x80;
    uint64_t total_bits = ctx->count * 8;
    sha256_update(ctx, &pad, 1);
    size_t buf_idx = (size_t)(ctx->count % 64);
    if (buf_idx > 56) {
        uint8_t zeros[64] = {0};
        sha256_update(ctx, zeros, 64 - buf_idx);
    }
    buf_idx = (size_t)(ctx->count % 64);
    if (buf_idx < 56) {
        uint8_t zeros[64] = {0};
        sha256_update(ctx, zeros, 56 - buf_idx);
    }
    uint8_t len_bytes[8];
    for (int i = 0; i < 8; ++i) {
        len_bytes[i] = (uint8_t)((total_bits >> (56 - i * 8)) & 0xff);
    }
    sha256_update(ctx, len_bytes, 8);
    for (int i = 0; i < 8; ++i) {
        hash[i * 4]     = (uint8_t)((ctx->state[i] >> 24) & 0xff);
        hash[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xff);
        hash[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 8) & 0xff);
        hash[i * 4 + 3] = (uint8_t)(ctx->state[i] & 0xff);
    }
}

static void bytes_to_hex(const uint8_t *in, size_t len, char *out) {
    static const char hex_digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out[i * 2]     = hex_digits[(in[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex_digits[in[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

/* --- Console output --- */

static void console_print(const char *msg) {
    printf("%s\n", msg);
    fflush(stdout);

    int fd = open("/dev/tty0", O_WRONLY | O_NOCTTY);
    if (fd >= 0) {
        size_t len = strlen(msg);
        (void)write(fd, msg, len);
        (void)write(fd, "\n", 1);
        close(fd);
    }
}

/* --- Cleanup and exit / pause loop --- */

static void finish(bool success, const char *failure_reason, bool no_mount, bool mounted, const char *mnt_dir) {
    if (mounted && mnt_dir) {
        sync();
        (void)umount2(mnt_dir, MNT_DETACH);
    }

    if (success) {
        console_print("SUCCESS");
    } else {
        char fail_buf[128];
        snprintf(fail_buf, sizeof(fail_buf), "FAILURE: %s", failure_reason ? failure_reason : "UNKNOWN");
        console_print(fail_buf);
    }

    if (no_mount) {
        exit(success ? 0 : 1);
    } else {
        sync();
        for (;;) {
            pause();
        }
    }
}

/* --- Helper functions --- */

static bool read_file_string(const char *path, char *buf, size_t max_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    ssize_t n = read(fd, buf, max_len - 1);
    close(fd);
    if (n < 0) return false;
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ' || buf[n - 1] == '\t')) {
        buf[--n] = '\0';
    }
    return true;
}

static bool path_is_usb(const char *sys_block_dir, const char *name) {
    /*
     * Many USB flash drives and USB SSD bridges report the SCSI RMB bit as 0,
     * so /sys/block/sdX/removable is "0" even though the device is on the USB
     * bus. Fall back to the device's sysfs ancestry: USB mass storage always
     * sits below a "usbN" node.
     */
    char path[PATH_BUF_LEN];
    char resolved[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s/device", sys_block_dir, name);
    if (!realpath(path, resolved)) return false;
    const char *p = resolved;
    while ((p = strstr(p, "/usb")) != NULL) {
        char c = p[4];
        if (c == '/' || (c >= '0' && c <= '9')) return true;
        p += 4;
    }
    return false;
}

typedef struct {
    char internal[256];
    int internal_count;   /* -1 means /sys/block could not be opened */
    char usb[256];
    int usb_count;
} devscan;

static void scan_devices(const char *sys_block_dir, devscan *s) {
    memset(s, 0, sizeof(*s));
    DIR *dir = opendir(sys_block_dir);
    if (!dir) {
        s->internal_count = -1;
        return;
    }
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;

        if (strncmp(de->d_name, "mmcblk", 6) == 0) {
            char type_path[PATH_BUF_LEN];
            char type_val[64];
            snprintf(type_path, sizeof(type_path), "%s/%s/device/type", sys_block_dir, de->d_name);
            if (read_file_string(type_path, type_val, sizeof(type_val)) &&
                strcmp(type_val, "SD") == 0) {
                s->internal_count++;
                snprintf(s->internal, sizeof(s->internal), "%s", de->d_name);
            }
        }

        if (strncmp(de->d_name, "sd", 2) == 0) {
            bool candidate = false;
            char rem_path[PATH_BUF_LEN];
            char rem_val[64];
            snprintf(rem_path, sizeof(rem_path), "%s/%s/removable", sys_block_dir, de->d_name);
            if (read_file_string(rem_path, rem_val, sizeof(rem_val)) &&
                strcmp(rem_val, "1") == 0) {
                candidate = true;
            }
            if (!candidate && path_is_usb(sys_block_dir, de->d_name)) {
                candidate = true;
            }
            if (candidate) {
                s->usb_count++;
                snprintf(s->usb, sizeof(s->usb), "%s", de->d_name);
            }
        }
    }
    closedir(dir);
}

static void ensure_mounts(void) {
    (void)mkdir("/dev", 0755);
    (void)mkdir("/proc", 0755);
    (void)mkdir("/sys", 0755);

    if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) != 0) {
        /* Ignore EBUSY/EEXIST */
    }
    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        /* Ignore EBUSY/EEXIST */
    }
    if (mount("sysfs", "/sys", "sysfs", 0, NULL) != 0) {
        /* Ignore EBUSY/EEXIST */
    }
}

static void get_timestamp_utc(char *buf, size_t buf_len) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        struct tm tm_utc;
        if (gmtime_r(&ts.tv_sec, &tm_utc) != NULL) {
            int year = tm_utc.tm_year + 1900;
            if (year >= 2020 && year <= 2100) {
                strftime(buf, buf_len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
                return;
            }
        }
    }
    snprintf(buf, buf_len, "unreliable");
}

static void parse_model(const uint8_t *sec_data, size_t data_len, char *model_out, size_t out_len) {
    snprintf(model_out, out_len, "unknown");
    if (!sec_data || data_len < 4) return;
    if (memcmp(sec_data, ":CFG", 4) != 0) return;

    for (size_t i = 0; i + 5 <= data_len; i++) {
        if (memcmp(sec_data + i, ":MDL=", 5) == 0) {
            size_t start = i + 5;
            size_t end = start;
            while (end < data_len && sec_data[end] != ':' && sec_data[end] != '\0' &&
                   sec_data[end] != '\r' && sec_data[end] != '\n') {
                end++;
            }
            size_t len = end - start;
            if (len > 0 && len < out_len) {
                memcpy(model_out, sec_data + start, len);
                model_out[len] = '\0';
            }
            return;
        }
    }
}

static uint64_t get_device_size(int fd, const char *sys_block_dev_dir) {
    uint64_t size_bytes = 0;
#ifdef BLKGETSIZE64
    if (ioctl(fd, BLKGETSIZE64, &size_bytes) == 0 && size_bytes > 0) {
        return size_bytes;
    }
#endif
    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
        return (uint64_t)st.st_size;
    }
    if (sys_block_dev_dir) {
        char size_path[PATH_BUF_LEN];
        snprintf(size_path, sizeof(size_path), "%.4000s/size", sys_block_dev_dir);
        char size_str[64];
        if (read_file_string(size_path, size_str, sizeof(size_str))) {
            uint64_t sectors = (uint64_t)strtoull(size_str, NULL, 10);
            if (sectors > 0) {
                return sectors * 512;
            }
        }
    }
    return 0;
}

static uint32_t get_sector_size(int fd) {
    uint32_t ssz = 512;
#ifdef BLKSSZGET
    int val = 0;
    if (ioctl(fd, BLKSSZGET, &val) == 0 && val > 0) {
        ssz = (uint32_t)val;
    }
#else
    (void)fd;
#endif
    return ssz;
}

static bool mkdir_p(const char *path) {
    char tmp[PATH_BUF_LEN];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return false;
    memcpy(tmp, path, len + 1);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return false;
    }
    return true;
}

/*
 * Mount the USB stick's FAT filesystem, trying each existing partition before
 * falling back to a superfloppy (whole-device) layout. fstype is pinned to
 * "vfat", so a non-FAT partition simply fails to mount rather than being
 * mounted read-write by some other filesystem driver.
 */
static bool mount_usb_vfat(const char *sys_block_dir, const char *dev_dir,
                           const char *usb, const char *mnt_dir) {
    char cand[17][PATH_BUF_LEN];
    int n = 0;

    for (int i = 1; i <= 15 && n < 16; i++) {
        char sys_part[PATH_BUF_LEN];
        struct stat st;
        snprintf(sys_part, sizeof(sys_part), "%s/%s/%s%d", sys_block_dir, usb, usb, i);
        if (stat(sys_part, &st) != 0) continue;
        snprintf(cand[n], sizeof(cand[n]), "%s/%s%d", dev_dir, usb, i);
        n++;
    }
    snprintf(cand[n], sizeof(cand[n]), "%s/%s", dev_dir, usb);
    n++;

    for (int i = 0; i < n; i++) {
        if (mount(cand[i], mnt_dir, "vfat", 0, NULL) == 0) {
            return true;
        }
    }
    return false;
}

static bool write_manifest(const char *manifest_path,
                           const char *utility_ver,
                           const char *build_commit,
                           const char *timestamp_utc,
                           const char *model,
                           uint64_t storage_bytes,
                           uint32_t sector_size,
                           uint32_t part1_start_lba,
                           uint32_t part1_sectors,
                           size_t image_length,
                           const char *sha256_hex,
                           const char *status) {
    int fd = open(manifest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    char buf[1024];
    int len = snprintf(buf, sizeof(buf),
                       "utility_version=%s\n"
                       "build_commit=%s\n"
                       "timestamp_utc=%s\n"
                       "model=%s\n"
                       "storage_bytes=%llu\n"
                       "sector_size=%u\n"
                       "partition1_start_lba=%u\n"
                       "partition1_sectors=%u\n"
                       "image_length=%zu\n"
                       "sha256=%s\n"
                       "status=%s\n",
                       utility_ver,
                       build_commit,
                       timestamp_utc,
                       model,
                       (unsigned long long)storage_bytes,
                       (unsigned int)sector_size,
                       (unsigned int)part1_start_lba,
                       (unsigned int)part1_sectors,
                       image_length,
                       sha256_hex,
                       status);
    if (len <= 0) {
        close(fd);
        return false;
    }
    ssize_t written = 0;
    while (written < len) {
        ssize_t w = write(fd, buf + written, len - written);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return false;
        }
        written += w;
    }
    (void)fsync(fd);
    close(fd);
    return true;
}

int main(int argc, char *argv[]) {
    const char *sys_block_dir = "/sys/block";
    const char *dev_dir = "/dev";
    const char *mnt_dir = "/mnt/usb";
    const char *out_dir = NULL;
    bool no_mount = false;
    size_t image_length = 4194304; /* 4 MiB */
    bool length_specified = false;
    uint64_t max_bytes = 0;
    bool max_bytes_specified = false;
    long wait_ms = -1;   /* resolved after argument parsing */

    /* Check environment variable defaults */
    const char *env_sys = getenv("X32SDUMP_SYSBLOCK");
    if (env_sys && env_sys[0]) sys_block_dir = env_sys;

    const char *env_dev = getenv("X32SDUMP_DEVDIR");
    if (env_dev && env_dev[0]) dev_dir = env_dev;

    const char *env_mnt = getenv("X32SDUMP_MNT");
    if (env_mnt && env_mnt[0]) mnt_dir = env_mnt;

    /* Parse CLI flags */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sys-block") == 0 && i + 1 < argc) {
            sys_block_dir = argv[++i];
        } else if (strcmp(argv[i], "--dev-dir") == 0 && i + 1 < argc) {
            dev_dir = argv[++i];
        } else if (strcmp(argv[i], "--mnt") == 0 && i + 1 < argc) {
            mnt_dir = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "--no-mount") == 0) {
            no_mount = true;
        } else if (strcmp(argv[i], "--length") == 0 && i + 1 < argc) {
            image_length = (size_t)strtoull(argv[++i], NULL, 10);
            length_specified = true;
        } else if (strcmp(argv[i], "--max-bytes") == 0 && i + 1 < argc) {
            max_bytes = (uint64_t)strtoull(argv[++i], NULL, 10);
            max_bytes_specified = true;
        } else if (strcmp(argv[i], "--wait-ms") == 0 && i + 1 < argc) {
            wait_ms = strtol(argv[++i], NULL, 10);
            if (wait_ms < 0) wait_ms = 0;
        } else {
            fprintf(stderr, "Usage error: unknown or incomplete argument '%s'\n", argv[i]);
            return 1;
        }
    }

    if (length_specified && !no_mount) {
        fprintf(stderr, "Usage error: --length is ONLY honoured together with --no-mount\n");
        return 1;
    }

    /* Test-only space simulation must never be able to mask a real space check. */
    if (max_bytes_specified && !no_mount) {
        fprintf(stderr, "Usage error: --max-bytes is ONLY honoured together with --no-mount\n");
        return 1;
    }

    if (wait_ms < 0) {
        /* USB enumeration is asynchronous; only real boots need to wait. */
        wait_ms = no_mount ? 0 : 15000;
    }

    /* Print startup banner */
    console_print("X32 SD DUMPER");

    /* Step 1: Ensure standard mount points */
    if (!no_mount) {
        ensure_mounts();
    }

    /* Step 2 & 3: Scan sysfs for internal SD and USB destination.
     *
     * /init runs milliseconds after the kernel starts. USB host enumeration,
     * SCSI probe and sd block registration are asynchronous and can take
     * seconds on this SoC, so a single scan at t=0 would report NO_USB on
     * perfectly good hardware. Poll until both devices are unambiguous or the
     * bounded wait expires.
     */
    devscan sc;
    struct timespec t0, tn;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        scan_devices(sys_block_dir, &sc);
        if (sc.internal_count < 0) {
            finish(false, "OPEN_SYSBLOCK_FAILED", no_mount, false, mnt_dir);
        }
        if ((sc.internal_count == 1 && sc.usb_count == 1) ||
            sc.internal_count > 1 || sc.usb_count > 1) {
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &tn);
        long elapsed_ms = (tn.tv_sec - t0.tv_sec) * 1000L +
                          (tn.tv_nsec - t0.tv_nsec) / 1000000L;
        if (elapsed_ms >= wait_ms) break;
        usleep(200000);
    }

    if (sc.internal_count == 0) {
        finish(false, "NO_INTERNAL_SD", no_mount, false, mnt_dir);
    }
    if (sc.internal_count > 1) {
        finish(false, "MULTIPLE_INTERNAL_SD", no_mount, false, mnt_dir);
    }
    if (sc.usb_count == 0) {
        finish(false, "NO_USB", no_mount, false, mnt_dir);
    }
    if (sc.usb_count > 1) {
        finish(false, "MULTIPLE_USB", no_mount, false, mnt_dir);
    }

    /*
     * The console's internal card is wired as non-removable (DTB: &esdhc1
     * non-removable). Upstream Linux never sets GENHD_FL_REMOVABLE for MMC
     * block devices, so this reports "0" on standard kernels and is
     * defence-in-depth only: it cannot be used to prove non-removability.
     */
    {
        char int_rem_path[PATH_BUF_LEN];
        char int_rem_val[64];
        snprintf(int_rem_path, sizeof(int_rem_path), "%s/%s/removable", sys_block_dir, sc.internal);
        if (read_file_string(int_rem_path, int_rem_val, sizeof(int_rem_val)) &&
            strcmp(int_rem_val, "1") == 0) {
            finish(false, "INTERNAL_SD_REMOVABLE", no_mount, false, mnt_dir);
        }
    }

    /* Step 4: Ensure internal and USB do not resolve to the same device */
    char int_dev_file[PATH_BUF_LEN], usb_dev_file[PATH_BUF_LEN];
    snprintf(int_dev_file, sizeof(int_dev_file), "%s/%s/dev", sys_block_dir, sc.internal);
    snprintf(usb_dev_file, sizeof(usb_dev_file), "%s/%s/dev", sys_block_dir, sc.usb);
    char int_majmin[64] = {0}, usb_majmin[64] = {0};
    bool got_int_dev = read_file_string(int_dev_file, int_majmin, sizeof(int_majmin));
    bool got_usb_dev = read_file_string(usb_dev_file, usb_majmin, sizeof(usb_majmin));
    if (got_int_dev && got_usb_dev && strcmp(int_majmin, usb_majmin) == 0) {
        finish(false, "SAME_DEVICE", no_mount, false, mnt_dir);
    }

    char int_block_dir[PATH_BUF_LEN], usb_block_dir[PATH_BUF_LEN];
    snprintf(int_block_dir, sizeof(int_block_dir), "%s/%s", sys_block_dir, sc.internal);
    snprintf(usb_block_dir, sizeof(usb_block_dir), "%s/%s", sys_block_dir, sc.usb);
    char res_int[PATH_MAX], res_usb[PATH_MAX];
    if (realpath(int_block_dir, res_int) && realpath(usb_block_dir, res_usb)) {
        if (strcmp(res_int, res_usb) == 0) {
            finish(false, "SAME_DEVICE", no_mount, false, mnt_dir);
        }
    }

    console_print("INTERNAL SD FOUND");

    /* Step 5: Open internal device O_RDONLY (read-only, never write, never mount) */
    char int_dev_path[PATH_BUF_LEN];
    snprintf(int_dev_path, sizeof(int_dev_path), "%s/%s", dev_dir, sc.internal);
    int int_fd = open(int_dev_path, O_RDONLY);
    if (int_fd < 0) {
        finish(false, "OPEN_INTERNAL_FAILED", no_mount, false, mnt_dir);
    }

    /* Step 6: pread 512 bytes at offset 0. Require 0x55AA at 510. */
    uint8_t sector0[512];
    ssize_t n0 = pread(int_fd, sector0, sizeof(sector0), 0);
    if (n0 != (ssize_t)sizeof(sector0)) {
        close(int_fd);
        finish(false, "SHORT_READ", no_mount, false, mnt_dir);
    }

    uint16_t magic = (uint16_t)sector0[510] | ((uint16_t)sector0[511] << 8);
    uint32_t part1_start_lba = (uint32_t)sector0[0x1BE + 8] |
                               ((uint32_t)sector0[0x1BE + 9] << 8) |
                               ((uint32_t)sector0[0x1BE + 10] << 16) |
                               ((uint32_t)sector0[0x1BE + 11] << 24);
    uint32_t part1_sectors = (uint32_t)sector0[0x1BE + 12] |
                             ((uint32_t)sector0[0x1BE + 13] << 8) |
                             ((uint32_t)sector0[0x1BE + 14] << 16) |
                             ((uint32_t)sector0[0x1BE + 15] << 24);

    bool geometry_mismatch = (magic != 0xAA55 || part1_start_lba != 2 || part1_sectors != 8190);

    /* Get model and device size info */
    char model[64] = "unknown";
    uint8_t cfg_sector[512] = {0};
    if (pread(int_fd, cfg_sector, sizeof(cfg_sector), 0x200) == (ssize_t)sizeof(cfg_sector)) {
        parse_model(cfg_sector, sizeof(cfg_sector), model, sizeof(model));
    }
    uint64_t storage_bytes = get_device_size(int_fd, int_block_dir);
    uint32_t sector_size = get_sector_size(int_fd);

    char timestamp_utc[64];
    get_timestamp_utc(timestamp_utc, sizeof(timestamp_utc));

    /* Destination directory setup */
    const char *dest_root = (no_mount && out_dir) ? out_dir : mnt_dir;
    char dump_dir[PATH_BUF_LEN];
    size_t root_len = strlen(dest_root);
    if (root_len >= 8 && strcmp(dest_root + root_len - 8, "x32-dump") == 0) {
        snprintf(dump_dir, sizeof(dump_dir), "%s", dest_root);
    } else {
        snprintf(dump_dir, sizeof(dump_dir), "%s/x32-dump", dest_root);
    }

    bool mounted = false;

    /* Handle GEOMETRY_MISMATCH: write ONLY the manifest, write NO image, FAIL */
    if (geometry_mismatch) {
        close(int_fd);
        int_fd = -1;

        if (!no_mount) {
            (void)mkdir_p(mnt_dir);
            if (!mount_usb_vfat(sys_block_dir, dev_dir, sc.usb, mnt_dir)) {
                finish(false, "MOUNT_FAILED", no_mount, false, mnt_dir);
            }
            mounted = true;
        }

        /* Check destination space */
        struct statvfs sv;
        uint64_t free_bytes = 0;
        if (statvfs(dest_root, &sv) == 0) {
            free_bytes = (uint64_t)sv.f_bavail * sv.f_frsize;
        }
        if (max_bytes_specified) {
            free_bytes = max_bytes;
        }
        if (free_bytes < 8ULL * 1024ULL * 1024ULL) {
            finish(false, "NO_SPACE", no_mount, mounted, mnt_dir);
        }

        if (!mkdir_p(dump_dir)) {
            finish(false, "MKDIR_FAILED", no_mount, mounted, mnt_dir);
        }

        char manifest_path[PATH_BUF_LEN];
        snprintf(manifest_path, sizeof(manifest_path), "%.4000s/manifest.txt", dump_dir);
        if (!write_manifest(manifest_path, UTILITY_VERSION, BUILD_COMMIT, timestamp_utc,
                            model, storage_bytes, sector_size,
                            part1_start_lba, part1_sectors,
                            0, "none", "GEOMETRY_MISMATCH")) {
            finish(false, "WRITE_MANIFEST_FAILED", no_mount, mounted, mnt_dir);
        }

        int dir_fd = open(dump_dir, O_RDONLY | O_DIRECTORY);
        if (dir_fd >= 0) {
            (void)fsync(dir_fd);
            close(dir_fd);
        }

        finish(false, "GEOMETRY_MISMATCH", no_mount, mounted, mnt_dir);
    }

    /* Step 7: pread image_length bytes into malloc'd buffer */
    console_print("READING...");
    uint8_t *img_buf = malloc(image_length);
    if (!img_buf) {
        close(int_fd);
        finish(false, "MALLOC_FAILED", no_mount, false, mnt_dir);
    }

    size_t total_read = 0;
    while (total_read < image_length) {
        ssize_t r = pread(int_fd, img_buf + total_read, image_length - total_read, (off_t)total_read);
        if (r < 0) {
            if (errno == EINTR) continue;
            free(img_buf);
            close(int_fd);
            finish(false, "SHORT_READ", no_mount, false, mnt_dir);
        }
        if (r == 0) {
            free(img_buf);
            close(int_fd);
            finish(false, "SHORT_READ", no_mount, false, mnt_dir);
        }
        total_read += (size_t)r;
    }

    /* Extract model if buffer covers offset 0x200 */
    if (image_length >= 0x200 + 512) {
        parse_model(img_buf + 0x200, 512, model, sizeof(model));
    }

    /* Step 8: Compute SHA-256 over buffer */
    sha256_ctx sctx;
    sha256_init(&sctx);
    sha256_update(&sctx, img_buf, image_length);
    uint8_t hash[32];
    sha256_final(&sctx, hash);
    char hash_hex[65];
    bytes_to_hex(hash, 32, hash_hex);

    /* Step 9: CLOSE the internal device before touching USB */
    close(int_fd);
    int_fd = -1;

    /* Step 10: Mount USB partition writable at /mnt/usb with fstype "vfat" */
    if (!no_mount) {
        (void)mkdir_p(mnt_dir);
        if (!mount_usb_vfat(sys_block_dir, dev_dir, sc.usb, mnt_dir)) {
            free(img_buf);
            finish(false, "MOUNT_FAILED", no_mount, false, mnt_dir);
        }
        mounted = true;
    }

    /* Step 11: statvfs: require >= 8 MiB free, else status=NO_SPACE, FAIL before writing */
    struct statvfs sv;
    uint64_t free_bytes = 0;
    if (statvfs(dest_root, &sv) == 0) {
        free_bytes = (uint64_t)sv.f_bavail * sv.f_frsize;
    }
    if (max_bytes_specified) {
        free_bytes = max_bytes;
    }
    if (free_bytes < 8ULL * 1024ULL * 1024ULL) {
        free(img_buf);
        finish(false, "NO_SPACE", no_mount, mounted, mnt_dir);
    }

    /* Step 12: mkdir <dest_root>/x32-dump and write files */
    if (!mkdir_p(dump_dir)) {
        free(img_buf);
        finish(false, "MKDIR_FAILED", no_mount, mounted, mnt_dir);
    }

    int dir_fd = open(dump_dir, O_RDONLY | O_DIRECTORY);

    /* Write loader-prefix.img via temp file -> fsync -> rename -> fsync(dir) */
    char img_tmp[PATH_BUF_LEN], img_final[PATH_BUF_LEN];
    snprintf(img_tmp, sizeof(img_tmp), "%.4000s/loader-prefix.img.tmp", dump_dir);
    snprintf(img_final, sizeof(img_final), "%.4000s/loader-prefix.img", dump_dir);

    int img_fd = open(img_tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (img_fd < 0) {
        free(img_buf);
        if (dir_fd >= 0) close(dir_fd);
        finish(false, "WRITE_IMG_FAILED", no_mount, mounted, mnt_dir);
    }

    size_t written = 0;
    while (written < image_length) {
        ssize_t w = write(img_fd, img_buf + written, image_length - written);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(img_fd);
            unlink(img_tmp);
            free(img_buf);
            if (dir_fd >= 0) close(dir_fd);
            finish(false, "WRITE_IMG_FAILED", no_mount, mounted, mnt_dir);
        }
        written += (size_t)w;
    }
    if (fsync(img_fd) != 0) {
        close(img_fd);
        unlink(img_tmp);
        free(img_buf);
        if (dir_fd >= 0) close(dir_fd);
        finish(false, "FSYNC_IMG_FAILED", no_mount, mounted, mnt_dir);
    }
    close(img_fd);

    if (rename(img_tmp, img_final) != 0) {
        unlink(img_tmp);
        free(img_buf);
        if (dir_fd >= 0) close(dir_fd);
        finish(false, "RENAME_IMG_FAILED", no_mount, mounted, mnt_dir);
    }

    if (dir_fd >= 0) {
        (void)fsync(dir_fd);
    }

    /* Write loader-prefix.sha256 */
    char sha_path[PATH_BUF_LEN];
    snprintf(sha_path, sizeof(sha_path), "%.4000s/loader-prefix.sha256", dump_dir);
    int sha_fd = open(sha_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (sha_fd < 0) {
        free(img_buf);
        if (dir_fd >= 0) close(dir_fd);
        finish(false, "WRITE_SHA_FAILED", no_mount, mounted, mnt_dir);
    }
    char sha_line[128];
    int sha_len = snprintf(sha_line, sizeof(sha_line), "%s  loader-prefix.img\n", hash_hex);
    written = 0;
    while ((int)written < sha_len) {
        ssize_t w = write(sha_fd, sha_line + written, sha_len - written);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(sha_fd);
            free(img_buf);
            if (dir_fd >= 0) close(dir_fd);
            finish(false, "WRITE_SHA_FAILED", no_mount, mounted, mnt_dir);
        }
        written += (size_t)w;
    }
    (void)fsync(sha_fd);
    close(sha_fd);

    if (dir_fd >= 0) {
        (void)fsync(dir_fd);
    }

    if (dir_fd >= 0) {
        (void)fsync(dir_fd);
        close(dir_fd);
        dir_fd = -1;
    }

    /* Step 13: fsync everything, re-open the written image read-only, recompute SHA-256, compare */
    console_print("VERIFYING...");
    sync();

    bool verify_ok = false;
    int verify_fd = open(img_final, O_RDONLY);
    if (verify_fd >= 0) {
        sha256_ctx vctx;
        sha256_init(&vctx);
        uint8_t vbuf[65536];
        size_t vtotal = 0;
        bool read_error = false;
        while (vtotal < image_length) {
            ssize_t r = read(verify_fd, vbuf, sizeof(vbuf));
            if (r < 0) {
                if (errno == EINTR) continue;
                read_error = true;
                break;
            }
            if (r == 0) break;
            sha256_update(&vctx, vbuf, (size_t)r);
            vtotal += (size_t)r;
        }
        close(verify_fd);
        if (!read_error && vtotal == image_length) {
            uint8_t vhash[32];
            sha256_final(&vctx, vhash);
            verify_ok = (memcmp(hash, vhash, 32) == 0);
        }
    }

    free(img_buf);
    img_buf = NULL;

#ifdef X32SDUMP_TEST_FORCE_VERIFY_FAIL
    /* Test-only: never defined in production builds. Proves the failure path below. */
    verify_ok = false;
#endif

    /*
     * The manifest is written LAST so that its status can never claim SUCCESS for
     * an image that failed read-back verification.
     */
    char manifest_path[PATH_BUF_LEN];
    snprintf(manifest_path, sizeof(manifest_path), "%.4000s/manifest.txt", dump_dir);
    if (!write_manifest(manifest_path, UTILITY_VERSION, BUILD_COMMIT, timestamp_utc,
                        model, storage_bytes, sector_size,
                        part1_start_lba, part1_sectors,
                        image_length, hash_hex,
                        verify_ok ? "SUCCESS" : "VERIFY_FAILED")) {
        finish(false, "WRITE_MANIFEST_FAILED", no_mount, mounted, mnt_dir);
    }

    {
        int final_dir_fd = open(dump_dir, O_RDONLY | O_DIRECTORY);
        if (final_dir_fd >= 0) {
            (void)fsync(final_dir_fd);
            close(final_dir_fd);
        }
    }

    if (!verify_ok) {
        finish(false, "VERIFY_FAILED", no_mount, mounted, mnt_dir);
    }

    /* Step 14 & 15 & 16: Finish with SUCCESS */
    finish(true, NULL, no_mount, mounted, mnt_dir);

    return 0;
}
