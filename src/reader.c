#include "recu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <malloc.h>
#include <winioctl.h>
#define RECU_RAW_READ_TIMEOUT_MS 8000u

static int utf8_to_wpath(const char *src, wchar_t *dst, size_t dst_count) {
    if (!dst || dst_count == 0) return 0;
    dst[0] = 0;
    if (!src) return 0;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, dst, (int)dst_count);
    if (n <= 0) {
        n = MultiByteToWideChar(CP_ACP, 0, src, -1, dst, (int)dst_count);
    }
    dst[dst_count - 1] = 0;
    return n > 0;
}

static int win32_device_lost_error(DWORD code) {
    return code == ERROR_NOT_READY ||
           code == ERROR_CRC ||
           code == ERROR_GEN_FAILURE ||
           code == ERROR_SEM_TIMEOUT ||
           code == ERROR_IO_DEVICE ||
           code == ERROR_DEVICE_NOT_CONNECTED ||
           code == ERROR_MEDIA_CHANGED ||
           code == ERROR_UNRECOGNIZED_MEDIA;
}

static void set_read_error(RecuError *err, uint64_t offset, DWORD code) {
    if (win32_device_lost_error(code)) {
        recu_error_set(err, "device disconnected or became unreadable at offset %llu (Win32 error %lu)",
                       (unsigned long long)offset, (unsigned long)code);
    } else {
        recu_error_set(err, "read failed at offset %llu (Win32 error %lu)",
                       (unsigned long long)offset, (unsigned long)code);
    }
}
#endif

static int is_raw_path(const char *path) {
    if (!path) return 0;
    if (strncmp(path, "\\\\.\\", 4) == 0) return 1;
    if (strlen(path) == 2 && path[1] == ':') return 1;
    if (strlen(path) == 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) return 1;
    return 0;
}

static void normalize_source_path(const char *in, char *out, size_t out_size, int *raw) {
    *raw = is_raw_path(in);
    if (*raw && strlen(in) >= 2 && in[1] == ':' && strncmp(in, "\\\\.\\", 4) != 0) {
        snprintf(out, out_size, "\\\\.\\%c:", (char)in[0]);
        *raw = 1;
    } else {
        recu_safe_copy(out, out_size, in);
    }
}

int recu_source_open(RecuSource *src, const char *path, RecuError *err) {
    if (!src || !path || !*path) {
        recu_error_set(err, "empty source path");
        return 0;
    }
    memset(src, 0, sizeof(*src));
    src->sector_size = 512;
    char open_path[RECU_MAX_PATH];
    int raw = 0;
    normalize_source_path(path, open_path, sizeof(open_path), &raw);
    recu_safe_copy(src->path, sizeof(src->path), open_path);
    src->is_raw_device = raw;

#ifdef _WIN32
    DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS;
    if (raw) flags |= FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING;
    wchar_t wopen_path[RECU_MAX_PATH];
    if (!utf8_to_wpath(open_path, wopen_path, sizeof(wopen_path) / sizeof(wopen_path[0]))) {
        recu_error_set(err, "cannot convert source path '%s' to UTF-16", open_path);
        return 0;
    }
    src->handle = CreateFileW(wopen_path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, flags, NULL);
    if (src->handle == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        recu_error_set(err, "cannot open '%s' read-only (Win32 error %lu). Raw drives usually need Administrator rights.", open_path, (unsigned long)code);
        return 0;
    }

    LARGE_INTEGER size;
    size.QuadPart = 0;
    if (raw) {
        GET_LENGTH_INFORMATION gli;
        DWORD ret = 0;
        if (DeviceIoControl(src->handle, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &gli, sizeof(gli), &ret, NULL)) {
            src->size = (uint64_t)gli.Length.QuadPart;
        } else if (GetFileSizeEx(src->handle, &size)) {
            src->size = (uint64_t)size.QuadPart;
        }

        DISK_GEOMETRY_EX geom;
        memset(&geom, 0, sizeof(geom));
        ret = 0;
        if (DeviceIoControl(src->handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &geom, sizeof(geom), &ret, NULL)) {
            if (geom.Geometry.BytesPerSector) {
                src->sector_size = geom.Geometry.BytesPerSector;
            }
        }
    } else {
        if (!GetFileSizeEx(src->handle, &size)) {
            DWORD code = GetLastError();
            CloseHandle(src->handle);
            recu_error_set(err, "cannot get size for '%s' (Win32 error %lu)", open_path, (unsigned long)code);
            return 0;
        }
        src->size = (uint64_t)size.QuadPart;
    }
    if (src->size == 0 && raw) {
        LARGE_INTEGER end;
        end.QuadPart = 0;
        if (SetFilePointerEx(src->handle, end, &end, FILE_END)) {
            src->size = (uint64_t)end.QuadPart;
        }
    }
    src->is_open = 1;
    return 1;
#else
    src->file = fopen(open_path, "rb");
    if (!src->file) {
        recu_error_set(err, "cannot open '%s'", open_path);
        return 0;
    }
    fseeko(src->file, 0, SEEK_END);
    src->size = (uint64_t)ftello(src->file);
    fseeko(src->file, 0, SEEK_SET);
    src->is_open = 1;
    return 1;
#endif
}

void recu_source_close(RecuSource *src) {
    if (!src || !src->is_open) return;
#ifdef _WIN32
    if (src->handle && src->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(src->handle);
    }
    src->handle = NULL;
#else
    if (src->file) fclose(src->file);
    src->file = NULL;
#endif
    src->is_open = 0;
}

#ifdef _WIN32
static int raw_read_once(RecuSource *src, uint64_t offset, uint8_t *buffer, DWORD size, DWORD *got_out, RecuError *err) {
    if (got_out) *got_out = 0;
    OVERLAPPED ov;
    memset(&ov, 0, sizeof(ov));
    ov.Offset = (DWORD)(offset & 0xFFFFFFFFu);
    ov.OffsetHigh = (DWORD)(offset >> 32);
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) {
        recu_error_set(err, "cannot create raw read event");
        return 0;
    }

    DWORD got = 0;
    BOOL ok = ReadFile(src->handle, buffer, size, NULL, &ov);
    if (!ok) {
        DWORD code = GetLastError();
        if (code != ERROR_IO_PENDING) {
            CloseHandle(ov.hEvent);
            set_read_error(err, offset, code);
            return 0;
        }
        DWORD wait = WaitForSingleObject(ov.hEvent, RECU_RAW_READ_TIMEOUT_MS);
        if (wait == WAIT_TIMEOUT) {
            CancelIoEx(src->handle, &ov);
            WaitForSingleObject(ov.hEvent, 1000);
            CloseHandle(ov.hEvent);
            recu_error_set(err, "device stopped responding during raw read at offset %llu after %u ms",
                           (unsigned long long)offset, (unsigned)RECU_RAW_READ_TIMEOUT_MS);
            return 0;
        }
        if (wait != WAIT_OBJECT_0) {
            DWORD wcode = GetLastError();
            CancelIoEx(src->handle, &ov);
            CloseHandle(ov.hEvent);
            recu_error_set(err, "raw read wait failed at offset %llu (Win32 error %lu)",
                           (unsigned long long)offset, (unsigned long)wcode);
            return 0;
        }
    }
    ok = GetOverlappedResult(src->handle, &ov, &got, FALSE);
    if (!ok) {
        DWORD code = GetLastError();
        CloseHandle(ov.hEvent);
        set_read_error(err, offset, code);
        return 0;
    }
    CloseHandle(ov.hEvent);
    if (got_out) *got_out = got;
    return 1;
}
#endif

int recu_source_read_partial(RecuSource *src, uint64_t offset, void *buffer, size_t size, size_t *read_out, RecuError *err) {
    if (read_out) *read_out = 0;
    if (!src || !src->is_open || !buffer) {
        recu_error_set(err, "source is not open");
        return 0;
    }
    if (offset >= src->size) {
        return 1;
    }
    uint64_t available = src->size - offset;
    if ((uint64_t)size > available) {
        size = (size_t)available;
    }
#ifdef _WIN32
    if (src->is_raw_device) {
        uint32_t sector = src->sector_size ? src->sector_size : 512u;
        uint64_t aligned_offset = offset - (offset % sector);
        size_t prefix = (size_t)(offset - aligned_offset);
        uint64_t padded64 = (uint64_t)prefix + (uint64_t)size;
        padded64 = ((padded64 + sector - 1u) / sector) * sector;
        if (aligned_offset + padded64 > src->size) {
            padded64 = src->size - aligned_offset;
            padded64 -= padded64 % sector;
        }
        if (padded64 < (uint64_t)prefix + (uint64_t)size) {
            size = padded64 > prefix ? (size_t)(padded64 - prefix) : 0;
        }
        if (size == 0) {
            return 1;
        }
        uint8_t *tmp = (uint8_t *)_aligned_malloc((size_t)padded64, sector);
        if (!tmp) {
            recu_error_set(err, "out of memory while preparing sector-aligned raw read");
            return 0;
        }
        size_t total = 0;
        while (total < (size_t)padded64) {
            DWORD chunk = (DWORD)(((size_t)padded64 - total) > (256u * 1024u) ? (256u * 1024u) : ((size_t)padded64 - total));
            DWORD got = 0;
            if (!raw_read_once(src, aligned_offset + total, tmp + total, chunk, &got, err)) {
                _aligned_free(tmp);
                return 0;
            }
            if (got == 0) break;
            total += got;
        }
        if (total < prefix) {
            _aligned_free(tmp);
            return 1;
        }
        size_t available = total - prefix;
        if (available > size) available = size;
        memcpy(buffer, tmp + prefix, available);
        _aligned_free(tmp);
        if (read_out) *read_out = available;
        return 1;
    }

    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(src->handle, li, NULL, FILE_BEGIN)) {
        recu_error_set(err, "seek failed at offset %llu", (unsigned long long)offset);
        return 0;
    }
    uint8_t *out = (uint8_t *)buffer;
    size_t total = 0;
    while (total < size) {
        DWORD chunk = (DWORD)((size - total) > (1u << 30) ? (1u << 30) : (size - total));
        DWORD got = 0;
        if (!ReadFile(src->handle, out + total, chunk, &got, NULL)) {
            DWORD code = GetLastError();
            set_read_error(err, offset + total, code);
            return 0;
        }
        if (got == 0) break;
        total += got;
    }
    if (read_out) *read_out = total;
    return 1;
#else
    if (fseeko(src->file, (off_t)offset, SEEK_SET) != 0) {
        recu_error_set(err, "seek failed");
        return 0;
    }
    size_t got = fread(buffer, 1, size, src->file);
    if (read_out) *read_out = got;
    return ferror(src->file) == 0;
#endif
}

int recu_source_read(RecuSource *src, uint64_t offset, void *buffer, size_t size, RecuError *err) {
    size_t got = 0;
    if (!recu_source_read_partial(src, offset, buffer, size, &got, err)) {
        return 0;
    }
    if (got != size) {
        recu_error_set(err, "short read at offset %llu: wanted %llu bytes, got %llu",
                       (unsigned long long)offset, (unsigned long long)size, (unsigned long long)got);
        return 0;
    }
    return 1;
}

uint64_t recu_cluster_offset(const RecuVolumeInfo *vol, uint32_t cluster) {
    if (!vol || cluster < 2) return 0;
    if (vol->fs_type == RECU_FS_EXFAT) {
        return vol->data_offset + (uint64_t)(cluster - 2) * vol->cluster_size;
    }
    return vol->data_offset + (uint64_t)(cluster - 2) * vol->cluster_size;
}

static int is_power2_u32(uint32_t v) {
    return v && ((v & (v - 1u)) == 0);
}

static int probe_fat(RecuSource *src, RecuVolumeInfo *vol, const uint8_t *bs, RecuError *err) {
    uint16_t bps = recu_le16(bs + 11);
    uint8_t spc = bs[13];
    uint16_t reserved = recu_le16(bs + 14);
    uint8_t fats = bs[16];
    uint16_t root_entries = recu_le16(bs + 17);
    uint16_t total16 = recu_le16(bs + 19);
    uint16_t fat16_size = recu_le16(bs + 22);
    uint32_t total32 = recu_le32(bs + 32);
    uint32_t fat32_size = recu_le32(bs + 36);

    if ((bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) ||
        !is_power2_u32(spc) || spc > 128 || fats == 0) {
        recu_error_set(err, "invalid FAT BPB");
        return 0;
    }
    uint64_t total_sectors = total16 ? total16 : total32;
    uint64_t fat_size = fat16_size ? fat16_size : fat32_size;
    if (total_sectors == 0 || fat_size == 0) {
        recu_error_set(err, "invalid FAT geometry");
        return 0;
    }
    uint64_t root_dir_sectors = ((uint64_t)root_entries * 32u + (bps - 1u)) / bps;
    uint64_t first_data_sector = (uint64_t)reserved + (uint64_t)fats * fat_size + root_dir_sectors;
    if (total_sectors <= first_data_sector) {
        recu_error_set(err, "invalid FAT data region");
        return 0;
    }
    if ((uint64_t)reserved + (uint64_t)fats * fat_size > total_sectors) {
        recu_error_set(err, "invalid FAT table bounds");
        return 0;
    }
    uint64_t total_bytes = total_sectors * (uint64_t)bps;
    if (src && src->size && total_bytes > src->size) {
        recu_error_set(err, "FAT volume is larger than the source");
        return 0;
    }
    uint64_t data_sectors = total_sectors - first_data_sector;
    uint32_t clusters = (uint32_t)(data_sectors / spc);
    if (clusters < 4085) {
        recu_error_set(err, "FAT12 is not supported by this build");
        return 0;
    }

    memset(vol, 0, sizeof(*vol));
    vol->fs_type = clusters < 65525 ? RECU_FS_FAT16 : RECU_FS_FAT32;
    recu_safe_copy(vol->fs_name, sizeof(vol->fs_name), recu_fs_name(vol->fs_type));
    vol->bytes_per_sector = bps;
    vol->sectors_per_cluster = spc;
    vol->cluster_size = (uint32_t)bps * spc;
    vol->total_sectors = total_sectors;
    vol->total_bytes = total_bytes;
    vol->fat_offset = (uint64_t)reserved * bps;
    vol->fat_size = fat_size * bps;
    vol->fat_count = fats;
    vol->root_dir_offset = ((uint64_t)reserved + (uint64_t)fats * fat_size) * bps;
    vol->root_dir_size = root_dir_sectors * bps;
    vol->data_offset = first_data_sector * bps;
    vol->data_size = data_sectors * bps;
    vol->root_cluster = vol->fs_type == RECU_FS_FAT32 ? recu_le32(bs + 44) : 0;
    if (vol->fs_type == RECU_FS_FAT32 && (vol->root_cluster < 2 || vol->root_cluster >= clusters + 2u)) {
        recu_error_set(err, "invalid FAT32 root cluster");
        return 0;
    }
    vol->cluster_count = clusters;
    return 1;
}

static int probe_exfat(RecuSource *src, RecuVolumeInfo *vol, const uint8_t *bs, RecuError *err) {
    if (memcmp(bs + 3, "EXFAT   ", 8) != 0) {
        return 0;
    }
    uint64_t partition_offset = recu_le64(bs + 64);
    (void)partition_offset;
    uint64_t total_sectors = recu_le64(bs + 72);
    uint32_t fat_offset = recu_le32(bs + 80);
    uint32_t fat_length = recu_le32(bs + 84);
    uint32_t cluster_heap_offset = recu_le32(bs + 88);
    uint32_t cluster_count = recu_le32(bs + 92);
    uint32_t root_cluster = recu_le32(bs + 96);
    uint8_t bps_shift = bs[108];
    uint8_t spc_shift = bs[109];
    uint8_t fat_count = bs[110];
    if (bps_shift < 9 || bps_shift > 12 || spc_shift > 25 ||
        (uint32_t)bps_shift + (uint32_t)spc_shift > 25 ||
        total_sectors == 0 || cluster_count == 0 || root_cluster < 2) {
        recu_error_set(err, "invalid exFAT boot sector");
        return 0;
    }
    uint32_t bps = 1u << bps_shift;
    uint32_t spc = 1u << spc_shift;
    uint64_t cluster_size64 = (uint64_t)bps * (uint64_t)spc;
    if (cluster_size64 == 0 || cluster_size64 > 32ull * 1024ull * 1024ull ||
        (uint64_t)fat_offset + fat_length > total_sectors ||
        cluster_heap_offset >= total_sectors ||
        (uint64_t)cluster_heap_offset + (uint64_t)cluster_count * spc > total_sectors ||
        root_cluster >= cluster_count + 2u) {
        recu_error_set(err, "invalid exFAT geometry");
        return 0;
    }
    uint64_t total_bytes = total_sectors * (uint64_t)bps;
    if (src && src->size && total_bytes > src->size) {
        recu_error_set(err, "exFAT volume is larger than the source");
        return 0;
    }
    memset(vol, 0, sizeof(*vol));
    vol->fs_type = RECU_FS_EXFAT;
    recu_safe_copy(vol->fs_name, sizeof(vol->fs_name), "exFAT");
    vol->bytes_per_sector = bps;
    vol->sectors_per_cluster = spc;
    vol->cluster_size = (uint32_t)cluster_size64;
    vol->total_sectors = total_sectors;
    vol->total_bytes = total_bytes;
    vol->fat_offset = (uint64_t)fat_offset * bps;
    vol->fat_size = (uint64_t)fat_length * bps;
    vol->fat_count = fat_count;
    vol->root_cluster = root_cluster;
    vol->cluster_count = cluster_count;
    vol->exfat_cluster_heap_offset = (uint64_t)cluster_heap_offset * bps;
    vol->data_offset = vol->exfat_cluster_heap_offset;
    vol->data_size = (uint64_t)cluster_count * vol->cluster_size;
    vol->root_dir_offset = recu_cluster_offset(vol, root_cluster);
    return 1;
}

int recu_probe_volume(RecuSource *src, RecuVolumeInfo *vol, RecuError *err) {
    if (!src || !vol) return 0;
    uint8_t bs[512];
    if (!recu_source_read(src, 0, bs, sizeof(bs), err)) {
        return 0;
    }
    if (bs[510] != 0x55 || bs[511] != 0xAA) {
        recu_error_set(err, "invalid boot sector signature");
        return 0;
    }
    if (probe_exfat(src, vol, bs, err)) {
        return 1;
    }
    RecuError fat_err;
    recu_error_clear(&fat_err);
    if (probe_fat(src, vol, bs, &fat_err)) {
        return 1;
    }
    recu_error_set(err, "unsupported or unrecognized filesystem. exFAT/FAT16/FAT32 are supported; FAT probe said: %s", fat_err.message);
    return 0;
}

#ifdef _WIN32
int recu_list_windows_drives(RecuDriveInfo *drives, int max_drives) {
    if (!drives || max_drives <= 0) return 0;
    DWORD mask = GetLogicalDrives();
    int n = 0;
    for (char letter = 'A'; letter <= 'Z' && n < max_drives; letter++) {
        if (!(mask & (1u << (letter - 'A')))) continue;
        char root[4] = { letter, ':', '\\', 0 };
        UINT type = GetDriveTypeA(root);
        if (type != DRIVE_REMOVABLE && type != DRIVE_FIXED) continue;
        char volpath[16];
        snprintf(volpath, sizeof(volpath), "\\\\.\\%c:", letter);
        RecuDriveInfo di;
        memset(&di, 0, sizeof(di));
        recu_safe_copy(di.volume_path, sizeof(di.volume_path), volpath);
        di.drive_type = type;
        ULARGE_INTEGER free_bytes, total_bytes, total_free;
        if (GetDiskFreeSpaceExA(root, &free_bytes, &total_bytes, &total_free)) {
            di.size = (uint64_t)total_bytes.QuadPart;
        }
        snprintf(di.display, sizeof(di.display), "%c: %s %llu MB",
                 letter,
                 type == DRIVE_REMOVABLE ? "removable" : "fixed",
                 (unsigned long long)(di.size / 1024u / 1024u));
        drives[n++] = di;
    }
    return n;
}
#endif
