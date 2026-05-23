#include "recu.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0777)
#endif

typedef struct ByteBuf {
    uint8_t *data;
    size_t size;
    size_t cap;
} ByteBuf;

typedef struct Sample {
    const char *name;
    RecuFileFormat fmt;
    ByteBuf bytes;
} Sample;

static int failures = 0;

static void failf(const char *msg, const char *name) {
    fprintf(stderr, "FAIL: %s: %s\n", name, msg);
    failures++;
}

static int buf_reserve(ByteBuf *b, size_t extra) {
    if (extra > (size_t)-1 - b->size) return 0;
    size_t need = b->size + extra;
    if (need <= b->cap) return 1;
    size_t cap = b->cap ? b->cap : 128;
    while (cap < need) cap *= 2;
    uint8_t *p = (uint8_t *)realloc(b->data, cap);
    if (!p) return 0;
    b->data = p;
    b->cap = cap;
    return 1;
}

static int buf_put(ByteBuf *b, const void *data, size_t n) {
    if (!buf_reserve(b, n)) return 0;
    memcpy(b->data + b->size, data, n);
    b->size += n;
    return 1;
}

static int buf_zero(ByteBuf *b, size_t n) {
    if (!buf_reserve(b, n)) return 0;
    memset(b->data + b->size, 0, n);
    b->size += n;
    return 1;
}

static void be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int buf_put_le16(ByteBuf *b, uint16_t v) {
    uint8_t p[2];
    le16(p, v);
    return buf_put(b, p, sizeof(p));
}

static int buf_put_le32(ByteBuf *b, uint32_t v) {
    uint8_t p[4];
    le32(p, v);
    return buf_put(b, p, sizeof(p));
}

static int buf_put_be32(ByteBuf *b, uint32_t v) {
    uint8_t p[4];
    be32(p, v);
    return buf_put(b, p, sizeof(p));
}

static int box(ByteBuf *b, const char type[4], const void *payload, size_t payload_size) {
    uint8_t h[8];
    if (payload_size > UINT32_MAX - 8u) return 0;
    be32(h, (uint32_t)payload_size + 8u);
    memcpy(h + 4, type, 4);
    return buf_put(b, h, sizeof(h)) && buf_put(b, payload, payload_size);
}

static ByteBuf make_png(void) {
    ByteBuf b = {0};
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    uint8_t ihdr[13] = {0};
    uint8_t chunk[12];
    be32(ihdr, 1);
    be32(ihdr + 4, 1);
    ihdr[8] = 8;
    ihdr[9] = 2;
    buf_put(&b, sig, sizeof(sig));
    be32(chunk, sizeof(ihdr));
    memcpy(chunk + 4, "IHDR", 4);
    buf_put(&b, chunk, 8);
    buf_put(&b, ihdr, sizeof(ihdr));
    buf_zero(&b, 4);
    be32(chunk, 0);
    memcpy(chunk + 4, "IEND", 4);
    buf_put(&b, chunk, 8);
    buf_zero(&b, 4);
    return b;
}

static ByteBuf make_zip_family(const char *file_name, const char *payload_text, const char *extra_name) {
    ByteBuf b = {0};
    const char *payload = payload_text ? payload_text : "payload";
    size_t name_len = strlen(file_name);
    size_t payload_len = strlen(payload);
    uint32_t local_off = 0;
    buf_put_le32(&b, 0x04034B50u);
    buf_put_le16(&b, 20);
    buf_put_le16(&b, 0);
    buf_put_le16(&b, 0);
    buf_put_le16(&b, 0);
    buf_put_le16(&b, 0);
    buf_put_le32(&b, 0);
    buf_put_le32(&b, (uint32_t)payload_len);
    buf_put_le32(&b, (uint32_t)payload_len);
    buf_put_le16(&b, (uint16_t)name_len);
    buf_put_le16(&b, 0);
    buf_put(&b, file_name, name_len);
    buf_put(&b, payload, payload_len);
    if (extra_name) {
        size_t extra_len = strlen(extra_name);
        const char *extra_payload = "x";
        buf_put_le32(&b, 0x04034B50u);
        buf_put_le16(&b, 20);
        buf_put_le16(&b, 0);
        buf_put_le16(&b, 0);
        buf_put_le16(&b, 0);
        buf_put_le16(&b, 0);
        buf_put_le32(&b, 0);
        buf_put_le32(&b, 1);
        buf_put_le32(&b, 1);
        buf_put_le16(&b, (uint16_t)extra_len);
        buf_put_le16(&b, 0);
        buf_put(&b, extra_name, extra_len);
        buf_put(&b, extra_payload, 1);
    }
    uint32_t cd_off = (uint32_t)b.size;
    buf_put_le32(&b, 0x02014B50u);
    buf_put_le16(&b, 20);
    buf_put_le16(&b, 20);
    buf_put_le16(&b, 0);
    buf_put_le16(&b, 0);
    buf_put_le16(&b, 0);
    buf_put_le16(&b, 0);
    buf_put_le32(&b, 0);
    buf_put_le32(&b, (uint32_t)payload_len);
    buf_put_le32(&b, (uint32_t)payload_len);
    buf_put_le16(&b, (uint16_t)name_len);
    buf_put_le16(&b, 0);
    buf_put_le16(&b, 0);
    buf_put_le16(&b, 0);
    buf_put_le16(&b, 0);
    buf_put_le32(&b, 0);
    buf_put_le32(&b, local_off);
    buf_put(&b, file_name, name_len);
    uint32_t cd_size = (uint32_t)b.size - cd_off;
    buf_put_le32(&b, 0x06054B50u);
    buf_put_le16(&b, 0);
    buf_put_le16(&b, 0);
    buf_put_le16(&b, 1);
    buf_put_le16(&b, 1);
    buf_put_le32(&b, cd_size);
    buf_put_le32(&b, cd_off);
    buf_put_le16(&b, 0);
    return b;
}

static void put_tiff_entry(ByteBuf *b, uint16_t tag, uint16_t type, uint32_t count, uint32_t value) {
    buf_put_le16(b, tag);
    buf_put_le16(b, type);
    buf_put_le32(b, count);
    buf_put_le32(b, value);
}

static ByteBuf make_tiff_like(RecuFileFormat fmt) {
    ByteBuf b = {0};
    uint32_t ifd = fmt == RECU_FMT_CR2 ? 16 : 8;
    uint16_t entries = fmt == RECU_FMT_DNG ? 5 : 4;
    buf_put(&b, "II", 2);
    buf_put_le16(&b, 42);
    buf_put_le32(&b, ifd);
    if (fmt == RECU_FMT_CR2) {
        buf_put(&b, "CR\002\000", 4);
        buf_zero(&b, 4);
    }
    buf_put_le16(&b, entries);
    put_tiff_entry(&b, 256, 4, 1, 1);
    put_tiff_entry(&b, 257, 4, 1, 1);
    put_tiff_entry(&b, 273, 4, 1, (uint32_t)(ifd + 2 + entries * 12 + 4));
    put_tiff_entry(&b, 279, 4, 1, 4);
    if (fmt == RECU_FMT_DNG) put_tiff_entry(&b, 50706, 1, 4, 0x00000401);
    buf_put_le32(&b, 0);
    buf_zero(&b, 4);
    return b;
}

static ByteBuf make_raf(void) {
    ByteBuf b = {0};
    uint8_t h[108];
    memset(h, 0, sizeof(h));
    memcpy(h, "FUJIFILMCCD-RAW ", 16);
    be32(h + 84, 108);
    be32(h + 88, 4);
    be32(h + 92, 112);
    be32(h + 96, 4);
    be32(h + 100, 116);
    be32(h + 104, 4);
    buf_put(&b, h, sizeof(h));
    buf_zero(&b, 12);
    return b;
}

static ByteBuf make_psd(void) {
    ByteBuf b = {0};
    uint8_t h[26];
    memset(h, 0, sizeof(h));
    memcpy(h, "8BPS", 4);
    h[5] = 1;
    h[13] = 3;
    be32(h + 14, 1);
    be32(h + 18, 1);
    h[23] = 8;
    h[25] = 3;
    buf_put(&b, h, sizeof(h));
    buf_put_be32(&b, 0);
    buf_put_be32(&b, 0);
    buf_put_be32(&b, 0);
    buf_put_le16(&b, 0);
    buf_zero(&b, 3);
    return b;
}

static ByteBuf make_rar4(void) {
    ByteBuf b = {0};
    uint8_t sig[7] = {'R', 'a', 'r', '!', 0x1A, 0x07, 0x00};
    uint8_t main_h[7] = {0, 0, 0x73, 0, 0, 7, 0};
    uint8_t end_h[7] = {0, 0, 0x7B, 0, 0, 7, 0};
    buf_put(&b, sig, sizeof(sig));
    buf_put(&b, main_h, sizeof(main_h));
    buf_put(&b, end_h, sizeof(end_h));
    return b;
}

static ByteBuf make_ebml(const char *doctype) {
    ByteBuf b = {0};
    uint8_t ebml_id[4] = {0x1A, 0x45, 0xDF, 0xA3};
    uint8_t doc_id[2] = {0x42, 0x82};
    uint8_t seg_id[4] = {0x18, 0x53, 0x80, 0x67};
    uint8_t payload[8] = {1,2,3,4,5,6,7,8};
    size_t doc_len = strlen(doctype);
    buf_put(&b, ebml_id, sizeof(ebml_id));
    buf_put(&b, "\x80", 1);
    size_t size_pos = b.size - 1;
    size_t start = b.size;
    buf_put(&b, doc_id, sizeof(doc_id));
    uint8_t doc_size = (uint8_t)(0x80 | doc_len);
    buf_put(&b, &doc_size, 1);
    buf_put(&b, doctype, doc_len);
    b.data[size_pos] = (uint8_t)(0x80 | (b.size - start));
    buf_put(&b, seg_id, sizeof(seg_id));
    uint8_t seg_size = (uint8_t)(0x80 | sizeof(payload));
    buf_put(&b, &seg_size, 1);
    buf_put(&b, payload, sizeof(payload));
    return b;
}

static ByteBuf make_ts(int m2ts) {
    ByteBuf b = {0};
    size_t packet = m2ts ? 192 : 188;
    uint8_t pkt[192];
    for (int i = 0; i < 6; i++) {
        memset(pkt, 0xFF, packet);
        pkt[m2ts ? 4 : 0] = 0x47;
        buf_put(&b, pkt, packet);
    }
    return b;
}

static ByteBuf make_iso(const char major[4], const char compat_a[4], const char compat_b[4], const char *moov_text) {
    ByteBuf b = {0};
    uint8_t ftyp[16];
    uint8_t moov[24];
    uint8_t mdat[16];
    memcpy(ftyp, major, 4);
    memset(ftyp + 4, 0, 4);
    memcpy(ftyp + 8, compat_a, 4);
    memcpy(ftyp + 12, compat_b, 4);
    memset(moov, 0, sizeof(moov));
    memcpy(moov, moov_text, strlen(moov_text) < sizeof(moov) ? strlen(moov_text) : sizeof(moov));
    memset(mdat, 0xA5, sizeof(mdat));
    box(&b, "ftyp", ftyp, sizeof(ftyp));
    box(&b, "moov", moov, sizeof(moov));
    box(&b, "mdat", mdat, sizeof(mdat));
    return b;
}

static ByteBuf make_avi(void) {
    ByteBuf b = {0};
    uint8_t h[12];
    uint8_t data[32];
    memcpy(h, "RIFF", 4);
    le32(h + 4, (uint32_t)(sizeof(h) + sizeof(data) - 8));
    memcpy(h + 8, "AVI ", 4);
    memset(data, 0x22, sizeof(data));
    buf_put(&b, h, sizeof(h));
    buf_put(&b, data, sizeof(data));
    return b;
}

static ByteBuf make_bmp(void) {
    ByteBuf b = {0};
    uint8_t h[54];
    uint8_t px[4] = {0x00, 0x00, 0xFF, 0x00};
    memset(h, 0, sizeof(h));
    h[0] = 'B';
    h[1] = 'M';
    le32(h + 2, sizeof(h) + sizeof(px));
    le32(h + 10, sizeof(h));
    le32(h + 14, 40);
    le32(h + 18, 1);
    le32(h + 22, 1);
    le16(h + 26, 1);
    le16(h + 28, 24);
    le32(h + 34, sizeof(px));
    buf_put(&b, h, sizeof(h));
    buf_put(&b, px, sizeof(px));
    return b;
}

static ByteBuf make_ogg(void) {
    ByteBuf b = {0};
    uint8_t h[28];
    static const uint8_t payload[8] = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd'};
    memset(h, 0, sizeof(h));
    memcpy(h, "OggS", 4);
    h[5] = 0x04;
    h[26] = 1;
    h[27] = sizeof(payload);
    buf_put(&b, h, sizeof(h));
    buf_put(&b, payload, sizeof(payload));
    return b;
}

static ByteBuf make_flac(void) {
    ByteBuf b = {0};
    uint8_t meta[4 + 4 + 34];
    uint8_t audio = 0xFF;
    memcpy(meta, "fLaC", 4);
    meta[4] = 0x80;
    meta[5] = 0;
    meta[6] = 0;
    meta[7] = 34;
    memset(meta + 8, 0x11, 34);
    buf_put(&b, meta, sizeof(meta));
    buf_put(&b, &audio, 1);
    return b;
}

static ByteBuf make_mp3(void) {
    ByteBuf b = {0};
    uint8_t id3[10] = {'I', 'D', '3', 4, 0, 0, 0, 0, 0, 0};
    uint8_t frame[417];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xFF;
    frame[1] = 0xFB;
    frame[2] = 0x90;
    frame[3] = 0x64;
    buf_put(&b, id3, sizeof(id3));
    buf_put(&b, frame, sizeof(frame));
    buf_put(&b, frame, sizeof(frame));
    return b;
}

static ByteBuf make_aac(void) {
    ByteBuf b = {0};
    uint8_t frame[20];
    memset(frame, 0x55, sizeof(frame));
    frame[0] = 0xFF;
    frame[1] = 0xF1;
    frame[2] = 0x50;
    frame[3] = 0x80;
    frame[4] = 0x02;
    frame[5] = 0x80;
    frame[6] = 0xFC;
    buf_put(&b, frame, sizeof(frame));
    buf_put(&b, frame, sizeof(frame));
    return b;
}

static int write_file(const char *path, const ByteBuf *b) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(b->data, 1, b->size, f) == b->size;
    fclose(f);
    return ok;
}

static RecuValidationStatus validate_path(const char *path, RecuFileFormat fmt, uint64_t size) {
    RecuSource src;
    RecuError err;
    RecuCandidate c;
    recu_error_clear(&err);
    if (!recu_source_open(&src, path, &err)) {
        fprintf(stderr, "open failed: %s: %s\n", path, err.message);
        failures++;
        return RECU_VALIDATION_UNKNOWN;
    }
    memset(&c, 0, sizeof(c));
    c.kind = RECU_KIND_CARVED;
    c.format = fmt;
    c.size = size;
    c.recoverable = 1;
    recu_error_clear(&err);
    RecuValidationStatus st = recu_validate_candidate(&src, NULL, &c, &err);
    recu_source_close(&src);
    return st;
}

static void run_validation_tests(const Sample *samples, size_t count) {
    char path[256];
    for (size_t i = 0; i < count; i++) {
        snprintf(path, sizeof(path), "tests/synthetic-media/%s", samples[i].name);
        if (!write_file(path, &samples[i].bytes)) {
            failf("cannot write sample", samples[i].name);
            continue;
        }
        RecuValidationStatus st = validate_path(path, samples[i].fmt, samples[i].bytes.size);
        if (st != RECU_VALIDATION_VALID) {
            failf(recu_validation_name(st), samples[i].name);
        } else {
            printf("validator ok: %-12s %s\n", samples[i].name, recu_format_name(samples[i].fmt));
        }
    }
}

static void run_carve_test(const Sample *samples, size_t count) {
    const char *path = "tests/synthetic-media/carve.bin";
    FILE *f = fopen(path, "wb");
    if (!f) {
        failf("cannot write carve image", path);
        return;
    }
    for (size_t i = 0; i < count; i++) {
        uint8_t pad[19];
        memset(pad, (int)(0x30 + i), sizeof(pad));
        fwrite(pad, 1, sizeof(pad), f);
        fwrite(samples[i].bytes.data, 1, samples[i].bytes.size, f);
    }
    fclose(f);

    RecuSource src;
    RecuError err;
    RecuCandidateList list;
    RecuScanOptions opt;
    recu_error_clear(&err);
    if (!recu_source_open(&src, path, &err)) {
        failf(err.message, path);
        return;
    }
    if (!recu_list_init(&list)) {
        failf("cannot allocate candidate list", path);
        recu_source_close(&src);
        return;
    }
    memset(&opt, 0, sizeof(opt));
    opt.deep_scan = 1;
    opt.max_carved_files = 64;
    opt.max_carve_bytes = 8u * 1024u * 1024u;
    if (!recu_carve_scan(&src, NULL, &list, &opt, &err)) {
        failf(err.message, path);
        recu_list_free(&list);
        recu_source_close(&src);
        return;
    }

    for (size_t i = 0; i < count; i++) {
        int found = 0;
        RecuValidationStatus st = RECU_VALIDATION_UNKNOWN;
        for (size_t j = 0; j < list.count; j++) {
            if (list.items[j].format == samples[i].fmt) {
                found = 1;
                st = list.items[j].validation;
                break;
            }
        }
        if (!found) {
            failf("not carved", samples[i].name);
        } else if (samples[i].fmt != RECU_FMT_FLAC && st != RECU_VALIDATION_VALID) {
            failf(recu_validation_name(st), samples[i].name);
        } else {
            printf("carve ok:     %-12s %s (%s)\n", samples[i].name, recu_format_name(samples[i].fmt), recu_validation_name(st));
        }
    }

    RecuVolumeInfo vol;
    memset(&vol, 0, sizeof(vol));
    vol.fs_type = RECU_FS_UNKNOWN;
    vol.total_bytes = src.size;
    RecuScanReportInfo info;
    memset(&info, 0, sizeof(info));
    info.product_name = "Recu Classic synthetic test";
    info.source_path = path;
    info.scan_mode = "synthetic-deep";
    info.created_at = "2026-05-16 00:00:00";
    info.visible_count = list.count;
    const struct {
        const char *path;
        RecuReportFormat format;
    } reports[] = {
        {"tests/synthetic-media/report.csv", RECU_REPORT_CSV},
        {"tests/synthetic-media/report.json", RECU_REPORT_JSON},
        {"tests/synthetic-media/report.log", RECU_REPORT_LOG},
    };
    for (size_t i = 0; i < sizeof(reports) / sizeof(reports[0]); i++) {
        recu_error_clear(&err);
        if (!recu_write_scan_report(reports[i].path, &vol, &list, &info, reports[i].format, &err)) {
            failf(err.message, reports[i].path);
        } else {
            printf("report ok:    %-12s %s\n", reports[i].path + strlen("tests/synthetic-media/"), recu_report_format_name(reports[i].format));
        }
    }

    recu_list_free(&list);
    recu_source_close(&src);
}

static int file_bytes_equal(const char *path, const uint8_t *expected, size_t expected_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t buf[64];
    size_t got = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return got == expected_size && memcmp(buf, expected, expected_size) == 0;
}

static void run_preserve_paths_test(void) {
    const char *source_path = "tests/synthetic-media/recover-source.bin";
    const char *out_dir = "tests/synthetic-media/recover-out";
    const uint8_t payload[] = {'R', 'E', 'C', 'U', '!', 1, 2, 3};
    FILE *f = fopen(source_path, "wb");
    if (!f) {
        failf("cannot write source", source_path);
        return;
    }
    uint8_t pad[16];
    memset(pad, 0xAB, sizeof(pad));
    fwrite(pad, 1, sizeof(pad), f);
    fwrite(payload, 1, sizeof(payload), f);
    fclose(f);

    MKDIR(out_dir);
    remove("tests/synthetic-media/recover-out/DCIM/100CANON/IMG_0001.JPG");
    remove("tests/synthetic-media/recover-out/DCIM/100CANON/IMG_0001_001.JPG");
    remove("tests/synthetic-media/recover-out/DCIM/100CANON/IMG_0001_002.JPG");

    RecuSource src;
    RecuError err;
    recu_error_clear(&err);
    if (!recu_source_open(&src, source_path, &err)) {
        failf(err.message, source_path);
        return;
    }

    RecuVolumeInfo vol;
    memset(&vol, 0, sizeof(vol));
    vol.fs_type = RECU_FS_FAT16;
    RecuCandidate c;
    memset(&c, 0, sizeof(c));
    c.id = 1;
    c.kind = RECU_KIND_DELETED_ENTRY;
    c.fs_type = RECU_FS_FAT16;
    c.format = RECU_FMT_JPG;
    recu_safe_copy(c.name, sizeof(c.name), "IMG_0001.JPG");
    recu_safe_copy(c.path, sizeof(c.path), "DCIM/100CANON/IMG_0001.JPG");
    c.offset = sizeof(pad);
    c.size = sizeof(payload);
    c.recoverable = 1;

    RecuRecoverOptions opt;
    memset(&opt, 0, sizeof(opt));
    opt.output_dir = out_dir;
    opt.preserve_paths = 1;
    char written[RECU_MAX_PATH];
    if (!recu_recover_candidate(&src, &vol, &c, &opt, written, sizeof(written), &err)) {
        failf(err.message, "preserve paths first recover");
        recu_source_close(&src);
        return;
    }
    if (!strstr(written, "DCIM") || !strstr(written, "100CANON") || !file_bytes_equal(written, payload, sizeof(payload))) {
        failf("preserved path output mismatch", written);
    }
    char written2[RECU_MAX_PATH];
    if (!recu_recover_candidate(&src, &vol, &c, &opt, written2, sizeof(written2), &err)) {
        failf(err.message, "preserve paths unique recover");
        recu_source_close(&src);
        return;
    }
    if (strcmp(written, written2) == 0 || !strstr(written2, "_001.JPG") || !file_bytes_equal(written2, payload, sizeof(payload))) {
        failf("unique preserved path output mismatch", written2);
    } else {
        printf("recover ok:   preserve folders\n");
    }
    recu_source_close(&src);
}

static void run_overlap_test(void) {
    const char *source_path = "tests/synthetic-media/overlap-source.bin";
    FILE *f = fopen(source_path, "wb");
    if (!f) {
        failf("cannot write overlap source", source_path);
        return;
    }
    uint8_t zero[1024];
    memset(zero, 0, sizeof(zero));
    fwrite(zero, 1, sizeof(zero), f);
    fclose(f);

    RecuSource src;
    RecuError err;
    recu_error_clear(&err);
    if (!recu_source_open(&src, source_path, &err)) {
        failf(err.message, source_path);
        return;
    }

    RecuCandidateList list;
    recu_list_init(&list);
    RecuCandidate c;
    memset(&c, 0, sizeof(c));
    c.kind = RECU_KIND_DELETED_ENTRY;
    c.fs_type = RECU_FS_FAT32;
    c.format = RECU_FMT_JPG;
    recu_safe_copy(c.name, sizeof(c.name), "named.jpg");
    recu_safe_copy(c.path, sizeof(c.path), "DCIM/named.jpg");
    c.offset = 100;
    c.size = 100;
    c.confidence = 80;
    c.recoverable = 1;
    recu_list_push(&list, &c, &err);

    memset(&c, 0, sizeof(c));
    c.kind = RECU_KIND_CARVED;
    c.format = RECU_FMT_JPG;
    recu_safe_copy(c.name, sizeof(c.name), "carved_same.jpg");
    recu_safe_copy(c.path, sizeof(c.path), "carved/carved_same.jpg");
    c.offset = 100;
    c.size = 100;
    c.confidence = 78;
    c.recoverable = 1;
    c.validation_checked = 1;
    c.validation = RECU_VALIDATION_VALID;
    recu_list_push(&list, &c, &err);

    memset(&c, 0, sizeof(c));
    c.kind = RECU_KIND_CARVED;
    c.format = RECU_FMT_PNG;
    recu_safe_copy(c.name, sizeof(c.name), "same_offset.png");
    recu_safe_copy(c.path, sizeof(c.path), "carved/same_offset.png");
    c.offset = 100;
    c.size = 50;
    c.confidence = 76;
    c.recoverable = 1;
    c.validation_checked = 1;
    c.validation = RECU_VALIDATION_VALID;
    recu_list_push(&list, &c, &err);

    memset(&c, 0, sizeof(c));
    c.kind = RECU_KIND_CARVED;
    c.format = RECU_FMT_PDF;
    recu_safe_copy(c.name, sizeof(c.name), "overlap.pdf");
    recu_safe_copy(c.path, sizeof(c.path), "carved/overlap.pdf");
    c.offset = 150;
    c.size = 100;
    c.confidence = 74;
    c.recoverable = 1;
    c.validation_checked = 1;
    c.validation = RECU_VALIDATION_VALID;
    recu_list_push(&list, &c, &err);

    recu_analyze_candidate_overlaps(&src, &list);
    RecuCandidate *dup = &list.items[1];
    RecuCandidate *same = &list.items[2];
    RecuCandidate *overlap = &list.items[3];
    if (dup->duplicate_of != 1 || dup->confidence > 25 || !strstr(dup->confidence_reasons, "overlap=duplicate")) {
        failf("duplicate overlap marker failed", dup->name);
    } else if (same->same_offset_as != 1 || same->confidence > 45 || !strstr(same->confidence_reasons, "overlap=same-offset")) {
        failf("same-offset overlap marker failed", same->name);
    } else if (overlap->overlaps_with != 1 || overlap->overlap_bytes != 50 || overlap->confidence > 55 ||
               !strstr(overlap->confidence_reasons, "overlap=range")) {
        failf("range overlap marker failed", overlap->name);
    } else {
        printf("overlap ok:   duplicate/same-offset/range markers\n");
    }
    recu_list_free(&list);
    recu_source_close(&src);
}

int main(void) {
    MKDIR("tests");
    MKDIR("tests/synthetic-media");

    Sample samples[] = {
        {"sample.png", RECU_FMT_PNG, {0}},
        {"sample.mov", RECU_FMT_MOV, {0}},
        {"sample.mp4", RECU_FMT_MP4, {0}},
        {"sample.3gp", RECU_FMT_3GP, {0}},
        {"sample.3g2", RECU_FMT_3G2, {0}},
        {"sample.cr3", RECU_FMT_CR3, {0}},
        {"sample.mkv", RECU_FMT_MKV, {0}},
        {"sample.webm", RECU_FMT_WEBM, {0}},
        {"sample.ts", RECU_FMT_TS, {0}},
        {"sample.m2ts", RECU_FMT_M2TS, {0}},
        {"sample.avi", RECU_FMT_AVI, {0}},
        {"sample.bmp", RECU_FMT_BMP, {0}},
        {"sample.dng", RECU_FMT_DNG, {0}},
        {"sample.cr2", RECU_FMT_CR2, {0}},
        {"sample.raf", RECU_FMT_RAF, {0}},
        {"sample.psd", RECU_FMT_PSD, {0}},
        {"sample.rar", RECU_FMT_RAR, {0}},
        {"sample.odt", RECU_FMT_ODT, {0}},
        {"sample.epub", RECU_FMT_EPUB, {0}},
        {"sample.apk", RECU_FMT_APK, {0}},
        {"sample.jar", RECU_FMT_JAR, {0}},
        {"sample.ogg", RECU_FMT_OGG, {0}},
        {"sample.mp3", RECU_FMT_MP3, {0}},
        {"sample.m4a", RECU_FMT_ALAC, {0}},
        {"sample.aac", RECU_FMT_AAC, {0}},
        {"sample.flac", RECU_FMT_FLAC, {0}},
    };
    const size_t count = sizeof(samples) / sizeof(samples[0]);
    samples[0].bytes = make_png();
    samples[1].bytes = make_iso("qt  ", "qt  ", "wide", "quicktime");
    samples[2].bytes = make_iso("isom", "isom", "mp42", "mp4");
    samples[3].bytes = make_iso("3gp5", "3gp5", "isom", "3gp");
    samples[4].bytes = make_iso("3g2a", "3g2a", "isom", "3g2");
    samples[5].bytes = make_iso("crx ", "crx ", "isom", "cr3");
    samples[6].bytes = make_ebml("matroska");
    samples[7].bytes = make_ebml("webm");
    samples[8].bytes = make_ts(0);
    samples[9].bytes = make_ts(1);
    samples[10].bytes = make_avi();
    samples[11].bytes = make_bmp();
    samples[12].bytes = make_tiff_like(RECU_FMT_DNG);
    samples[13].bytes = make_tiff_like(RECU_FMT_CR2);
    samples[14].bytes = make_raf();
    samples[15].bytes = make_psd();
    samples[16].bytes = make_rar4();
    samples[17].bytes = make_zip_family("mimetype", "application/vnd.oasis.opendocument.text", "content.xml");
    samples[18].bytes = make_zip_family("mimetype", "application/epub+zip", "META-INF/container.xml");
    samples[19].bytes = make_zip_family("AndroidManifest.xml", "manifest", NULL);
    samples[20].bytes = make_zip_family("META-INF/MANIFEST.MF", "Manifest-Version: 1.0\n", NULL);
    samples[21].bytes = make_ogg();
    samples[22].bytes = make_mp3();
    samples[23].bytes = make_iso("M4A ", "M4A ", "alac", "alac");
    samples[24].bytes = make_aac();
    samples[25].bytes = make_flac();

    for (size_t i = 0; i < count; i++) {
        if (!samples[i].bytes.data || samples[i].bytes.size == 0) {
            failf("sample generation failed", samples[i].name);
        }
    }

    run_validation_tests(samples, count);
    run_carve_test(samples, count);
    run_preserve_paths_test();
    run_overlap_test();

    for (size_t i = 0; i < count; i++) free(samples[i].bytes.data);

    if (failures) {
        fprintf(stderr, "%d synthetic media test(s) failed\n", failures);
        return 1;
    }
    printf("synthetic media tests passed\n");
    return 0;
}
