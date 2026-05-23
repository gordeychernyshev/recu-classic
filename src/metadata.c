#include "recu.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JPEG_META_READ_LIMIT (1024u * 1024u)

static uint16_t be16m(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint16_t rd16(const uint8_t *p, int le) {
    return le ? (uint16_t)(p[0] | ((uint16_t)p[1] << 8)) : be16m(p);
}

static uint32_t rd32(const uint8_t *p, int le) {
    if (le) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int type_size(uint16_t type) {
    switch (type) {
        case 1:
        case 2:
        case 7:
            return 1;
        case 3:
            return 2;
        case 4:
        case 9:
            return 4;
        case 5:
        case 10:
            return 8;
        default:
            return 0;
    }
}

static int value_ptr(const uint8_t *base, size_t len, const uint8_t *entry, int le,
                     const uint8_t **ptr, size_t *bytes) {
    uint16_t type = rd16(entry + 2, le);
    uint32_t count = rd32(entry + 4, le);
    int unit = type_size(type);
    if (!unit || count > 0x10000000u) return 0;
    uint64_t total64 = (uint64_t)unit * count;
    if (total64 > len) return 0;
    size_t total = (size_t)total64;
    if (total <= 4) {
        *ptr = entry + 8;
        *bytes = total;
        return 1;
    }
    uint32_t off = rd32(entry + 8, le);
    if (off > len || total > len - off) return 0;
    *ptr = base + off;
    *bytes = total;
    return 1;
}

static void trim_ascii(char *s) {
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start) memmove(s, s + start, strlen(s + start) + 1);
}

static void copy_ascii_value(char *dst, size_t dst_size, const uint8_t *src, size_t n) {
    if (!dst || dst_size == 0) return;
    dst[0] = 0;
    if (!src || n == 0) return;
    size_t j = 0;
    for (size_t i = 0; i < n && j + 1 < dst_size; i++) {
        unsigned char c = src[i];
        if (c == 0) break;
        dst[j++] = (char)((c < 32 || c == 127) ? ' ' : c);
    }
    dst[j] = 0;
    trim_ascii(dst);
}

static int entry_ascii(const uint8_t *base, size_t len, const uint8_t *entry, int le, char *dst, size_t dst_size) {
    if (rd16(entry + 2, le) != 2) return 0;
    const uint8_t *p = NULL;
    size_t n = 0;
    if (!value_ptr(base, len, entry, le, &p, &n)) return 0;
    copy_ascii_value(dst, dst_size, p, n);
    return dst && dst[0];
}

static int entry_u32(const uint8_t *base, size_t len, const uint8_t *entry, int le, uint32_t *out) {
    uint16_t type = rd16(entry + 2, le);
    uint32_t count = rd32(entry + 4, le);
    const uint8_t *p = NULL;
    size_t n = 0;
    if (count < 1 || !value_ptr(base, len, entry, le, &p, &n)) return 0;
    if (type == 3 && n >= 2) {
        *out = rd16(p, le);
        return 1;
    }
    if ((type == 4 || type == 9) && n >= 4) {
        *out = rd32(p, le);
        return 1;
    }
    return 0;
}

static int read_rational3(const uint8_t *base, size_t len, const uint8_t *entry, int le, double out[3]) {
    if (rd16(entry + 2, le) != 5 || rd32(entry + 4, le) < 3) return 0;
    const uint8_t *p = NULL;
    size_t n = 0;
    if (!value_ptr(base, len, entry, le, &p, &n) || n < 24) return 0;
    for (int i = 0; i < 3; i++) {
        uint32_t num = rd32(p + i * 8, le);
        uint32_t den = rd32(p + i * 8 + 4, le);
        if (den == 0) return 0;
        out[i] = (double)num / (double)den;
    }
    return 1;
}

static int ifd_entry_range(const uint8_t *base, size_t len, uint32_t ifd_off, const uint8_t **entries, uint16_t *count) {
    if (ifd_off > len || len - ifd_off < 2) return 0;
    int le = base[0] == 'I';
    uint16_t n = rd16(base + ifd_off, le);
    if (n > 1024 || len - ifd_off - 2 < (size_t)n * 12u) return 0;
    *entries = base + ifd_off + 2;
    *count = n;
    return 1;
}

static void parse_gps_ifd(const uint8_t *base, size_t len, uint32_t ifd_off, int le, RecuCandidate *c) {
    const uint8_t *entries = NULL;
    uint16_t count = 0;
    char lat_ref[4] = "";
    char lon_ref[4] = "";
    double lat[3] = {0};
    double lon[3] = {0};
    int have_lat = 0;
    int have_lon = 0;
    if (!ifd_entry_range(base, len, ifd_off, &entries, &count)) return;
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *e = entries + (size_t)i * 12u;
        uint16_t tag = rd16(e, le);
        if (tag == 1) entry_ascii(base, len, e, le, lat_ref, sizeof(lat_ref));
        else if (tag == 2) have_lat = read_rational3(base, len, e, le, lat);
        else if (tag == 3) entry_ascii(base, len, e, le, lon_ref, sizeof(lon_ref));
        else if (tag == 4) have_lon = read_rational3(base, len, e, le, lon);
    }
    if (have_lat && have_lon && lat_ref[0] && lon_ref[0]) {
        double lat_dec = lat[0] + lat[1] / 60.0 + lat[2] / 3600.0;
        double lon_dec = lon[0] + lon[1] / 60.0 + lon[2] / 3600.0;
        if (lat_ref[0] == 'S' || lat_ref[0] == 's') lat_dec = -lat_dec;
        if (lon_ref[0] == 'W' || lon_ref[0] == 'w') lon_dec = -lon_dec;
        snprintf(c->photo_gps, sizeof(c->photo_gps), "%.6f, %.6f", lat_dec, lon_dec);
    }
}

static void parse_named_ifd(const uint8_t *base, size_t len, uint32_t ifd_off, int le, RecuCandidate *c,
                            uint32_t *exif_ifd, uint32_t *gps_ifd, int exif) {
    const uint8_t *entries = NULL;
    uint16_t count = 0;
    if (!ifd_entry_range(base, len, ifd_off, &entries, &count)) return;
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *e = entries + (size_t)i * 12u;
        uint16_t tag = rd16(e, le);
        uint32_t v = 0;
        if (!exif) {
            if (tag == 0x010F) entry_ascii(base, len, e, le, c->photo_make, sizeof(c->photo_make));
            else if (tag == 0x0110) entry_ascii(base, len, e, le, c->photo_model, sizeof(c->photo_model));
            else if (tag == 0x0131) entry_ascii(base, len, e, le, c->photo_software, sizeof(c->photo_software));
            else if (tag == 0x0132 && !c->photo_datetime[0]) entry_ascii(base, len, e, le, c->photo_datetime, sizeof(c->photo_datetime));
            else if (tag == 0x0112 && entry_u32(base, len, e, le, &v)) c->photo_orientation = (uint16_t)v;
            else if (tag == 0x0100 && entry_u32(base, len, e, le, &v)) c->photo_width = v;
            else if (tag == 0x0101 && entry_u32(base, len, e, le, &v)) c->photo_height = v;
            else if (tag == 0x8769 && entry_u32(base, len, e, le, &v)) *exif_ifd = v;
            else if (tag == 0x8825 && entry_u32(base, len, e, le, &v)) *gps_ifd = v;
        } else {
            if (tag == 0x9003) entry_ascii(base, len, e, le, c->photo_datetime, sizeof(c->photo_datetime));
            else if (tag == 0x9004 && !c->photo_datetime[0]) entry_ascii(base, len, e, le, c->photo_datetime, sizeof(c->photo_datetime));
            else if (tag == 0xA002 && entry_u32(base, len, e, le, &v)) c->photo_width = v;
            else if (tag == 0xA003 && entry_u32(base, len, e, le, &v)) c->photo_height = v;
        }
    }
}

static void parse_exif_tiff(const uint8_t *exif, size_t exif_len, RecuCandidate *c) {
    if (exif_len < 14 || memcmp(exif, "Exif\0\0", 6) != 0) return;
    const uint8_t *base = exif + 6;
    size_t len = exif_len - 6;
    int le = 0;
    if (len < 8) return;
    if (base[0] == 'I' && base[1] == 'I') le = 1;
    else if (base[0] == 'M' && base[1] == 'M') le = 0;
    else return;
    if (rd16(base + 2, le) != 42) return;
    uint32_t ifd0 = rd32(base + 4, le);
    if (ifd0 >= len) return;
    c->photo_exif_present = 1;
    c->photo_metadata_present = 1;

    uint32_t exif_ifd = 0;
    uint32_t gps_ifd = 0;
    parse_named_ifd(base, len, ifd0, le, c, &exif_ifd, &gps_ifd, 0);
    if (exif_ifd && exif_ifd < len) parse_named_ifd(base, len, exif_ifd, le, c, &exif_ifd, &gps_ifd, 1);
    if (gps_ifd && gps_ifd < len) parse_gps_ifd(base, len, gps_ifd, le, c);
}

static int is_sof_marker(uint8_t marker) {
    return (marker >= 0xC0 && marker <= 0xC3) ||
           (marker >= 0xC5 && marker <= 0xC7) ||
           (marker >= 0xC9 && marker <= 0xCB) ||
           (marker >= 0xCD && marker <= 0xCF);
}

static int parse_jpeg_metadata(RecuSource *src, RecuCandidate *c, RecuError *err) {
    if (!src || !c || c->size < 4 || c->offset >= src->size) return 0;
    uint64_t available = src->size - c->offset;
    uint64_t want64 = c->size < available ? c->size : available;
    if (want64 > JPEG_META_READ_LIMIT) want64 = JPEG_META_READ_LIMIT;
    if (want64 < 4) return 0;
    size_t want = (size_t)want64;
    uint8_t *buf = (uint8_t *)malloc(want);
    if (!buf) {
        recu_error_set(err, "out of memory while reading JPEG metadata");
        return 0;
    }
    if (!recu_source_read(src, c->offset, buf, want, err)) {
        free(buf);
        return 0;
    }
    if (buf[0] != 0xFF || buf[1] != 0xD8) {
        free(buf);
        return 0;
    }

    size_t p = 2;
    int segments = 0;
    while (p + 4 <= want && segments++ < 256) {
        while (p < want && buf[p] == 0xFF) p++;
        if (p >= want) break;
        uint8_t marker = buf[p++];
        if (marker == 0xDA || marker == 0xD9) break;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (p + 2 > want) break;
        uint16_t seg_len = be16m(buf + p);
        if (seg_len < 2 || p + seg_len > want) break;
        const uint8_t *data = buf + p + 2;
        size_t data_len = (size_t)seg_len - 2u;
        if (marker == 0xE1 && data_len >= 14 && memcmp(data, "Exif\0\0", 6) == 0) {
            parse_exif_tiff(data, data_len, c);
        } else if (is_sof_marker(marker) && data_len >= 5) {
            c->photo_height = be16m(data + 1);
            c->photo_width = be16m(data + 3);
            c->photo_metadata_present = 1;
        }
        p += seg_len;
    }
    free(buf);
    return c->photo_metadata_present;
}

static int datetime_to_filename(const char *dt, char *out, size_t out_size) {
    if (!dt || strlen(dt) < 19) return 0;
    if (!(isdigit((unsigned char)dt[0]) && isdigit((unsigned char)dt[1]) &&
          isdigit((unsigned char)dt[2]) && isdigit((unsigned char)dt[3]) &&
          dt[4] == ':' && dt[7] == ':' && dt[10] == ' ' &&
          dt[13] == ':' && dt[16] == ':')) {
        return 0;
    }
    snprintf(out, out_size, "%c%c%c%c%c%c%c%c_%c%c%c%c%c%c",
             dt[0], dt[1], dt[2], dt[3], dt[5], dt[6], dt[8], dt[9],
             dt[11], dt[12], dt[14], dt[15], dt[17], dt[18]);
    return 1;
}

int recu_enrich_candidate_metadata(RecuSource *src, const RecuVolumeInfo *vol, RecuCandidate *candidate, RecuError *err) {
    (void)vol;
    if (!candidate) return 0;
    if (candidate->metadata_checked) return candidate->photo_metadata_present;
    candidate->metadata_checked = 1;
    if (!src || !candidate->recoverable || candidate->format != RECU_FMT_JPG) return 0;
    int ok = parse_jpeg_metadata(src, candidate, err);
    if (ok && candidate->recoverable && !candidate->likely_overwritten && candidate->confidence > 0 && candidate->confidence < 95) {
        recu_candidate_confidence_add(candidate, 4, "metadata=jpeg-exif");
    }
    return ok;
}

int recu_enrich_scan_metadata(RecuSource *src, const RecuVolumeInfo *vol, RecuCandidateList *list) {
    if (!src || !list) return 0;
    int total = 0;
    for (size_t i = 0; i < list->count; i++) {
        RecuCandidate *c = &list->items[i];
        RecuError err;
        recu_error_clear(&err);
        if (recu_enrich_candidate_metadata(src, vol, c, &err)) {
            total++;
            if (c->kind == RECU_KIND_CARVED && c->photo_datetime[0]) {
                char stamp[32];
                if (datetime_to_filename(c->photo_datetime, stamp, sizeof(stamp))) {
                    snprintf(c->name, sizeof(c->name), "IMG_%s_%04u.jpg", stamp, c->id);
                    recu_sanitize_filename(c->name);
                    snprintf(c->path, sizeof(c->path), "carved/%s", c->name);
                    recu_safe_copy(c->extension, sizeof(c->extension), "jpg");
                }
            }
        }
    }
    return total;
}
