#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <utime.h>

#define XOR_KEY 0x76
#define ENC_SUFFIX ".enc"
#define ENC_SUFFIX_LEN 4

static const char *base_path = "/home/bilaeev/SISOP-4-2026-IT-073/soal_2/encrypted_storage";

// ===================== XOR =====================

static void xor_buf(char *buf, size_t size) {
    for (size_t i = 0; i < size; i++) {
        buf[i] ^= XOR_KEY;
    }
}

// ===================== PATH HELPERS =====================

// Cek apakah path adalah directory di encrypted_storage
static int path_is_dir(const char *path) {
    char full[4096];
    snprintf(full, sizeof(full), "%s%s", base_path, path);
    struct stat st;
    return (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
}

// Build full path: directory -> langsung, file -> tambah .enc
static void build_real_path(char *out, size_t out_sz, const char *path) {
    if (strcmp(path, "/") == 0 || path_is_dir(path)) {
        snprintf(out, out_sz, "%s%s", base_path, path);
    } else {
        snprintf(out, out_sz, "%s%s%s", base_path, path, ENC_SUFFIX);
    }
}

// ===================== FUSE OPERATIONS =====================

static int xor_getattr(const char *path, struct stat *stbuf,
                       struct fuse_file_info *fi) {
    (void) fi;
    char real[4096];
    build_real_path(real, sizeof(real), path);

    memset(stbuf, 0, sizeof(struct stat));
    if (lstat(real, stbuf) == -1) return -errno;
    return 0;
}

static int xor_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi,
                       enum fuse_readdir_flags flags) {
    (void) offset; (void) fi; (void) flags;

    char real[4096];
    snprintf(real, sizeof(real), "%s%s", base_path, path);

    DIR *dp = opendir(real);
    if (!dp) return -errno;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        char name[512];
        strncpy(name, de->d_name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        // Strip .enc suffix untuk ditampilkan
        size_t len = strlen(name);
        if (len > ENC_SUFFIX_LEN &&
            strcmp(name + len - ENC_SUFFIX_LEN, ENC_SUFFIX) == 0) {
            name[len - ENC_SUFFIX_LEN] = '\0';
        }

        filler(buf, name, NULL, 0, 0);
    }

    closedir(dp);
    return 0;
}

static int xor_mkdir(const char *path, mode_t mode) {
    char real[4096];
    snprintf(real, sizeof(real), "%s%s", base_path, path);
    if (mkdir(real, mode) == -1) return -errno;
    return 0;
}

static int xor_rmdir(const char *path) {
    char real[4096];
    snprintf(real, sizeof(real), "%s%s", base_path, path);
    if (rmdir(real) == -1) return -errno;
    return 0;
}

static int xor_create(const char *path, mode_t mode,
                      struct fuse_file_info *fi) {
    char real[4096];
    snprintf(real, sizeof(real), "%s%s%s", base_path, path, ENC_SUFFIX);
    int fd = open(real, fi->flags | O_CREAT | O_TRUNC, mode);
    if (fd == -1) return -errno;
    fi->fh = fd;
    return 0;
}

static int xor_open(const char *path, struct fuse_file_info *fi) {
    char real[4096];
    snprintf(real, sizeof(real), "%s%s%s", base_path, path, ENC_SUFFIX);
    int fd = open(real, fi->flags);
    if (fd == -1) return -errno;
    fi->fh = fd;
    return 0;
}

static int xor_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi) {
    (void) path;
    int res = pread(fi->fh, buf, size, offset);
    if (res == -1) return -errno;
    xor_buf(buf, res);
    return res;
}

static int xor_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi) {
    (void) path;
    char *enc = malloc(size);
    if (!enc) return -ENOMEM;
    memcpy(enc, buf, size);
    xor_buf(enc, size);
    int res = pwrite(fi->fh, enc, size, offset);
    free(enc);
    if (res == -1) return -errno;
    return res;
}

static int xor_truncate(const char *path, off_t size,
                        struct fuse_file_info *fi) {
    if (fi && fi->fh) {
        if (ftruncate(fi->fh, size) == -1) return -errno;
        return 0;
    }
    char real[4096];
    snprintf(real, sizeof(real), "%s%s%s", base_path, path, ENC_SUFFIX);
    if (truncate(real, size) == -1) return -errno;
    return 0;
}

static int xor_unlink(const char *path) {
    char real[4096];
    snprintf(real, sizeof(real), "%s%s%s", base_path, path, ENC_SUFFIX);
    if (unlink(real) == -1) return -errno;
    return 0;
}

static int xor_access(const char *path, int mask) {
    char real[4096];
    build_real_path(real, sizeof(real), path);
    if (access(real, mask) == -1) return -errno;
    return 0;
}

static int xor_utimens(const char *path, const struct timespec ts[2],
                       struct fuse_file_info *fi) {
    (void) fi;
    char real[4096];
    build_real_path(real, sizeof(real), path);
    if (utimensat(0, real, ts, 0) == -1) return -errno;
    return 0;
}

static int xor_release(const char *path, struct fuse_file_info *fi) {
    (void) path;
    close(fi->fh);
    return 0;
}

// ===================== OPERATIONS STRUCT =====================

static struct fuse_operations xor_ops = {
    .getattr  = xor_getattr,
    .readdir  = xor_readdir,
    .mkdir    = xor_mkdir,
    .rmdir    = xor_rmdir,
    .create   = xor_create,
    .open     = xor_open,
    .read     = xor_read,
    .write    = xor_write,
    .truncate = xor_truncate,
    .unlink   = xor_unlink,
    .access   = xor_access,
    .utimens  = xor_utimens,
    .release  = xor_release,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &xor_ops, NULL);
}
