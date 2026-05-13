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

// Source directory path (amba_files/)
static char source_dir[4096];

// ============================================================
// Helper: build full path ke source directory
// ============================================================
static void build_path(char *dest, const char *path) {
    snprintf(dest, 4096, "%s%s", source_dir, path);
}

// ============================================================
// Helper: generate isi tujuan.txt secara on-the-fly
// Baca KOORD: dari tiap file 1.txt..7.txt, gabungkan
// ============================================================
static char *generate_tujuan(size_t *out_len) {
    char combined[4096] = "";

    for (int i = 1; i <= 7; i++) {
        char filepath[4096];
        snprintf(filepath, sizeof(filepath), "%s/%d.txt", source_dir, i);

        FILE *f = fopen(filepath, "r");
        if (!f) continue;

        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "KOORD:", 6) == 0) {
                // Ambil bagian setelah "KOORD: "
                char *fragment = line + 6;
                // Trim leading space
                while (*fragment == ' ') fragment++;
                // Trim trailing newline
                size_t len = strlen(fragment);
                while (len > 0 && (fragment[len-1] == '\n' || fragment[len-1] == '\r'))
                    fragment[--len] = '\0';

                // Append ke combined (tanpa spasi antar fragmen)
                strncat(combined, fragment, sizeof(combined) - strlen(combined) - 1);
                break;
            }
        }
        fclose(f);
    }

    // Format akhir: "Tujuan Mas Amba: <gabungan>\n"
    char *result = malloc(4096);
    int written = snprintf(result, 4096, "Tujuan Mas Amba: %s\n", combined);
    *out_len = (size_t)written;
    return result;
}

// ============================================================
// FUSE CALLBACKS
// ============================================================

static int kenz_getattr(const char *path, struct stat *stbuf,
                        struct fuse_file_info *fi) {
    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));

    // Virtual file: tujuan.txt
    if (strcmp(path, "/tujuan.txt") == 0) {
        size_t len;
        char *content = generate_tujuan(&len);
        free(content);

        stbuf->st_mode  = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size  = (off_t)len;
        stbuf->st_uid   = 0;
        stbuf->st_gid   = 0;
        stbuf->st_atime = 0;
        stbuf->st_mtime = 0;
        stbuf->st_ctime = 0;
        return 0;
    }

    // Root directory
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode  = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    // Passthrough: file di source_dir
    char full_path[4096];
    build_path(full_path, path);

    if (lstat(full_path, stbuf) == -1)
        return -errno;

    return 0;
}

static int kenz_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi,
                        enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    // List file dari source_dir
    char full_path[4096];
    snprintf(full_path, sizeof(full_path), "%s", source_dir);

    DIR *dp = opendir(full_path);
    if (!dp) return -errno;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (de->d_name[0] == '.') continue; // skip hidden
        filler(buf, de->d_name, NULL, 0, 0);
    }
    closedir(dp);

    // Tambahkan virtual file
    filler(buf, "tujuan.txt", NULL, 0, 0);

    return 0;
}

static int kenz_open(const char *path, struct fuse_file_info *fi) {
    // Virtual file
    if (strcmp(path, "/tujuan.txt") == 0) {
        if ((fi->flags & O_ACCMODE) != O_RDONLY)
            return -EACCES;
        return 0;
    }

    // Passthrough
    char full_path[4096];
    build_path(full_path, path);

    int fd = open(full_path, fi->flags);
    if (fd == -1) return -errno;

    fi->fh = fd;
    return 0;
}

static int kenz_read(const char *path, char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi) {
    // Virtual file: generate on-the-fly
    if (strcmp(path, "/tujuan.txt") == 0) {
        size_t len;
        char *content = generate_tujuan(&len);

        size_t bytes_to_copy = 0;
        if ((size_t)offset < len) {
            bytes_to_copy = len - (size_t)offset;
            if (bytes_to_copy > size) bytes_to_copy = size;
            memcpy(buf, content + offset, bytes_to_copy);
        }

        free(content);
        return (int)bytes_to_copy;
    }

    // Passthrough
    (void) fi;
    char full_path[4096];
    build_path(full_path, path);

    int fd = open(full_path, O_RDONLY);
    if (fd == -1) return -errno;

    int res = pread(fd, buf, size, offset);
    if (res == -1) res = -errno;

    close(fd);
    return res;
}

// ============================================================
// FUSE operations struct
// ============================================================
static const struct fuse_operations kenz_ops = {
    .getattr = kenz_getattr,
    .readdir = kenz_readdir,
    .open    = kenz_open,
    .read    = kenz_read,
};

// ============================================================
// Main
// ============================================================
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <source_dir> <mount_dir>\n", argv[0]);
        return 1;
    }

    // Simpan source_dir sebagai absolute path
    if (realpath(argv[1], source_dir) == NULL) {
        perror("realpath source_dir");
        return 1;
    }

    // Buat argv baru untuk fuse_main (hanya mount_dir)
    // fuse_main butuh: program_name [fuse_options] mountpoint
    char *fuse_argv[] = { argv[0], argv[2], "-f", NULL };
    int fuse_argc = 3;

    return fuse_main(fuse_argc, fuse_argv, &kenz_ops, NULL);
}
