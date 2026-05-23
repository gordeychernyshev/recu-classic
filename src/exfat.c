#include "recu.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define EXFAT_TYPE_END       0x00
#define EXFAT_TYPE_BITMAP    0x81
#define EXFAT_TYPE_FILE      0x85
#define EXFAT_TYPE_STREAM    0xC0
#define EXFAT_TYPE_NAME      0xC1
#define EXFAT_TYPE_FILE_DEL  0x05
#define EXFAT_TYPE_STREAM_DEL 0x40
#define EXFAT_TYPE_NAME_DEL   0x41
#define EXFAT_ATTR_DIR       0x10

typedef struct ExfatCtx {
    RecuSource *src;
    RecuVolumeInfo *vol;
    RecuCandidateList *out;
    const RecuScanOptions *opt;
    RecuError *err;
    uint8_t *bitmap;
    size_t bitmap_size;
} ExfatCtx;

static uint32_t exfat_fat_entry(ExfatCtx *ctx, uint32_t cluster) {
    if (cluster >= ctx->vol->cluster_count + 2) return 0xFFFFFFFFu;
    uint8_t b[4];
    RecuError local;
    recu_error_clear(&local);
    uint64_t off = ctx->vol->fat_offset + (uint64_t)cluster * 4u;
    if (!recu_source_read(ctx->src, off, b, 4, &local)) return 0xFFFFFFFFu;
    return recu_le32(b);
}

static int exfat_is_eoc(uint32_t v) {
    return v >= 0xFFFFFFF8u;
}

static int exfat_bitmap_allocated(ExfatCtx *ctx, uint32_t cluster) {
    if (!ctx->bitmap || cluster < 2) return 0;
    uint32_t index = cluster - 2;
    uint32_t byte = index / 8u;
    uint32_t bit = index % 8u;
    if (byte >= ctx->bitmap_size) return 0;
    return (ctx->bitmap[byte] & (1u << bit)) != 0;
}

static int exfat_range_likely_overwritten(ExfatCtx *ctx, uint32_t first_cluster, uint64_t size) {
    if (size == 0) return 0;
    if (first_cluster < 2) return 1;
    uint64_t need = (size + ctx->vol->cluster_size - 1u) / ctx->vol->cluster_size;
    if (need > 4096) need = 4096;
    for (uint64_t i = 0; i < need; i++) {
        uint32_t c = first_cluster + (uint32_t)i;
        if (c >= ctx->vol->cluster_count + 2) return 1;
        if (exfat_bitmap_allocated(ctx, c)) return 1;
    }
    return 0;
}

static uint8_t *read_exfat_stream(ExfatCtx *ctx, uint32_t first_cluster, uint64_t data_len, int no_fat_chain, size_t *size_out) {
    *size_out = 0;
    if (first_cluster < 2 || data_len == 0) return NULL;
    if (data_len > 128u * 1024u * 1024u) data_len = 128u * 1024u * 1024u;
    uint8_t *buf = (uint8_t *)malloc((size_t)data_len);
    if (!buf) {
        recu_error_set(ctx->err, "out of memory while reading exFAT directory");
        return NULL;
    }

    uint64_t written = 0;
    if (no_fat_chain) {
        uint64_t off = recu_cluster_offset(ctx->vol, first_cluster);
        if (!recu_source_read(ctx->src, off, buf, (size_t)data_len, ctx->err)) {
            free(buf);
            return NULL;
        }
        *size_out = (size_t)data_len;
        return buf;
    }

    uint32_t c = first_cluster;
    uint32_t guard = 0;
    while (c >= 2 && c < ctx->vol->cluster_count + 2 && written < data_len && guard++ < ctx->vol->cluster_count) {
        size_t chunk = ctx->vol->cluster_size;
        if (written + chunk > data_len) chunk = (size_t)(data_len - written);
        if (!recu_source_read(ctx->src, recu_cluster_offset(ctx->vol, c), buf + written, chunk, ctx->err)) {
            free(buf);
            return NULL;
        }
        written += chunk;
        uint32_t next = exfat_fat_entry(ctx, c);
        if (next == 0 || exfat_is_eoc(next)) break;
        c = next;
    }
    *size_out = (size_t)written;
    return buf;
}

static int exfat_dir_cluster_has_end(const uint8_t *buf, uint32_t size) {
    if (!buf) return 0;
    for (uint32_t off = 0; off + 32 <= size; off += 32) {
        if (buf[off] == EXFAT_TYPE_END) return 1;
    }
    return 0;
}

static uint8_t *read_exfat_directory_unknown(ExfatCtx *ctx, uint32_t first_cluster, int no_fat_chain, size_t *size_out) {
    const uint64_t limit = 128ull * 1024ull * 1024ull;
    *size_out = 0;
    if (first_cluster < 2 || first_cluster >= ctx->vol->cluster_count + 2 || ctx->vol->cluster_size == 0) return NULL;

    uint64_t cap64 = (uint64_t)ctx->vol->cluster_size * 16u;
    if (cap64 < ctx->vol->cluster_size) cap64 = ctx->vol->cluster_size;
    if (cap64 > limit) cap64 = limit;
    uint8_t *buf = (uint8_t *)malloc((size_t)cap64);
    if (!buf) {
        recu_error_set(ctx->err, "out of memory while reading exFAT directory");
        return NULL;
    }

    uint32_t c = first_cluster;
    uint32_t guard = 0;
    size_t len = 0;
    size_t cap = (size_t)cap64;
    while (c >= 2 && c < ctx->vol->cluster_count + 2 && guard++ < ctx->vol->cluster_count && len < limit) {
        if ((uint64_t)len + ctx->vol->cluster_size > limit) break;
        if (len + ctx->vol->cluster_size > cap) {
            size_t next_cap = cap * 2u;
            if (next_cap < cap || next_cap > limit) next_cap = (size_t)limit;
            if (next_cap < len + ctx->vol->cluster_size) {
                next_cap = len + ctx->vol->cluster_size;
            }
            uint8_t *grown = (uint8_t *)realloc(buf, next_cap);
            if (!grown) {
                free(buf);
                recu_error_set(ctx->err, "out of memory while reading exFAT directory");
                return NULL;
            }
            buf = grown;
            cap = next_cap;
        }

        if (!recu_source_read(ctx->src, recu_cluster_offset(ctx->vol, c), buf + len, ctx->vol->cluster_size, ctx->err)) {
            free(buf);
            return NULL;
        }
        int has_end = exfat_dir_cluster_has_end(buf + len, ctx->vol->cluster_size);
        len += ctx->vol->cluster_size;
        if (has_end) break;

        if (no_fat_chain) {
            c++;
            continue;
        }
        uint32_t next = exfat_fat_entry(ctx, c);
        if (next == 0) {
            c++;
            continue;
        }
        if (exfat_is_eoc(next) || next < 2 || next >= ctx->vol->cluster_count + 2) break;
        c = next;
    }

    *size_out = len;
    return buf;
}

static uint8_t *read_exfat_directory(ExfatCtx *ctx, uint32_t first_cluster, uint64_t length_hint, int no_fat_chain, size_t *size_out) {
    if (length_hint == 0) {
        return read_exfat_directory_unknown(ctx, first_cluster, no_fat_chain, size_out);
    }
    return read_exfat_stream(ctx, first_cluster, length_hint, no_fat_chain, size_out);
}

static void exfat_name_from_entries(const uint8_t *entries, int count, int deleted, char *dst, size_t dst_size) {
    dst[0] = 0;
    char tmp[RECU_MAX_NAME];
    tmp[0] = 0;
    for (int i = 0; i < count; i++) {
        const uint8_t *e = entries + i * 32;
        uint8_t type = e[0];
        if (deleted ? (type != EXFAT_TYPE_NAME_DEL) : (type != EXFAT_TYPE_NAME)) continue;
        char part[64];
        recu_utf16le_to_utf8(e + 2, 15, part, sizeof(part));
        recu_safe_append(tmp, sizeof(tmp), part);
    }
    if (tmp[0]) recu_safe_copy(dst, dst_size, tmp);
    else recu_safe_copy(dst, dst_size, deleted ? "deleted-exfat-file" : "exfat-file");
}

static void get_extension(const char *name, char *ext, size_t ext_size) {
    ext[0] = 0;
    const char *p = strrchr(name, '.');
    if (!p || !p[1]) return;
    recu_safe_copy(ext, ext_size, p + 1);
    for (char *x = ext; *x; x++) *x = (char)tolower((unsigned char)*x);
}

static void exfat_add_candidate(ExfatCtx *ctx, const uint8_t *file_entry, const uint8_t *stream, const uint8_t *names, int name_count, const char *dir_path) {
    RecuCandidate c;
    memset(&c, 0, sizeof(c));
    c.kind = RECU_KIND_DELETED_ENTRY;
    c.fs_type = RECU_FS_EXFAT;
    exfat_name_from_entries(names, name_count, 1, c.name, sizeof(c.name));
    recu_sanitize_filename(c.name);
    if (dir_path && *dir_path) snprintf(c.path, sizeof(c.path), "%s/%s", dir_path, c.name);
    else recu_safe_copy(c.path, sizeof(c.path), c.name);
    get_extension(c.name, c.extension, sizeof(c.extension));
    c.format = recu_format_from_extension(c.extension);
    c.first_cluster = recu_le32(stream + 20);
    c.size = recu_le64(stream + 24);
    c.cluster_count = c.size ? (uint32_t)((c.size + ctx->vol->cluster_size - 1u) / ctx->vol->cluster_size) : 0;
    c.offset = c.first_cluster >= 2 ? recu_cluster_offset(ctx->vol, c.first_cluster) : 0;
    c.no_fat_chain = (stream[1] & 0x02) != 0;
    c.is_directory = (recu_le16(file_entry + 4) & EXFAT_ATTR_DIR) != 0;
    c.likely_overwritten = exfat_range_likely_overwritten(ctx, c.first_cluster, c.size);
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
        recu_candidate_confidence_add(&c, name_count > 0 ? 10 : 2, name_count > 0 ? "name=entry-group-ok" : "name=generated");
    } else {
        recu_candidate_confidence_reset(&c, 12, "source=exfat-deleted-entry");
        recu_candidate_confidence_add(&c, name_count > 0 ? 14 : 4, name_count > 0 ? "name=entry-group-ok" : "name=generated");
        recu_candidate_confidence_add(&c, 14, "size=nonzero");
        recu_candidate_confidence_add(&c, 14, "cluster=valid");
        recu_candidate_confidence_add(&c, range_inside ? 8 : -30, range_inside ? "bounds=inside-volume" : "bounds=outside-volume");
        recu_candidate_confidence_add(&c, c.likely_overwritten ? -30 : 25, c.likely_overwritten ? "allocation=reused" : "allocation=free");
        recu_candidate_confidence_add(&c, c.no_fat_chain ? 8 : 0, c.no_fat_chain ? "method=exfat-contiguous" : "method=exfat-fat-chain");
    }
    if (c.is_directory || zero_payload) {
        c.validation_checked = 1;
        c.validation = empty_known_binary ? RECU_VALIDATION_DAMAGED : RECU_VALIDATION_UNSUPPORTED;
        recu_candidate_apply_validation_confidence(&c);
        snprintf(c.note, sizeof(c.note), "%s", c.is_directory ? "deleted directory entry; not a recoverable file" : "zero-byte file entry; no payload to recover");
    } else {
        snprintf(c.note, sizeof(c.note), "%s; %s",
                 c.likely_overwritten ? "bitmap says clusters may be reused" : "bitmap says first clusters are free",
                 c.no_fat_chain ? "NoFatChain contiguous stream" : "chain-based stream");
    }
    recu_list_push(ctx->out, &c, ctx->err);
}

static void scan_exfat_directory(ExfatCtx *ctx, const char *dir_path, uint32_t first_cluster, uint64_t length_hint, int no_fat_chain, int depth);

static void maybe_recurse_active_dir(ExfatCtx *ctx, const uint8_t *file_entry, const uint8_t *stream, const uint8_t *names, int name_count, const char *dir_path, int depth) {
    if (!(recu_le16(file_entry + 4) & EXFAT_ATTR_DIR)) return;
    char name[RECU_MAX_NAME];
    exfat_name_from_entries(names, name_count, 0, name, sizeof(name));
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;
    uint32_t first_cluster = recu_le32(stream + 20);
    uint64_t data_len = recu_le64(stream + 24);
    int no_fat_chain = (stream[1] & 0x02) != 0;
    if (first_cluster < 2) return;
    char child_path[RECU_MAX_PATH];
    if (dir_path && *dir_path) snprintf(child_path, sizeof(child_path), "%s/%s", dir_path, name);
    else recu_safe_copy(child_path, sizeof(child_path), name);
    scan_exfat_directory(ctx, child_path, first_cluster, data_len, no_fat_chain, depth + 1);
}

static void discover_bitmap_from_dir(ExfatCtx *ctx, const uint8_t *dir, size_t dir_size) {
    for (size_t off = 0; off + 32 <= dir_size; off += 32) {
        const uint8_t *e = dir + off;
        if (e[0] == EXFAT_TYPE_END) break;
        if (e[0] == EXFAT_TYPE_BITMAP) {
            uint32_t cluster = recu_le32(e + 20);
            uint64_t size = recu_le64(e + 24);
            if (cluster >= 2 && size > 0 && size < 128u * 1024u * 1024u) {
                size_t got = 0;
                uint8_t *b = read_exfat_stream(ctx, cluster, size, 1, &got);
                if (b && got == size) {
                    free(ctx->bitmap);
                    ctx->bitmap = b;
                    ctx->bitmap_size = got;
                    ctx->vol->exfat_allocation_bitmap_cluster = cluster;
                    ctx->vol->exfat_allocation_bitmap_offset = recu_cluster_offset(ctx->vol, cluster);
                    ctx->vol->exfat_allocation_bitmap_size = size;
                } else {
                    free(b);
                }
            }
        }
    }
}

static void scan_exfat_directory(ExfatCtx *ctx, const char *dir_path, uint32_t first_cluster, uint64_t length_hint, int no_fat_chain, int depth) {
    if (depth > 24) return;
    size_t dir_size = 0;
    uint8_t *dir = read_exfat_directory(ctx, first_cluster, length_hint, no_fat_chain, &dir_size);
    if (!dir) return;
    if (depth == 0 && !ctx->bitmap) discover_bitmap_from_dir(ctx, dir, dir_size);

    for (size_t off = 0; off + 32 <= dir_size; off += 32) {
        uint8_t type = dir[off];
        if (type == EXFAT_TYPE_END) break;
        if (type == EXFAT_TYPE_FILE || type == EXFAT_TYPE_FILE_DEL) {
            int deleted = type == EXFAT_TYPE_FILE_DEL;
            uint8_t secondary_count = dir[off + 1];
            if (secondary_count == 0 || off + (size_t)(secondary_count + 1) * 32 > dir_size) continue;
            const uint8_t *stream = dir + off + 32;
            uint8_t expected_stream = deleted ? EXFAT_TYPE_STREAM_DEL : EXFAT_TYPE_STREAM;
            if (stream[0] != expected_stream) {
                off += (size_t)secondary_count * 32u;
                continue;
            }
            int name_count = 0;
            uint8_t names[18 * 32];
            memset(names, 0, sizeof(names));
            for (int i = 1; i < secondary_count && name_count < 18; i++) {
                const uint8_t *ne = dir + off + (size_t)(i + 1) * 32u;
                uint8_t expected_name = deleted ? EXFAT_TYPE_NAME_DEL : EXFAT_TYPE_NAME;
                if (ne[0] == expected_name) {
                    memcpy(names + name_count * 32, ne, 32);
                    name_count++;
                }
            }
            if (deleted) {
                exfat_add_candidate(ctx, dir + off, stream, names, name_count, dir_path);
            } else {
                maybe_recurse_active_dir(ctx, dir + off, stream, names, name_count, dir_path, depth);
            }
            off += (size_t)secondary_count * 32u;
        }
    }
    free(dir);
}

int recu_scan_exfat(RecuSource *src, RecuVolumeInfo *vol, RecuCandidateList *out, const RecuScanOptions *opt, RecuError *err) {
    if (!src || !vol || !out) return 0;
    if (vol->fs_type != RECU_FS_EXFAT) {
        recu_error_set(err, "not an exFAT volume");
        return 0;
    }
    ExfatCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.src = src;
    ctx.vol = vol;
    ctx.out = out;
    ctx.opt = opt;
    ctx.err = err;
    if (opt && opt->progress) opt->progress(opt->progress_user, "exFAT quick scan", 0, 1);
    scan_exfat_directory(&ctx, "", vol->root_cluster, 0, 0, 0);
    free(ctx.bitmap);
    if (opt && opt->progress) opt->progress(opt->progress_user, "exFAT quick scan", 1, 1);
    return 1;
}
