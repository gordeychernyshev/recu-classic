#include "recu.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sys/stat.h>

#ifdef _WIN32
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

static FILE *fopen_write_utf8(const char *path) {
    wchar_t wpath[RECU_MAX_PATH];
    if (!utf8_to_wpath(path, wpath, sizeof(wpath) / sizeof(wpath[0]))) return NULL;
    return _wfopen(wpath, L"wb");
}

static void delete_file_utf8(const char *path) {
    wchar_t wpath[RECU_MAX_PATH];
    if (utf8_to_wpath(path, wpath, sizeof(wpath) / sizeof(wpath[0]))) {
        DeleteFileW(wpath);
    }
}

static int same_file_path_utf8(const char *a, const char *b) {
    wchar_t wa[RECU_MAX_PATH];
    wchar_t wb[RECU_MAX_PATH];
    wchar_t fa[RECU_MAX_PATH];
    wchar_t fb[RECU_MAX_PATH];
    if (!utf8_to_wpath(a, wa, sizeof(wa) / sizeof(wa[0])) ||
        !utf8_to_wpath(b, wb, sizeof(wb) / sizeof(wb[0]))) {
        return 0;
    }
    DWORD na = GetFullPathNameW(wa, (DWORD)(sizeof(fa) / sizeof(fa[0])), fa, NULL);
    DWORD nb = GetFullPathNameW(wb, (DWORD)(sizeof(fb) / sizeof(fb[0])), fb, NULL);
    if (na == 0 || na >= sizeof(fa) / sizeof(fa[0]) ||
        nb == 0 || nb >= sizeof(fb) / sizeof(fb[0])) {
        return _wcsicmp(wa, wb) == 0;
    }
    return _wcsicmp(fa, fb) == 0;
}
#else
static FILE *fopen_write_utf8(const char *path) {
    return fopen(path, "wb");
}

static void delete_file_utf8(const char *path) {
    remove(path);
}

static int same_file_path_utf8(const char *a, const char *b) {
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}
#endif

static int contains_ci_ascii(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return 0;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return 1;
    }
    return 0;
}

static int error_is_source_read_loss(const RecuError *err) {
    const char *m = err ? err->message : "";
    return contains_ci_ascii(m, "device disconnected") ||
           contains_ci_ascii(m, "became unreadable") ||
           contains_ci_ascii(m, "stopped responding") ||
           contains_ci_ascii(m, "read failed") ||
           contains_ci_ascii(m, "short read") ||
           contains_ci_ascii(m, "raw read timed out");
}

static uint32_t read_fat_entry(RecuSource *src, const RecuVolumeInfo *vol, uint32_t cluster) {
    uint8_t b[4] = {0};
    RecuError err;
    recu_error_clear(&err);
    if (cluster >= vol->cluster_count + 2) return 0xFFFFFFFFu;
    if (vol->fs_type == RECU_FS_FAT16) {
        if (!recu_source_read(src, vol->fat_offset + (uint64_t)cluster * 2u, b, 2, &err)) return 0xFFFFu;
        return recu_le16(b);
    }
    if (!recu_source_read(src, vol->fat_offset + (uint64_t)cluster * 4u, b, 4, &err)) return 0xFFFFFFFFu;
    if (vol->fs_type == RECU_FS_FAT32) return recu_le32(b) & 0x0FFFFFFFu;
    return recu_le32(b);
}

static int is_eoc(const RecuVolumeInfo *vol, uint32_t v) {
    if (vol->fs_type == RECU_FS_FAT16) return v >= 0xFFF8u;
    if (vol->fs_type == RECU_FS_FAT32) return v >= 0x0FFFFFF8u;
    if (vol->fs_type == RECU_FS_EXFAT) return v >= 0xFFFFFFF8u;
    return 1;
}

static int ensure_dir(const char *path) {
#ifdef _WIN32
    wchar_t wpath[RECU_MAX_PATH];
    if (!utf8_to_wpath(path, wpath, sizeof(wpath) / sizeof(wpath[0]))) return 0;
    return CreateDirectoryW(wpath, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(path, 0777) == 0 || errno == EEXIST;
#endif
}

static int file_exists_utf8(const char *path) {
    if (!path || !*path) return 0;
#ifdef _WIN32
    wchar_t wpath[RECU_MAX_PATH];
    if (!utf8_to_wpath(path, wpath, sizeof(wpath) / sizeof(wpath[0]))) return 0;
    DWORD attrs = GetFileAttributesW(wpath);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static void make_output_path(const RecuRecoverOptions *opt, const RecuCandidate *candidate, char *out, size_t out_size) {
    char name[RECU_MAX_PATH];
    if (opt->preserve_paths && candidate->path[0]) recu_safe_copy(name, sizeof(name), candidate->path);
    else recu_safe_copy(name, sizeof(name), candidate->name[0] ? candidate->name : "recovered.bin");
    for (char *p = name; *p; p++) {
        if (*p == '/') *p = '\\';
    }
    char safe[RECU_MAX_PATH];
    safe[0] = 0;
    if (opt->preserve_paths && strchr(name, '\\')) {
        const char *p = name;
        while (*p) {
            char part[RECU_MAX_NAME];
            size_t j = 0;
            while (*p && *p != '\\' && j + 1 < sizeof(part)) part[j++] = *p++;
            part[j] = 0;
            while (*p == '\\') p++;
            if (strcmp(part, ".") == 0 || strcmp(part, "..") == 0 || part[0] == 0) continue;
            recu_sanitize_filename(part);
            if (safe[0]) recu_safe_append(safe, sizeof(safe), "\\");
            recu_safe_append(safe, sizeof(safe), part);
        }
    } else {
        recu_sanitize_filename(name);
        char prefix[16];
        snprintf(prefix, sizeof(prefix), "%04u_", candidate->id);
        recu_safe_copy(safe, sizeof(safe), prefix);
        recu_safe_append(safe, sizeof(safe), name);
    }
    recu_path_join(out, out_size, opt->output_dir, safe);
}

static int make_unique_output_path(char *path, size_t path_size) {
    if (!path || path_size == 0) return 0;
    if (!file_exists_utf8(path)) return 1;
    char original[RECU_MAX_PATH];
    recu_safe_copy(original, sizeof(original), path);
    char *slash = strrchr(original, '\\');
    char *slash2 = strrchr(original, '/');
    if (!slash || (slash2 && slash2 > slash)) slash = slash2;
    char *base = slash ? slash + 1 : original;
    char dir[RECU_MAX_PATH];
    if (slash) {
        size_t dir_len = (size_t)(slash - original);
        if (dir_len >= sizeof(dir)) dir_len = sizeof(dir) - 1;
        memcpy(dir, original, dir_len);
        dir[dir_len] = 0;
    } else {
        dir[0] = 0;
    }
    char stem[RECU_MAX_NAME];
    char ext[64];
    recu_safe_copy(stem, sizeof(stem), base);
    ext[0] = 0;
    char *dot = strrchr(stem, '.');
    if (dot && dot != stem) {
        recu_safe_copy(ext, sizeof(ext), dot);
        *dot = 0;
    }
    for (int i = 1; i < 10000; i++) {
        char candidate[RECU_MAX_PATH];
        char file[RECU_MAX_NAME + 80];
        snprintf(file, sizeof(file), "%s_%03d%s", stem, i, ext);
        if (dir[0]) recu_path_join(candidate, sizeof(candidate), dir, file);
        else recu_safe_copy(candidate, sizeof(candidate), file);
        if (!file_exists_utf8(candidate)) {
            recu_safe_copy(path, path_size, candidate);
            return 1;
        }
    }
    return 0;
}

static int copy_range(RecuSource *src, FILE *out, uint64_t offset, uint64_t size, RecuProgressFn progress, void *progress_user, const char *stage, RecuError *err) {
    uint8_t *buf = (uint8_t *)malloc(1024u * 1024u);
    if (!buf) {
        recu_error_set(err, "out of memory during copy");
        return 0;
    }
    uint64_t done = 0;
    while (done < size) {
        size_t chunk = (size - done) > (1024u * 1024u) ? (1024u * 1024u) : (size_t)(size - done);
        if (!recu_source_read(src, offset + done, buf, chunk, err)) {
            free(buf);
            return 0;
        }
        if (fwrite(buf, 1, chunk, out) != chunk) {
            free(buf);
            recu_error_set(err, "write failed while recovering file");
            return 0;
        }
        done += chunk;
        if (progress && !progress(progress_user, stage, done, size)) {
            free(buf);
            recu_error_set(err, "operation cancelled");
            return 0;
        }
    }
    free(buf);
    return 1;
}

static int copy_cluster_chain(RecuSource *src, const RecuVolumeInfo *vol, FILE *out, uint32_t first_cluster, uint64_t size, RecuProgressFn progress, void *progress_user, RecuError *err) {
    if (size == 0) return 1;
    if (first_cluster < 2) {
        recu_error_set(err, "invalid first cluster");
        return 0;
    }
    uint8_t *buf = (uint8_t *)malloc(vol->cluster_size);
    if (!buf) {
        recu_error_set(err, "out of memory during cluster-chain recovery");
        return 0;
    }
    uint64_t done = 0;
    uint32_t cluster = first_cluster;
    uint32_t guard = 0;
    while (done < size && cluster >= 2 && cluster < vol->cluster_count + 2 && guard++ < vol->cluster_count) {
        size_t chunk = vol->cluster_size;
        if (done + chunk > size) chunk = (size_t)(size - done);
        if (!recu_source_read(src, recu_cluster_offset(vol, cluster), buf, chunk, err)) {
            free(buf);
            return 0;
        }
        if (fwrite(buf, 1, chunk, out) != chunk) {
            free(buf);
            recu_error_set(err, "write failed during chain recovery");
            return 0;
        }
        done += chunk;
        if (progress && !progress(progress_user, "recover chain", done, size)) {
            free(buf);
            recu_error_set(err, "operation cancelled");
            return 0;
        }
        uint32_t next = read_fat_entry(src, vol, cluster);
        if (next == 0 || is_eoc(vol, next)) break;
        cluster = next;
    }
    free(buf);
    if (done < size) {
        recu_error_set(err, "cluster chain ended early after %llu of %llu bytes",
                       (unsigned long long)done, (unsigned long long)size);
        return 0;
    }
    return 1;
}

typedef struct CandidateRangeRef {
    size_t index;
    uint64_t start;
    uint64_t end;
} CandidateRangeRef;

static int compare_range_ref(const void *a, const void *b) {
    const CandidateRangeRef *ra = (const CandidateRangeRef *)a;
    const CandidateRangeRef *rb = (const CandidateRangeRef *)b;
    if (ra->start < rb->start) return -1;
    if (ra->start > rb->start) return 1;
    if (ra->end < rb->end) return -1;
    if (ra->end > rb->end) return 1;
    if (ra->index < rb->index) return -1;
    if (ra->index > rb->index) return 1;
    return 0;
}

static int candidate_range(const RecuCandidate *c, uint64_t source_size, uint64_t *start, uint64_t *end) {
    if (!c || c->size == 0 || !c->recoverable) return 0;
    if (source_size > 0 && c->offset >= source_size) return 0;
    if (c->size <= UINT64_MAX - c->offset) {
        uint64_t e = c->offset + c->size;
        if (source_size > 0 && e > source_size) e = source_size;
        if (e <= c->offset) return 0;
        *start = c->offset;
        *end = e;
        return 1;
    }
    if (source_size > c->offset) {
        *start = c->offset;
        *end = source_size;
        return 1;
    }
    return 0;
}

static int candidate_overlap_priority(const RecuCandidate *c) {
    if (!c || !c->recoverable || c->size == 0) return -1000;
    int p = c->confidence;
    if (c->kind == RECU_KIND_DELETED_ENTRY) p += 20;
    if (c->validation_checked) {
        if (c->validation == RECU_VALIDATION_VALID) p += 20;
        else if (c->validation == RECU_VALIDATION_PARTIAL) p -= 10;
        else if (c->validation == RECU_VALIDATION_DAMAGED) p -= 50;
    }
    if (c->likely_overwritten) p -= 15;
    return p;
}

static int choose_overlap_owner(const RecuCandidate *a, const RecuCandidate *b, int same_start) {
    if (same_start && a->kind != b->kind) {
        return a->kind == RECU_KIND_DELETED_ENTRY ? 0 : 1;
    }
    int pa = candidate_overlap_priority(a);
    int pb = candidate_overlap_priority(b);
    if (pa != pb) return pa > pb ? 0 : 1;
    return a->id <= b->id ? 0 : 1;
}

static void append_overlap_note(RecuCandidate *c, const char *message) {
    if (!c || !message || !*message) return;
    if (strstr(c->note, message)) return;
    if (c->note[0]) recu_safe_append(c->note, sizeof(c->note), "; ");
    recu_safe_append(c->note, sizeof(c->note), message);
}

static void mark_duplicate_candidate(RecuCandidate *c, uint32_t owner_id) {
    if (!c || c->duplicate_of) return;
    c->duplicate_of = owner_id;
    c->same_offset_as = owner_id;
    c->overlaps_with = owner_id;
    c->overlap_bytes = c->size;
    if (c->confidence > 25) c->confidence = 25;
    recu_candidate_confidence_add(c, 0, "overlap=duplicate");
    append_overlap_note(c, "duplicate byte range");
}

static void mark_same_offset_candidate(RecuCandidate *c, uint32_t owner_id, uint64_t overlap_bytes) {
    if (!c || c->duplicate_of || c->same_offset_as) return;
    c->same_offset_as = owner_id;
    c->overlaps_with = owner_id;
    c->overlap_bytes = overlap_bytes;
    if (c->confidence > 45) c->confidence = 45;
    recu_candidate_confidence_add(c, 0, "overlap=same-offset");
    append_overlap_note(c, "same data offset as another candidate");
}

static void mark_overlap_candidate(RecuCandidate *c, uint32_t owner_id, uint64_t overlap_bytes) {
    if (!c || c->duplicate_of || c->same_offset_as) return;
    if (c->overlaps_with && c->overlap_bytes >= overlap_bytes) return;
    c->overlaps_with = owner_id;
    c->overlap_bytes = overlap_bytes;
    if (c->confidence > 55) c->confidence = 55;
    recu_candidate_confidence_add(c, 0, "overlap=range");
    append_overlap_note(c, "data range overlaps another candidate");
}

static void mark_overlap_pair(RecuCandidate *a, RecuCandidate *b, const CandidateRangeRef *ra, const CandidateRangeRef *rb) {
    uint64_t start = ra->start > rb->start ? ra->start : rb->start;
    uint64_t end = ra->end < rb->end ? ra->end : rb->end;
    if (end <= start) return;
    uint64_t bytes = end - start;
    int same_start = ra->start == rb->start;
    int same_range = same_start && ra->end == rb->end;
    int owner_is_a = choose_overlap_owner(a, b, same_start) == 0;
    RecuCandidate *owner = owner_is_a ? a : b;
    RecuCandidate *weaker = owner_is_a ? b : a;
    if (same_range) {
        mark_duplicate_candidate(weaker, owner->id);
    } else if (same_start) {
        mark_same_offset_candidate(weaker, owner->id, bytes);
    } else {
        mark_overlap_candidate(weaker, owner->id, bytes);
    }
}

void recu_analyze_candidate_overlaps(RecuSource *src, RecuCandidateList *list) {
    if (!src || !list || list->count < 2) return;
    CandidateRangeRef *refs = (CandidateRangeRef *)malloc(list->count * sizeof(*refs));
    if (!refs) return;
    size_t n = 0;
    for (size_t i = 0; i < list->count; i++) {
        uint64_t start = 0;
        uint64_t end = 0;
        if (!candidate_range(&list->items[i], src->size, &start, &end)) continue;
        refs[n].index = i;
        refs[n].start = start;
        refs[n].end = end;
        n++;
    }
    qsort(refs, n, sizeof(*refs), compare_range_ref);
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 1; j < n && refs[j].start < refs[i].end; j++) {
            RecuCandidate *a = &list->items[refs[i].index];
            RecuCandidate *b = &list->items[refs[j].index];
            mark_overlap_pair(a, b, &refs[i], &refs[j]);
        }
    }
    free(refs);
}

int recu_scan_source(RecuSource *src, RecuScanReport *report, const RecuScanOptions *opt, RecuError *err) {
    if (!src || !report) return 0;
    memset(report, 0, sizeof(*report));
    if (!recu_list_init(&report->candidates)) {
        recu_error_set(err, "cannot initialize candidate list");
        return 0;
    }
    if (!recu_probe_volume(src, &report->volume, err)) {
        recu_list_free(&report->candidates);
        return 0;
    }
    RecuScanOptions defaults;
    memset(&defaults, 0, sizeof(defaults));
    defaults.quick_scan = 1;
    defaults.deep_scan = 0;
    defaults.max_carved_files = 2048;
    defaults.max_carve_bytes = 512ull * 1024ull * 1024ull;
    if (!opt) opt = &defaults;

    if (opt->quick_scan) {
        if (report->volume.fs_type == RECU_FS_FAT16 || report->volume.fs_type == RECU_FS_FAT32) {
            if (!recu_scan_fat(src, &report->volume, &report->candidates, opt, err)) {
                recu_list_free(&report->candidates);
                return 0;
            }
        } else if (report->volume.fs_type == RECU_FS_EXFAT) {
            if (!recu_scan_exfat(src, &report->volume, &report->candidates, opt, err)) {
                recu_list_free(&report->candidates);
                return 0;
            }
        }
    }
    if (opt->deep_scan) {
        if (!recu_carve_scan(src, &report->volume, &report->candidates, opt, err)) {
            recu_list_free(&report->candidates);
            return 0;
        }
    }
    recu_enrich_scan_metadata(src, &report->volume, &report->candidates);
    recu_analyze_candidate_overlaps(src, &report->candidates);
    return 1;
}

void recu_scan_report_free(RecuScanReport *report) {
    if (!report) return;
    recu_list_free(&report->candidates);
}

int recu_recover_candidate(RecuSource *src, const RecuVolumeInfo *vol, const RecuCandidate *candidate, const RecuRecoverOptions *opt, char *written_path, size_t written_path_size, RecuError *err) {
    if (!src || !vol || !candidate || !opt || !opt->output_dir) {
        recu_error_set(err, "invalid recovery arguments");
        return 0;
    }
    if (!candidate->recoverable) {
        recu_error_set(err, "candidate is not recoverable");
        return 0;
    }
    ensure_dir(opt->output_dir);
    char path[RECU_MAX_PATH];
    make_output_path(opt, candidate, path, sizeof(path));
    if (!make_unique_output_path(path, sizeof(path))) {
        recu_error_set(err, "cannot choose a unique output file name for '%s'", path);
        return 0;
    }
    recu_make_parent_dirs(path);
    FILE *f = fopen_write_utf8(path);
    if (!f) {
        recu_error_set(err, "cannot create output file '%s'", path);
        return 0;
    }

    int ok = 0;
    if (candidate->kind == RECU_KIND_CARVED) {
        ok = copy_range(src, f, candidate->offset, candidate->size, opt->progress, opt->progress_user, "recover carved", err);
    } else if (candidate->size == 0) {
        ok = 1;
    } else if (candidate->fs_type == RECU_FS_EXFAT && !candidate->no_fat_chain) {
        ok = copy_cluster_chain(src, vol, f, candidate->first_cluster, candidate->size, opt->progress, opt->progress_user, err);
    } else {
        ok = copy_range(src, f, candidate->offset, candidate->size, opt->progress, opt->progress_user, "recover contiguous", err);
    }
    fclose(f);
    if (!ok) {
        delete_file_utf8(path);
        return 0;
    }
    if (written_path) recu_safe_copy(written_path, written_path_size, path);
    return 1;
}

int recu_create_image(RecuSource *src, const char *output_path, RecuProgressFn progress, void *progress_user, RecuError *err) {
    if (!src || !output_path) {
        recu_error_set(err, "invalid image arguments");
        return 0;
    }
    if (!src->is_raw_device && same_file_path_utf8(src->path, output_path)) {
        recu_error_set(err, "output image path is the same as the source image");
        return 0;
    }
    recu_make_parent_dirs(output_path);
    FILE *out = fopen_write_utf8(output_path);
    if (!out) {
        recu_error_set(err, "cannot create image '%s'", output_path);
        return 0;
    }
    int ok = copy_range(src, out, 0, src->size, progress, progress_user, "create image", err);
    if (fclose(out) != 0) {
        if (ok) recu_error_set(err, "write failed while finalizing image '%s'", output_path);
        ok = 0;
    }
    if (!ok) {
        char original[RECU_MAX_ERR];
        recu_safe_copy(original, sizeof(original), err ? err->message : "");
        delete_file_utf8(output_path);
        if (error_is_source_read_loss(err)) {
            recu_error_set(err, "%s. Incomplete image was deleted; reconnect the source and try again.", original);
        }
    }
    return ok;
}

static void csv_escape(FILE *f, const char *s) {
    fputc('"', f);
    for (; s && *s; s++) {
        if (*s == '"') fputc('"', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

static void csv_key_value(FILE *f, const char *key, const char *value) {
    fprintf(f, "%s,", key);
    csv_escape(f, value);
    fputc('\n', f);
}

static void json_escape(FILE *f, const char *s) {
    fputc('"', f);
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"': fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\b': fputs("\\b", f); break;
            case '\f': fputs("\\f", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:
                if (c < 32) fprintf(f, "\\u%04x", c);
                else fputc(c, f);
                break;
        }
    }
    fputc('"', f);
}

static const char *candidate_risk(const RecuCandidate *c) {
    if (!c) return "unknown";
    if (c->duplicate_of) return "duplicate";
    if (c->same_offset_as) return "same-offset";
    if (c->overlaps_with) return "overlap";
    if (c->validation_checked && c->validation == RECU_VALIDATION_DAMAGED) return "damaged";
    if (c->validation_checked && c->validation == RECU_VALIDATION_PARTIAL) return "partial";
    if (c->likely_overwritten) return "possibly-overwritten";
    if (c->confidence < 50) return "low-confidence";
    if (c->validation_checked && c->validation == RECU_VALIDATION_VALID) return "valid";
    return "unknown";
}

static const char *scan_info_value(const char *value) {
    return value ? value : "";
}

static void write_scan_report_csv(FILE *f, const RecuVolumeInfo *vol, const RecuCandidateList *list, const RecuScanReportInfo *info) {
    fputs("\xEF\xBB\xBF", f);
    csv_key_value(f, "product", scan_info_value(info ? info->product_name : "Recu Classic"));
    csv_key_value(f, "created_at", scan_info_value(info ? info->created_at : ""));
    csv_key_value(f, "source", scan_info_value(info ? info->source_path : ""));
    csv_key_value(f, "scan_mode", scan_info_value(info ? info->scan_mode : ""));
    csv_key_value(f, "fs", recu_fs_name(vol->fs_type));
    fprintf(f, "cluster_size,%u\ntotal_bytes,%llu\n",
            vol->cluster_size, (unsigned long long)vol->total_bytes);
    fprintf(f, "total_candidates,%llu\n", (unsigned long long)list->count);
    if (info) fprintf(f, "visible_candidates,%llu\n", (unsigned long long)info->visible_count);
    fputc('\n', f);
    fprintf(f, "id,kind,fs,name,path,format,size,offset,first_cluster,confidence,confidence_reasons,duplicate_of,same_offset_as,overlaps_with,overlap_bytes,validation,risk,overwritten,recoverable,note,photo_datetime,camera_make,camera_model,photo_width,photo_height,orientation,gps,software\n");
    for (size_t i = 0; i < list->count; i++) {
        const RecuCandidate *c = &list->items[i];
        fprintf(f, "%u,%s,%s,", c->id, c->kind == RECU_KIND_CARVED ? "carved" : "deleted-entry", recu_fs_name(c->fs_type));
        csv_escape(f, c->name);
        fputc(',', f);
        csv_escape(f, c->path);
        fprintf(f, ",%s,%llu,%llu,%u,%d,",
                recu_format_name(c->format),
                (unsigned long long)c->size,
                (unsigned long long)c->offset,
                c->first_cluster,
                c->confidence);
        csv_escape(f, c->confidence_reasons);
        fprintf(f, ",%u,%u,%u,%llu,%s,%s,%d,%d,",
                c->duplicate_of,
                c->same_offset_as,
                c->overlaps_with,
                (unsigned long long)c->overlap_bytes,
                c->validation_checked ? recu_validation_name(c->validation) : "unchecked",
                candidate_risk(c),
                c->likely_overwritten,
                c->recoverable);
        csv_escape(f, c->note);
        fputc(',', f);
        csv_escape(f, c->photo_datetime);
        fputc(',', f);
        csv_escape(f, c->photo_make);
        fputc(',', f);
        csv_escape(f, c->photo_model);
        fprintf(f, ",%u,%u,%u,",
                c->photo_width,
                c->photo_height,
                c->photo_orientation);
        csv_escape(f, c->photo_gps);
        fputc(',', f);
        csv_escape(f, c->photo_software);
        fputc('\n', f);
    }
}

static void write_candidate_json(FILE *f, const RecuCandidate *c) {
    fprintf(f, "{");
    fprintf(f, "\"id\":%u,", c->id);
    fprintf(f, "\"kind\":");
    json_escape(f, c->kind == RECU_KIND_CARVED ? "carved" : "deleted-entry");
    fprintf(f, ",\"fs\":");
    json_escape(f, recu_fs_name(c->fs_type));
    fprintf(f, ",\"name\":");
    json_escape(f, c->name);
    fprintf(f, ",\"path\":");
    json_escape(f, c->path);
    fprintf(f, ",\"format\":");
    json_escape(f, recu_format_name(c->format));
    fprintf(f, ",\"size\":%llu,\"offset\":%llu,\"first_cluster\":%u,\"confidence\":%d,",
            (unsigned long long)c->size, (unsigned long long)c->offset, c->first_cluster, c->confidence);
    fprintf(f, "\"confidence_reasons\":");
    json_escape(f, c->confidence_reasons);
    fprintf(f, ",\"duplicate_of\":%u,\"same_offset_as\":%u,\"overlaps_with\":%u,\"overlap_bytes\":%llu,",
            c->duplicate_of, c->same_offset_as, c->overlaps_with, (unsigned long long)c->overlap_bytes);
    fprintf(f, "\"validation\":");
    json_escape(f, c->validation_checked ? recu_validation_name(c->validation) : "unchecked");
    fprintf(f, ",\"risk\":");
    json_escape(f, candidate_risk(c));
    fprintf(f, ",\"overwritten\":%s,\"recoverable\":%s,", c->likely_overwritten ? "true" : "false", c->recoverable ? "true" : "false");
    fprintf(f, "\"note\":");
    json_escape(f, c->note);
    fprintf(f, ",\"photo_datetime\":");
    json_escape(f, c->photo_datetime);
    fprintf(f, ",\"camera_make\":");
    json_escape(f, c->photo_make);
    fprintf(f, ",\"camera_model\":");
    json_escape(f, c->photo_model);
    fprintf(f, ",\"photo_width\":%u,\"photo_height\":%u,\"orientation\":%u,",
            c->photo_width, c->photo_height, c->photo_orientation);
    fprintf(f, "\"gps\":");
    json_escape(f, c->photo_gps);
    fprintf(f, ",\"software\":");
    json_escape(f, c->photo_software);
    fprintf(f, "}");
}

static void write_scan_report_json(FILE *f, const RecuVolumeInfo *vol, const RecuCandidateList *list, const RecuScanReportInfo *info) {
    fprintf(f, "{\n  \"product\": ");
    json_escape(f, scan_info_value(info ? info->product_name : "Recu Classic"));
    fprintf(f, ",\n  \"created_at\": ");
    json_escape(f, scan_info_value(info ? info->created_at : ""));
    fprintf(f, ",\n  \"source\": ");
    json_escape(f, scan_info_value(info ? info->source_path : ""));
    fprintf(f, ",\n  \"scan_mode\": ");
    json_escape(f, scan_info_value(info ? info->scan_mode : ""));
    fprintf(f, ",\n  \"filesystem\": ");
    json_escape(f, recu_fs_name(vol->fs_type));
    fprintf(f, ",\n  \"cluster_size\": %u,\n  \"total_bytes\": %llu,\n  \"total_candidates\": %llu",
            vol->cluster_size, (unsigned long long)vol->total_bytes, (unsigned long long)list->count);
    if (info) fprintf(f, ",\n  \"visible_candidates\": %llu", (unsigned long long)info->visible_count);
    fprintf(f, ",\n  \"candidates\": [\n");
    for (size_t i = 0; i < list->count; i++) {
        fputs("    ", f);
        write_candidate_json(f, &list->items[i]);
        fputs(i + 1 < list->count ? ",\n" : "\n", f);
    }
    fprintf(f, "  ]\n}\n");
}

static void write_scan_report_log(FILE *f, const RecuVolumeInfo *vol, const RecuCandidateList *list, const RecuScanReportInfo *info) {
    fprintf(f, "Recu Classic scan report\n");
    fprintf(f, "Product: %s\n", scan_info_value(info ? info->product_name : "Recu Classic"));
    fprintf(f, "Created: %s\n", scan_info_value(info ? info->created_at : ""));
    fprintf(f, "Source: %s\n", scan_info_value(info ? info->source_path : ""));
    fprintf(f, "Scan mode: %s\n", scan_info_value(info ? info->scan_mode : ""));
    fprintf(f, "Filesystem: %s\n", recu_fs_name(vol->fs_type));
    fprintf(f, "Cluster size: %u bytes\n", vol->cluster_size);
    fprintf(f, "Total bytes: %llu\n", (unsigned long long)vol->total_bytes);
    fprintf(f, "Total candidates: %llu\n", (unsigned long long)list->count);
    if (info) fprintf(f, "Visible candidates: %llu\n", (unsigned long long)info->visible_count);
    fprintf(f, "\nCandidates\n");
    fprintf(f, "----------\n");
    for (size_t i = 0; i < list->count; i++) {
        const RecuCandidate *c = &list->items[i];
        fprintf(f, "#%u %s %s %llu bytes offset=%llu cluster=%u confidence=%d%% validation=%s risk=%s recoverable=%s\n",
                c->id,
                c->kind == RECU_KIND_CARVED ? "carved" : "deleted-entry",
                recu_format_name(c->format),
                (unsigned long long)c->size,
                (unsigned long long)c->offset,
                c->first_cluster,
                c->confidence,
                c->validation_checked ? recu_validation_name(c->validation) : "unchecked",
                candidate_risk(c),
                c->recoverable ? "yes" : "no");
        fprintf(f, "  name: %s\n", c->name);
        fprintf(f, "  path: %s\n", c->path);
        fprintf(f, "  confidence reasons: %s\n", c->confidence_reasons);
        if (c->duplicate_of) fprintf(f, "  duplicate of: #%u\n", c->duplicate_of);
        else if (c->same_offset_as) fprintf(f, "  same offset as: #%u overlap=%llu bytes\n", c->same_offset_as, (unsigned long long)c->overlap_bytes);
        else if (c->overlaps_with) fprintf(f, "  overlaps with: #%u overlap=%llu bytes\n", c->overlaps_with, (unsigned long long)c->overlap_bytes);
        fprintf(f, "  note: %s\n", c->note);
        if (c->photo_datetime[0] || c->photo_make[0] || c->photo_model[0]) {
            fprintf(f, "  photo: %s %s %s %ux%u orientation=%u gps=%s\n",
                    c->photo_datetime, c->photo_make, c->photo_model,
                    c->photo_width, c->photo_height, c->photo_orientation, c->photo_gps);
        }
        fputc('\n', f);
    }
}

int recu_write_scan_report(const char *report_path, const RecuVolumeInfo *vol, const RecuCandidateList *list, const RecuScanReportInfo *info, RecuReportFormat format, RecuError *err) {
    FILE *f = fopen_write_utf8(report_path);
    if (!f) {
        recu_error_set(err, "cannot write report '%s'", report_path);
        return 0;
    }
    switch (format) {
        case RECU_REPORT_JSON:
            write_scan_report_json(f, vol, list, info);
            break;
        case RECU_REPORT_LOG:
            write_scan_report_log(f, vol, list, info);
            break;
        case RECU_REPORT_CSV:
        default:
            write_scan_report_csv(f, vol, list, info);
            break;
    }
    fclose(f);
    return 1;
}

int recu_write_report(const char *report_path, const RecuVolumeInfo *vol, const RecuCandidateList *list, RecuError *err) {
    RecuScanReportInfo info;
    memset(&info, 0, sizeof(info));
    info.product_name = "Recu Classic";
    info.visible_count = list ? list->count : 0;
    return recu_write_scan_report(report_path, vol, list, &info, RECU_REPORT_CSV, err);
}

static int looks_text(const uint8_t *buf, size_t n) {
    if (n == 0) return 0;
    size_t good = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = buf[i];
        if (c == 9 || c == 10 || c == 13 || (c >= 32 && c < 127) || c >= 0xC0) good++;
    }
    return good * 100 / n > 85;
}

static void append_photo_metadata_preview(char *text, size_t text_size, const RecuCandidate *candidate) {
    if (!candidate || !candidate->photo_metadata_present) return;
    recu_safe_append(text, text_size, "Photo metadata:\r\n");
    if (candidate->photo_datetime[0]) {
        recu_safe_append(text, text_size, "  Date taken: ");
        recu_safe_append(text, text_size, candidate->photo_datetime);
        recu_safe_append(text, text_size, "\r\n");
    }
    if (candidate->photo_make[0] || candidate->photo_model[0]) {
        recu_safe_append(text, text_size, "  Camera: ");
        recu_safe_append(text, text_size, candidate->photo_make);
        if (candidate->photo_make[0] && candidate->photo_model[0]) recu_safe_append(text, text_size, " ");
        recu_safe_append(text, text_size, candidate->photo_model);
        recu_safe_append(text, text_size, "\r\n");
    }
    if (candidate->photo_width || candidate->photo_height) {
        char line[96];
        snprintf(line, sizeof(line), "  Image size: %u x %u\r\n", candidate->photo_width, candidate->photo_height);
        recu_safe_append(text, text_size, line);
    }
    if (candidate->photo_orientation) {
        char line[64];
        snprintf(line, sizeof(line), "  Orientation: %u\r\n", candidate->photo_orientation);
        recu_safe_append(text, text_size, line);
    }
    if (candidate->photo_gps[0]) {
        recu_safe_append(text, text_size, "  GPS: ");
        recu_safe_append(text, text_size, candidate->photo_gps);
        recu_safe_append(text, text_size, "\r\n");
    }
    if (candidate->photo_software[0]) {
        recu_safe_append(text, text_size, "  Software: ");
        recu_safe_append(text, text_size, candidate->photo_software);
        recu_safe_append(text, text_size, "\r\n");
    }
    recu_safe_append(text, text_size, "\r\n");
}

int recu_preview_candidate(RecuSource *src, const RecuVolumeInfo *vol, const RecuCandidate *candidate, char *text, size_t text_size, RecuError *err) {
    if (!src || !vol || !candidate || !text || text_size == 0) return 0;
    text[0] = 0;
    uint64_t n64 = candidate->size;
    if (n64 > RECU_MAX_PREVIEW) n64 = RECU_MAX_PREVIEW;
    size_t n = (size_t)n64;
    if (candidate->offset > src->size || n64 > src->size - candidate->offset) {
        recu_error_set(err, "preview read is outside candidate bounds");
        return 0;
    }
    uint8_t *buf = (uint8_t *)malloc(n ? n : 1);
    if (!buf) {
        recu_error_set(err, "out of memory during preview");
        return 0;
    }
    uint64_t offset = candidate->offset;
    if (n > 0 && !recu_source_read(src, offset, buf, n, err)) {
        free(buf);
        return 0;
    }
    snprintf(text, text_size,
             "Name: %s\r\nKind: %s\r\nFS: %s\r\nFormat: %s\r\nSize: %llu bytes\r\nOffset: %llu\r\nFirst cluster: %u\r\nConfidence: %d%%\r\nConfidence reasons: %s\r\nNote: %s\r\n\r\n",
             candidate->name,
             candidate->kind == RECU_KIND_CARVED ? "carved" : "deleted entry",
             recu_fs_name(candidate->fs_type),
             recu_format_name(candidate->format),
             (unsigned long long)candidate->size,
             (unsigned long long)candidate->offset,
             candidate->first_cluster,
             candidate->confidence,
             candidate->confidence_reasons,
             candidate->note);
    append_photo_metadata_preview(text, text_size, candidate);
    if (n == 0) {
        recu_safe_append(text, text_size, "(empty file)");
        free(buf);
        return 1;
    }
    if (looks_text(buf, n)) {
        recu_safe_append(text, text_size, "Text preview:\r\n");
        size_t base = strlen(text);
        for (size_t i = 0; i < n && base + 4 < text_size; i++) {
            char c = (char)buf[i];
            if (c == '\n') {
                text[base++] = '\r';
                text[base++] = '\n';
            } else if (c == '\r') {
                continue;
            } else if ((unsigned char)c < 9) {
                text[base++] = '.';
            } else {
                text[base++] = c;
            }
        }
        text[base] = 0;
    } else {
        recu_safe_append(text, text_size, "Hex preview:\r\n");
        size_t lines = n < 512 ? n : 512;
        for (size_t i = 0; i < lines; i += 16) {
            char line[128];
            size_t p = 0;
            p += snprintf(line + p, sizeof(line) - p, "%08llX  ", (unsigned long long)i);
            for (size_t j = 0; j < 16 && i + j < lines; j++) {
                p += snprintf(line + p, sizeof(line) - p, "%02X ", buf[i + j]);
            }
            p += snprintf(line + p, sizeof(line) - p, "\r\n");
            recu_safe_append(text, text_size, line);
        }
    }
    free(buf);
    return 1;
}
