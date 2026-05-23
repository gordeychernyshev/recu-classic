#include "recu.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define FAT_ATTR_READONLY 0x01
#define FAT_ATTR_HIDDEN   0x02
#define FAT_ATTR_SYSTEM   0x04
#define FAT_ATTR_VOLUME   0x08
#define FAT_ATTR_DIR      0x10
#define FAT_ATTR_ARCHIVE  0x20
#define FAT_ATTR_LFN      0x0F

typedef struct FatCtx {
    RecuSource *src;
    RecuVolumeInfo *vol;
    RecuCandidateList *out;
    const RecuScanOptions *opt;
    RecuError *err;
} FatCtx;

static uint32_t fat_entry(FatCtx *ctx, uint32_t cluster) {
    if (cluster >= ctx->vol->cluster_count + 2) return 0x0FFFFFFF;
    uint64_t off = ctx->vol->fat_offset;
    uint8_t b[4] = {0};
    RecuError local;
    recu_error_clear(&local);
    if (ctx->vol->fs_type == RECU_FS_FAT16) {
        off += (uint64_t)cluster * 2u;
        if (!recu_source_read(ctx->src, off, b, 2, &local)) return 0xFFFF;
        return recu_le16(b);
    }
    off += (uint64_t)cluster * 4u;
    if (!recu_source_read(ctx->src, off, b, 4, &local)) return 0x0FFFFFFF;
    return recu_le32(b) & 0x0FFFFFFFu;
}

static int fat_is_eoc(FatCtx *ctx, uint32_t value) {
    if (ctx->vol->fs_type == RECU_FS_FAT16) return value >= 0xFFF8u;
    return value >= 0x0FFFFFF8u;
}

static int fat_cluster_allocated(FatCtx *ctx, uint32_t cluster) {
    if (cluster < 2 || cluster >= ctx->vol->cluster_count + 2) return 1;
    return fat_entry(ctx, cluster) != 0;
}

static int fat_range_likely_overwritten(FatCtx *ctx, uint32_t first_cluster, uint64_t size) {
    if (size == 0) return 0;
    if (first_cluster < 2) return 1;
    uint64_t need = (size + ctx->vol->cluster_size - 1u) / ctx->vol->cluster_size;
    if (need > 4096) need = 4096;
    for (uint64_t i = 0; i < need; i++) {
        uint32_t c = first_cluster + (uint32_t)i;
        if (c >= ctx->vol->cluster_count + 2) return 1;
        if (fat_cluster_allocated(ctx, c)) return 1;
    }
    return 0;
}

static int read_cluster(FatCtx *ctx, uint32_t cluster, uint8_t *buffer) {
    uint64_t off = recu_cluster_offset(ctx->vol, cluster);
    return recu_source_read(ctx->src, off, buffer, ctx->vol->cluster_size, ctx->err);
}

static uint8_t *read_directory_chain(FatCtx *ctx, uint32_t first_cluster, uint64_t fixed_offset, uint64_t fixed_size, size_t *size_out) {
    *size_out = 0;
    if (fixed_size > 0) {
        if (fixed_size > (64u * 1024u * 1024u)) fixed_size = 64u * 1024u * 1024u;
        uint8_t *buf = (uint8_t *)malloc((size_t)fixed_size);
        if (!buf) {
            recu_error_set(ctx->err, "out of memory while reading FAT root directory");
            return NULL;
        }
        if (!recu_source_read(ctx->src, fixed_offset, buf, (size_t)fixed_size, ctx->err)) {
            free(buf);
            return NULL;
        }
        *size_out = (size_t)fixed_size;
        return buf;
    }

    if (first_cluster < 2) {
        recu_error_set(ctx->err, "invalid FAT directory cluster");
        return NULL;
    }

    size_t cap = ctx->vol->cluster_size * 4u;
    size_t len = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    uint8_t *cluster_buf = (uint8_t *)malloc(ctx->vol->cluster_size);
    if (!buf || !cluster_buf) {
        free(buf);
        free(cluster_buf);
        recu_error_set(ctx->err, "out of memory while reading FAT directory chain");
        return NULL;
    }

    uint32_t c = first_cluster;
    uint32_t guard = 0;
    while (c >= 2 && c < ctx->vol->cluster_count + 2 && guard++ < ctx->vol->cluster_count) {
        if (!read_cluster(ctx, c, cluster_buf)) {
            free(buf);
            free(cluster_buf);
            return NULL;
        }
        if (len + ctx->vol->cluster_size > cap) {
            size_t next = cap * 2u;
            while (next < len + ctx->vol->cluster_size) next *= 2u;
            if (next > 128u * 1024u * 1024u) break;
            uint8_t *nb = (uint8_t *)realloc(buf, next);
            if (!nb) {
                free(buf);
                free(cluster_buf);
                recu_error_set(ctx->err, "out of memory while expanding FAT directory");
                return NULL;
            }
            buf = nb;
            cap = next;
        }
        memcpy(buf + len, cluster_buf, ctx->vol->cluster_size);
        len += ctx->vol->cluster_size;
        uint32_t next = fat_entry(ctx, c);
        if (next == 0 || fat_is_eoc(ctx, next)) break;
        c = next;
    }

    free(cluster_buf);
    *size_out = len;
    return buf;
}

static void fat_short_name(const uint8_t *e, int deleted, char *dst, size_t dst_size) {
    uint8_t name_raw[8];
    uint8_t ext_raw[3];
    char name[RECU_MAX_NAME];
    char ext[32];
    int name_len = 0;
    int ext_len = 0;
    memset(name, 0, sizeof(name));
    memset(ext, 0, sizeof(ext));
    for (int i = 0; i < 8; i++) {
        unsigned char c = e[i];
        if (i == 0 && deleted) c = '_';
        else if (i == 0 && c == 0x05) c = 0xE5;
        if (c == ' ') break;
        name_raw[name_len++] = c;
    }
    for (int i = 0; i < 3; i++) {
        unsigned char c = e[8 + i];
        if (c == ' ') break;
        ext_raw[ext_len++] = c;
    }
    recu_oem_to_utf8(name_raw, (size_t)name_len, name, sizeof(name));
    recu_oem_to_utf8(ext_raw, (size_t)ext_len, ext, sizeof(ext));
    if (ext[0]) snprintf(dst, dst_size, "%s.%s", name, ext);
    else snprintf(dst, dst_size, "%s", name);
}

static uint8_t fat_lfn_checksum(const uint8_t short_name[11]) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + short_name[i]);
    }
    return sum;
}

static void append_lfn_chars(const uint8_t *e, char *dst, size_t dst_size) {
    static const int offsets[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
    char part[64];
    size_t pos = strlen(dst);
    for (int i = 0; i < 13; i++) {
        uint16_t w = recu_le16(e + offsets[i]);
        if (w == 0x0000 || w == 0xFFFF) break;
        uint8_t tmp[2] = { (uint8_t)(w & 0xFF), (uint8_t)(w >> 8) };
        recu_utf16le_to_utf8(tmp, 1, part, sizeof(part));
        if (pos + strlen(part) + 1 >= dst_size) break;
        strcpy(dst + pos, part);
        pos += strlen(part);
    }
}

static int lfn_deleted_first_char_compatible(uint8_t entries[][32], int count, uint8_t possible_short_first) {
    if (count <= 0) return 0;
    uint16_t first = recu_le16(entries[count - 1] + 1);
    if (first == 0x0000 || first == 0xFFFF) return 0;
    if (first < 128 && isalnum((unsigned char)first)) {
        return toupper((unsigned char)first) == toupper(possible_short_first);
    }
    return 1;
}

static int lfn_checksum_matches_short_entry(uint8_t entries[][32], int count, const uint8_t *short_entry, int deleted) {
    if (count <= 0) return 0;
    uint8_t expected = entries[0][13];
    for (int i = 0; i < count; i++) {
        const uint8_t *e = entries[i];
        if (e[11] != FAT_ATTR_LFN || e[12] != 0 || recu_le16(e + 26) != 0) return 0;
        if (e[13] != expected) return 0;
    }

    uint8_t raw[11];
    memcpy(raw, short_entry, 11);
    if (!deleted) return fat_lfn_checksum(raw) == expected;

    int possible = 0;
    for (unsigned first = 1; first <= 255; first++) {
        raw[0] = (uint8_t)first;
        if (fat_lfn_checksum(raw) == expected) {
            if (!lfn_deleted_first_char_compatible(entries, count, (uint8_t)first)) continue;
            possible = 1;
            break;
        }
    }
    return possible;
}

static int lfn_order_is_plausible(uint8_t entries[][32], int count, int deleted) {
    if (count <= 0 || count > 20) return 0;
    if (deleted) return 1;
    for (int i = 0; i < count; i++) {
        uint8_t seq = entries[i][0];
        uint8_t ord = seq & 0x1Fu;
        int last = (seq & 0x40u) != 0;
        if (ord != (uint8_t)(count - i)) return 0;
        if (last != (i == 0)) return 0;
    }
    return 1;
}

static int build_lfn(uint8_t entries[][32], int count, const uint8_t *short_entry, int deleted, char *dst, size_t dst_size) {
    dst[0] = 0;
    if (!lfn_order_is_plausible(entries, count, deleted)) return 0;
    if (!lfn_checksum_matches_short_entry(entries, count, short_entry, deleted)) return 0;
    for (int i = count - 1; i >= 0; i--) {
        append_lfn_chars(entries[i], dst, dst_size);
    }
    return dst[0] != 0;
}

static int dot_dir_name(const char *name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strcmp(name, "_.") == 0 || strcmp(name, "_..") == 0;
}

static uint32_t fat_entry_cluster(const uint8_t *e) {
    uint32_t high = recu_le16(e + 20);
    uint32_t low = recu_le16(e + 26);
    return (high << 16) | low;
}

static void get_extension(const char *name, char *ext, size_t ext_size) {
    ext[0] = 0;
    const char *p = strrchr(name, '.');
    if (!p || !p[1]) return;
    recu_safe_copy(ext, ext_size, p + 1);
    for (char *x = ext; *x; x++) *x = (char)tolower((unsigned char)*x);
}

static void add_deleted_candidate(FatCtx *ctx, const uint8_t *e, const char *dir_path, const char *name, int short_name_fallback) {
    RecuCandidate c;
    memset(&c, 0, sizeof(c));
    c.kind = RECU_KIND_DELETED_ENTRY;
    c.fs_type = ctx->vol->fs_type;
    recu_safe_copy(c.name, sizeof(c.name), name && *name ? name : "deleted-file");
    recu_sanitize_filename(c.name);
    if (dir_path && *dir_path) snprintf(c.path, sizeof(c.path), "%s/%s", dir_path, c.name);
    else recu_safe_copy(c.path, sizeof(c.path), c.name);
    get_extension(c.name, c.extension, sizeof(c.extension));
    c.format = recu_format_from_extension(c.extension);
    c.first_cluster = fat_entry_cluster(e);
    c.size = recu_le32(e + 28);
    c.cluster_count = c.size ? (uint32_t)((c.size + ctx->vol->cluster_size - 1u) / ctx->vol->cluster_size) : 0;
    c.offset = c.first_cluster >= 2 ? recu_cluster_offset(ctx->vol, c.first_cluster) : 0;
    c.is_directory = (e[11] & FAT_ATTR_DIR) != 0;
    c.likely_overwritten = fat_range_likely_overwritten(ctx, c.first_cluster, c.size);
    int zero_payload = c.size == 0;
    int empty_known_binary = zero_payload && recu_format_requires_data(c.format);
    int cluster_valid = c.first_cluster >= 2 && c.first_cluster < ctx->vol->cluster_count + 2;
    int range_inside = cluster_valid && c.offset > 0 && c.size <= ctx->src->size && c.offset <= ctx->src->size - c.size;
    c.recoverable = !c.is_directory && !zero_payload && cluster_valid && range_inside;
    if (c.is_directory) {
        recu_candidate_confidence_reset(&c, 0, "item=directory");
    } else if (zero_payload) {
        recu_candidate_confidence_reset(&c, 0, "size=zero");
    } else if (!cluster_valid) {
        recu_candidate_confidence_reset(&c, 20, "cluster=invalid");
        recu_candidate_confidence_add(&c, 8, "size=nonzero");
        recu_candidate_confidence_add(&c, short_name_fallback ? 4 : 10, short_name_fallback ? "name=short-first-char-lost" : "name=lfn-ok");
    } else {
        recu_candidate_confidence_reset(&c, 10, "source=fat-deleted-entry");
        recu_candidate_confidence_add(&c, short_name_fallback ? 6 : 14, short_name_fallback ? "name=short-first-char-lost" : "name=lfn-ok");
        recu_candidate_confidence_add(&c, 14, "size=nonzero");
        recu_candidate_confidence_add(&c, 14, "cluster=valid");
        recu_candidate_confidence_add(&c, range_inside ? 8 : -30, range_inside ? "bounds=inside-volume" : "bounds=outside-volume");
        recu_candidate_confidence_add(&c, c.likely_overwritten ? -30 : 28, c.likely_overwritten ? "allocation=reused" : "allocation=free");
        recu_candidate_confidence_add(&c, 0, "method=fat-contiguous-assumed");
    }
    if (c.is_directory || zero_payload) {
        c.validation_checked = 1;
        c.validation = empty_known_binary ? RECU_VALIDATION_DAMAGED : RECU_VALIDATION_UNSUPPORTED;
        recu_candidate_apply_validation_confidence(&c);
    }
    uint16_t time = recu_le16(e + 22);
    uint16_t date = recu_le16(e + 24);
    recu_dos_datetime(date, time, c.modified_utc, sizeof(c.modified_utc));
    if (c.is_directory) {
        snprintf(c.note, sizeof(c.note), "deleted directory entry; not a recoverable file");
    } else if (zero_payload) {
        snprintf(c.note, sizeof(c.note), "zero-byte file entry; no payload to recover");
    } else if (short_name_fallback) {
        snprintf(c.note, sizeof(c.note), "%s; first char of deleted FAT short name is unknown; assuming contiguous clusters",
                 c.likely_overwritten ? "clusters look allocated/reused" : "clusters look free");
    } else {
        snprintf(c.note, sizeof(c.note), "%s; FAT chain is usually cleared after delete, so recovery assumes contiguous clusters",
                 c.likely_overwritten ? "clusters look allocated/reused" : "clusters look free");
    }
    recu_list_push(ctx->out, &c, ctx->err);
}

static void scan_directory(FatCtx *ctx, const char *dir_path, uint32_t first_cluster, uint64_t fixed_offset, uint64_t fixed_size, int depth) {
    if (depth > 24) return;
    size_t dir_size = 0;
    uint8_t *dir = read_directory_chain(ctx, first_cluster, fixed_offset, fixed_size, &dir_size);
    if (!dir) return;

    uint8_t lfn[32][32];
    int lfn_count = 0;

    for (size_t off = 0; off + 32 <= dir_size; off += 32) {
        const uint8_t *e = dir + off;
        uint8_t first = e[0];
        uint8_t attr = e[11];
        if (first == 0x00) break;
        if (first == 0x05) first = 0xE5;

        if (attr == FAT_ATTR_LFN) {
            if (lfn_count < 32) {
                memcpy(lfn[lfn_count++], e, 32);
            }
            continue;
        }

        if (attr & FAT_ATTR_VOLUME) {
            lfn_count = 0;
            continue;
        }

        int deleted = e[0] == 0xE5;
        char name[RECU_MAX_NAME];
        int short_name_fallback = 0;
        if (lfn_count > 0) {
            if (!build_lfn(lfn, lfn_count, e, deleted, name, sizeof(name))) {
                fat_short_name(e, deleted, name, sizeof(name));
                short_name_fallback = 1;
            }
        } else {
            fat_short_name(e, deleted, name, sizeof(name));
            short_name_fallback = 1;
        }

        if (deleted) {
            add_deleted_candidate(ctx, e, dir_path, name, short_name_fallback);
            lfn_count = 0;
            continue;
        }

        if ((attr & FAT_ATTR_DIR) && !dot_dir_name(name)) {
            uint32_t child = fat_entry_cluster(e);
            if (child >= 2 && child < ctx->vol->cluster_count + 2) {
                char child_path[RECU_MAX_PATH];
                if (dir_path && *dir_path) snprintf(child_path, sizeof(child_path), "%s/%s", dir_path, name);
                else recu_safe_copy(child_path, sizeof(child_path), name);
                scan_directory(ctx, child_path, child, 0, 0, depth + 1);
            }
        }

        lfn_count = 0;
    }
    free(dir);
}

int recu_scan_fat(RecuSource *src, RecuVolumeInfo *vol, RecuCandidateList *out, const RecuScanOptions *opt, RecuError *err) {
    if (!src || !vol || !out) return 0;
    if (vol->fs_type != RECU_FS_FAT16 && vol->fs_type != RECU_FS_FAT32) {
        recu_error_set(err, "not a FAT16/FAT32 volume");
        return 0;
    }
    FatCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.src = src;
    ctx.vol = vol;
    ctx.out = out;
    ctx.opt = opt;
    ctx.err = err;
    if (opt && opt->progress) opt->progress(opt->progress_user, "FAT quick scan", 0, 1);
    if (vol->fs_type == RECU_FS_FAT16) {
        scan_directory(&ctx, "", 0, vol->root_dir_offset, vol->root_dir_size, 0);
    } else {
        scan_directory(&ctx, "", vol->root_cluster, 0, 0, 0);
    }
    if (opt && opt->progress) opt->progress(opt->progress_user, "FAT quick scan", 1, 1);
    return 1;
}
