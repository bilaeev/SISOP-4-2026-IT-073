# Soal 1 — FUSE Kenz Rescue 🗻

> **Lore:** Sebastian, fans nomor satu Asisten Kenz, harus menemukan koordinat ritual di Puncak Gunung Kawi sebelum tengah malam. Satu-satunya petunjuk: flashdisk merah `TOYYIBAN 32GB` berisi tujuh catatan log ekspedisi, masing-masing menyimpan satu fragmen koordinat di baris `KOORD:`. Dengan FUSE, Sebastian bisa membaca ketujuh fragmen itu tanpa menyentuh satu byte pun di flashdisk — lalu menggabungkannya menjadi koordinat lengkap lewat file virtual `tujuan.txt`.

---

## Daftar Isi

- [Struktur Repo](#struktur-repo)
- [Penjelasan Kode `kenz_rescue.c`](#penjelasan-kode-kenz_rescuec)
- [Langkah Pengerjaan](#langkah-pengerjaan)
- [Verifikasi Per Poin Soal](#verifikasi-per-poin-soal)
- [Output Akhir](#output-akhir)

---

## Struktur Repo

**Repo awal:**
```
soal1/
└── kenz_rescue.c
```

**Repo akhir (setelah setup):**
```
soal1/
├── kenz_rescue          ← binary hasil compile
├── kenz_rescue.c        ← source code FUSE
├── amba_files/          ← source directory (flashdisk)
│   ├── 1.txt
│   ├── 2.txt
│   ├── 3.txt
│   ├── 4.txt
│   ├── 5.txt
│   ├── 6.txt
│   └── 7.txt
└── mnt/                 ← mount point (filesystem virtual)
    ├── 1.txt            (passthrough dari amba_files)
    ├── 2.txt            (passthrough dari amba_files)
    ├── 3.txt            (passthrough dari amba_files)
    ├── 4.txt            (passthrough dari amba_files)
    ├── 5.txt            (passthrough dari amba_files)
    ├── 6.txt            (passthrough dari amba_files)
    ├── 7.txt            (passthrough dari amba_files)
    └── tujuan.txt       (virtual — tidak ada di amba_files!)
```

---

## Penjelasan Kode `kenz_rescue.c`

```c
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
```
> `FUSE_USE_VERSION 31` menentukan versi API FUSE yang digunakan (FUSE 3.1). Header `fuse3/fuse.h` menyediakan semua struct dan fungsi FUSE yang dibutuhkan.

---

### Global Variable

```c
static char source_dir[4096];
```
> Menyimpan absolute path dari `amba_files/` sebagai source directory. Dibuat global agar bisa diakses dari semua callback FUSE.

---

### Helper: `build_path()`

```c
static void build_path(char *dest, const char *path) {
    snprintf(dest, 4096, "%s%s", source_dir, path);
}
```
> Menggabungkan `source_dir` dengan path relatif FUSE (misalnya `/1.txt`) menjadi full path di disk (misalnya `/home/user/soal1/amba_files/1.txt`). Digunakan oleh semua callback passthrough.

---

### Helper: `generate_tujuan()`

```c
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
                char *fragment = line + 6;
                while (*fragment == ' ') fragment++;         // trim spasi awal
                size_t len = strlen(fragment);
                while (len > 0 && (fragment[len-1] == '\n' || fragment[len-1] == '\r'))
                    fragment[--len] = '\0';                  // trim newline

                strncat(combined, fragment, sizeof(combined) - strlen(combined) - 1);
                break;
            }
        }
        fclose(f);
    }

    char *result = malloc(4096);
    int written = snprintf(result, 4096, "Tujuan Mas Amba: %s\n", combined);
    *out_len = (size_t)written;
    return result;
}
```
> Fungsi inti soal poin D. Membuka `1.txt` sampai `7.txt` secara berurutan, mencari baris yang diawali `KOORD:`, mengambil fragmen koordinatnya, lalu menggabungkan semua fragmen menjadi satu string. Hasilnya diformat sebagai `Tujuan Mas Amba: <koordinat>\n`. Fungsi ini dipanggil **saat file dibaca** (on-the-fly), bukan disimpan di disk.

---

### Callback: `kenz_getattr()`

```c
static int kenz_getattr(const char *path, struct stat *stbuf,
                        struct fuse_file_info *fi) {
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/tujuan.txt") == 0) {
        size_t len;
        char *content = generate_tujuan(&len);
        free(content);

        stbuf->st_mode  = S_IFREG | 0444;  // regular file, read-only
        stbuf->st_nlink = 1;
        stbuf->st_size  = (off_t)len;       // ukuran konsisten dengan isi
        stbuf->st_uid   = 0;
        stbuf->st_gid   = 0;
        stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = 0;
        return 0;
    }

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode  = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    char full_path[4096];
    build_path(full_path, path);
    if (lstat(full_path, stbuf) == -1) return -errno;
    return 0;
}
```
> Dipanggil setiap kali sistem membutuhkan metadata file (`stat`, `ls -l`, dll.). Untuk `tujuan.txt`, dikembalikan metadata hardcoded (permission `0444`, size dihitung dari `generate_tujuan`). Untuk file lain, diteruskan ke `lstat()` di source directory (passthrough).

---

### Callback: `kenz_readdir()`

```c
static int kenz_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi,
                        enum fuse_readdir_flags flags) {
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    DIR *dp = opendir(source_dir);
    if (!dp) return -errno;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (de->d_name[0] == '.') continue;
        filler(buf, de->d_name, NULL, 0, 0);
    }
    closedir(dp);

    filler(buf, "tujuan.txt", NULL, 0, 0);  // tambahkan file virtual
    return 0;
}
```
> Dipanggil saat `ls` dijalankan. Membaca isi `source_dir` (7 file dari `amba_files/`), lalu **menambahkan `tujuan.txt` secara manual** tanpa file fisiknya ada di disk. Inilah yang membuat `ls mnt/` menampilkan 8 entry sementara `ls amba_files/` tetap 7.

---

### Callback: `kenz_open()`

```c
static int kenz_open(const char *path, struct fuse_file_info *fi) {
    if (strcmp(path, "/tujuan.txt") == 0) {
        if ((fi->flags & O_ACCMODE) != O_RDONLY) return -EACCES;
        return 0;
    }

    char full_path[4096];
    build_path(full_path, path);
    int fd = open(full_path, fi->flags);
    if (fd == -1) return -errno;
    fi->fh = fd;
    return 0;
}
```
> Dipanggil saat file dibuka. Untuk `tujuan.txt`, hanya mengizinkan mode read-only (`O_RDONLY`). Untuk file lain, membuka file fisik di source directory dan menyimpan file descriptor di `fi->fh`.

---

### Callback: `kenz_read()`

```c
static int kenz_read(const char *path, char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi) {
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

    char full_path[4096];
    build_path(full_path, path);
    int fd = open(full_path, O_RDONLY);
    if (fd == -1) return -errno;
    int res = pread(fd, buf, size, offset);
    if (res == -1) res = -errno;
    close(fd);
    return res;
}
```
> Dipanggil saat file dibaca (`cat`, dll.). Untuk `tujuan.txt`, memanggil `generate_tujuan()` untuk membuat konten on-the-fly, lalu menyalin sebagian sesuai `offset` dan `size` yang diminta. Untuk file lain, menggunakan `pread()` untuk membaca langsung dari disk.

---

### `main()`

```c
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <source_dir> <mount_dir>\n", argv[0]);
        return 1;
    }

    if (realpath(argv[1], source_dir) == NULL) {
        perror("realpath source_dir");
        return 1;
    }

    char *fuse_argv[] = { argv[0], argv[2], "-f", NULL };
    int fuse_argc = 3;

    return fuse_main(fuse_argc, fuse_argv, &kenz_ops, NULL);
}
```
> Menerima dua argumen: `<source_dir>` dan `<mount_dir>`. `realpath()` mengonversi path relatif ke absolute path agar callback bisa mengaksesnya dari konteks apapun. Flag `-f` menjalankan FUSE di foreground sehingga mudah di-debug. `fuse_main()` memulai event loop FUSE.

---

## Langkah Pengerjaan

### Prasyarat

```bash
sudo apt update
sudo apt install libfuse3-dev fuse3 python3-pip -y
pip install gdown --break-system-packages
```

### Poin A — Download, Extract, Hapus Zip

```bash
cd ~/SISOP-4-2026-IT-073/soal1

# Download dari Google Drive
~/.local/bin/gdown "1nLXFhptDo2mnUlZsw8pTWyAVpV49W20U" -O amba_files.zip

# Extract
unzip amba_files.zip

# Hapus zip (wajib sesuai soal)
rm amba_files.zip

# Verifikasi
ls amba_files/
# Output: 1.txt  2.txt  3.txt  4.txt  5.txt  6.txt  7.txt
```

### Poin B, C, D — Compile & Mount

```bash
# Buat mount point
mkdir -p mnt

# Compile
gcc -Wall -o kenz_rescue kenz_rescue.c $(pkg-config --cflags --libs fuse3)

# Jalankan FUSE (terminal akan ngehang — normal)
./kenz_rescue amba_files mnt
```

> Buka terminal baru untuk verifikasi.

---

## Verifikasi Per Poin Soal

### Poin A — Struktur folder benar, zip sudah terhapus

```bash
ls amba_files/
# Output: 1.txt  2.txt  3.txt  4.txt  5.txt  6.txt  7.txt

ls amba_files.zip 2>&1
# Output: ls: cannot access 'amba_files.zip': No such file or directory
```

### Poin B — Passthrough byte-identical

```bash
for i in 1 2 3 4 5 6 7; do
    diff mnt/$i.txt amba_files/$i.txt && echo "$i.txt OK"
done
# Output:
# 1.txt OK
# 2.txt OK
# 3.txt OK
# 4.txt OK
# 5.txt OK
# 6.txt OK
# 7.txt OK
```

### Poin C — Virtual file hanya muncul di mnt/

```bash
ls mnt/
# Output: 1.txt  2.txt  3.txt  4.txt  5.txt  6.txt  7.txt  tujuan.txt

ls amba_files/
# Output: 1.txt  2.txt  3.txt  4.txt  5.txt  6.txt  7.txt

ls amba_files/tujuan.txt 2>&1
# Output: ls: cannot access 'amba_files/tujuan.txt': No such file or directory
```

### Poin D — Koordinat muncul on-the-fly

```bash
cat mnt/tujuan.txt
# Output: Tujuan Mas Amba: -7.957382728443728, 112.4698688227961, 23:59 WIB

stat mnt/tujuan.txt
# Size: 66   Blocks: 0   IO Block: 4096   regular file
# Access: (0444/-r--r--r--)
# Access: 1970-01-01 07:00:00.000000000 +0700

wc -c mnt/tujuan.txt
# Output: 66 mnt/tujuan.txt
```

### Unmount setelah selesai

```bash
fusermount3 -u mnt
# Lalu Ctrl+C di terminal FUSE
```

---

## Output Akhir

Koordinat ritual berhasil ditemukan:

```
Tujuan Mas Amba: -7.957382728443728, 112.4698688227961, 23:59 WIB
```

## Output
1. <img width="703" height="117" alt="Screenshot 2026-05-17 145246" src="https://github.com/user-attachments/assets/d07a28f3-3685-4cd6-b39f-d6392f05604a" />
   ls pertama memastikan zip sudah terhapus. ls amba_files/ memastikan ketujuh file log ekspedisi ada. ls amba_files.zip 2>&1 memastikan zip benar-benar sudah tidak ada.
2. <img width="707" height="196" alt="Screenshot 2026-05-17 145538" src="https://github.com/user-attachments/assets/3d2d2071-70c0-492d-bec5-b1a3022dc987" />
   Membandingkan setiap file di mnt/ dengan file aslinya di amba_files/ menggunakan diff. Jika tidak ada perbedaan satu byte pun, mencetak "OK". Ini membuktikan bahwa FUSE berhasil meneruskan (passthrough) isi file secara identik dari source ke mount point.
3. <img width="732" height="121" alt="Screenshot 2026-05-17 150151" src="https://github.com/user-attachments/assets/5581f573-6e66-4ca1-9331-29f398b8ec27" />
   ls mnt/ Menampilkan isi mount directory. Harus muncul 8 file: 1.txt sampai 7.txt ditambah tujuan.txt.  

ls amba_files/ Menampilkan isi source directory. Harus tetap 7 file saja — membuktikan tujuan.txt tidak pernah dibuat secara fisik di disk.  

ls amba_files/tujuan.txt 2>&1 Membuktikan secara eksplisit bahwa tujuan.txt tidak exist di amba_files/. File ini murni virtual, hanya hidup di dalam FUSE.  

4. <img width="737" height="254" alt="Screenshot 2026-05-17 150525" src="https://github.com/user-attachments/assets/0a12cb7b-a1c1-44ea-a0fd-1e28691be298" />
   cat mnt/tujuan.txt Membaca isi tujuan.txt. Isinya dibangkitkan on-the-fly saat dibaca — program membuka 1.txt sampai 7.txt, mencari baris yang diawali KOORD:, menggabungkan semua fragmennya secara berurutan, lalu mengembalikan hasilnya dalam format Tujuan Mas Amba: <koordinat>.  

stat mnt/tujuan.txt Menampilkan metadata file virtual. Ukuran file (Size) harus konsisten dengan panjang string koordinat, permission 0444 (read-only), dan timestamp epoch 1970-01-01 sesuai yang kita hardcode di program.  

wc -c mnt/tujuan.txt Menghitung jumlah byte file. Harus konsisten dengan nilai Size yang ditampilkan stat — membuktikan ukuran file virtual sudah dilaporkan dengan benar oleh FUSE.  




   
