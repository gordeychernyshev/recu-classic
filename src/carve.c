#include "recu.h"

#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static int read_small(RecuSource *src, uint64_t offset, uint8_t *buf, size_t n) {
    RecuError err;
    recu_error_clear(&err);
    return recu_source_read(src, offset, buf, n, &err);
}

static int carve_progress_stage(const RecuScanOptions *opt, const char *stage, uint64_t done, uint64_t total, int *cancelled) {
    if (opt && opt->progress && !opt->progress(opt->progress_user, stage, done, total)) {
        if (cancelled) *cancelled = 1;
        return 0;
    }
    return 1;
}

static int carve_progress(const RecuScanOptions *opt, uint64_t done, uint64_t total, int *cancelled) {
    return carve_progress_stage(opt, "deep scan", done, total, cancelled);
}

static int search_needle(RecuSource *src, uint64_t start, uint64_t limit, const uint8_t *needle, size_t needle_len, uint64_t *found,
                         const RecuScanOptions *opt, uint64_t progress_done, uint64_t progress_total, int *cancelled) {
    const size_t chunk = 1024u * 1024u;
    size_t overlap = needle_len ? needle_len - 1u : 0;
    uint8_t *buf = (uint8_t *)malloc(chunk + overlap);
    if (!buf) return 0;
    uint64_t pos = start;
    size_t carry = 0;
    while (pos < limit) {
        if (!carve_progress(opt, progress_done, progress_total, cancelled)) {
            free(buf);
            return 0;
        }
        if (carry) memmove(buf, buf + chunk, carry);
        size_t want = (limit - pos) > chunk ? chunk : (size_t)(limit - pos);
        size_t got = 0;
        RecuError err;
        recu_error_clear(&err);
        if (!recu_source_read_partial(src, pos, buf + carry, want, &got, &err)) {
            free(buf);
            return 0;
        }
        size_t total = carry + got;
        if (total >= needle_len) {
            for (size_t i = 0; i + needle_len <= total; i++) {
                if (memcmp(buf + i, needle, needle_len) == 0) {
                    *found = pos - carry + i;
                    free(buf);
                    return 1;
                }
            }
        }
        if (got == 0) break;
        carry = total < overlap ? total : overlap;
        if (carry) memcpy(buf + chunk, buf + total - carry, carry);
        pos += got;
    }
    free(buf);
    return 0;
}

static uint64_t jpeg_length(RecuSource *src, uint64_t start, uint64_t max_len, const RecuScanOptions *opt, uint64_t progress_done, uint64_t progress_total, int *cancelled) {
    const uint8_t eoi[] = {0xFF, 0xD9};
    uint64_t p = start + 2;
    uint64_t limit = start + max_len;
    if (limit > src->size) limit = src->size;
    int segments = 0;
    while (p + 4 <= limit && segments++ < 256) {
        uint8_t h[4];
        if (!read_small(src, p, h, sizeof(h)) || h[0] != 0xFF) return 0;
        while (p + 2 <= limit) {
            if (!read_small(src, p, h, 2) || h[0] != 0xFF) return 0;
            if (h[1] != 0xFF) break;
            p++;
        }
        if (!read_small(src, p, h, sizeof(h))) return 0;
        uint8_t marker = h[1];
        if (marker == 0xD9) return 0;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) return 0;
        uint16_t seg_len = be16(h + 2);
        if (seg_len < 2 || p + 2ull + seg_len > limit) return 0;
        if (marker == 0xDA) {
            uint64_t found = 0;
            if (search_needle(src, p + 2ull + seg_len, limit, eoi, sizeof(eoi), &found, opt, progress_done, progress_total, cancelled)) {
                return found - start + 2;
            }
            return 0;
        }
        p += 2ull + seg_len;
    }
    return 0;
}

static uint64_t pdf_length(RecuSource *src, uint64_t start, uint64_t max_len, const RecuScanOptions *opt, uint64_t progress_done, uint64_t progress_total, int *cancelled) {
    const uint8_t eof[] = {'%', '%', 'E', 'O', 'F'};
    uint64_t found = 0;
    uint64_t limit = start + max_len;
    if (limit > src->size) limit = src->size;
    if (search_needle(src, start + 5, limit, eof, sizeof(eof), &found, opt, progress_done, progress_total, cancelled)) {
        uint64_t len = found - start + sizeof(eof);
        uint8_t b[2] = {0};
        size_t got = 0;
        RecuError err;
        recu_error_clear(&err);
        if (recu_source_read_partial(src, start + len, b, sizeof(b), &got, &err)) {
            while (got && (b[0] == '\r' || b[0] == '\n' || b[0] == ' ')) {
                len++;
                got = 0;
                recu_source_read_partial(src, start + len, b, 1, &got, &err);
            }
        }
        return len;
    }
    return 0;
}

static uint64_t png_length(RecuSource *src, uint64_t start, uint64_t max_len, const RecuScanOptions *opt, uint64_t progress_done, uint64_t progress_total, int *cancelled) {
    uint64_t p = start + 8;
    uint64_t limit = start + max_len;
    if (limit > src->size) limit = src->size;
    unsigned guard = 0;
    while (p + 12 <= limit) {
        if ((guard++ & 63u) == 0 && !carve_progress(opt, progress_done, progress_total, cancelled)) return 0;
        uint8_t h[8];
        if (!read_small(src, p, h, 8)) return 0;
        uint32_t len = be32(h);
        if (len > max_len || p + 12ull + len > limit) return 0;
        p += 8ull + len + 4ull;
        if (memcmp(h + 4, "IEND", 4) == 0) return p - start;
    }
    return 0;
}

static uint64_t zip_length(RecuSource *src, uint64_t start, uint64_t max_len, const RecuScanOptions *opt, uint64_t progress_done, uint64_t progress_total, int *cancelled) {
    const uint8_t eocd[] = {0x50, 0x4B, 0x05, 0x06};
    uint64_t limit = start + max_len;
    if (limit > src->size) limit = src->size;
    uint64_t search = start + 4;
    uint64_t last = 0;
    int have = 0;
    while (search < limit) {
        uint64_t found = 0;
        if (!search_needle(src, search, limit, eocd, sizeof(eocd), &found, opt, progress_done, progress_total, cancelled)) break;
        if (cancelled && *cancelled) break;
        if (found + 22 <= limit) {
            uint8_t h[22];
            if (read_small(src, found, h, sizeof(h))) {
                uint16_t comment = recu_le16(h + 20);
                uint32_t cd_size = recu_le32(h + 12);
                uint32_t cd_offset = recu_le32(h + 16);
                uint64_t eocd_rel = found - start;
                uint8_t cd_sig[4];
                int central_dir_ok = cd_offset < eocd_rel &&
                                     (uint64_t)cd_offset + cd_size <= eocd_rel &&
                                     read_small(src, start + cd_offset, cd_sig, sizeof(cd_sig)) &&
                                     memcmp(cd_sig, "PK\001\002", 4) == 0;
                if (central_dir_ok && found + 22ull + comment <= limit) {
                    last = found + 22ull + comment;
                    have = 1;
                }
            }
        }
        search = found + 4;
    }
    return have ? last - start : 0;
}

static uint64_t mp4_length(RecuSource *src, uint64_t start, uint64_t max_len, const RecuScanOptions *opt, uint64_t progress_done, uint64_t progress_total, int *cancelled) {
    uint64_t p = start;
    uint64_t limit = start + max_len;
    if (limit > src->size) limit = src->size;
    int boxes = 0;
    int saw_ftyp = 0;
    int saw_moov = 0;
    int saw_mdat = 0;
    while (p + 8 <= limit && boxes++ < 100000) {
        if ((boxes & 255) == 1 && !carve_progress(opt, progress_done, progress_total, cancelled)) return 0;
        uint8_t h[16];
        if (!read_small(src, p, h, 8)) break;
        uint64_t size = be32(h);
        char type[5] = { (char)h[4], (char)h[5], (char)h[6], (char)h[7], 0 };
        uint64_t header = 8;
        if (size == 1) {
            if (!read_small(src, p, h, 16)) break;
            size = be64(h + 8);
            header = 16;
        } else if (size == 0) {
            return 0;
        }
        if (size < header || p + size > limit) return 0;
        if (strcmp(type, "ftyp") == 0) saw_ftyp = 1;
        else if (strcmp(type, "moov") == 0) saw_moov = 1;
        else if (strcmp(type, "mdat") == 0) saw_mdat = 1;
        p += size;
        if (saw_ftyp && saw_moov && saw_mdat) {
            uint8_t next[8];
            if (p + 8 > limit || !read_small(src, p, next, 8)) return p - start;
            uint32_t ns = be32(next);
            if (ns < 8 || p + ns > limit) return p - start;
        }
    }
    return saw_ftyp && saw_moov && saw_mdat && p > start ? p - start : 0;
}

static uint64_t riff_length(RecuSource *src, uint64_t start, uint64_t max_len, const char type[4]) {
    if (max_len < 12 || start + 12 > src->size) return 0;
    uint8_t h[12];
    if (!read_small(src, start, h, sizeof(h))) return 0;
    if (memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, type, 4) != 0) return 0;
    uint64_t len = (uint64_t)recu_le32(h + 4) + 8ull;
    if (len < 12 || len > max_len || start + len > src->size) return 0;
    return len;
}

static uint64_t bmp_length(RecuSource *src, uint64_t start, uint64_t max_len) {
    if (max_len < 54 || start + 54 > src->size) return 0;
    uint8_t h[54];
    if (!read_small(src, start, h, sizeof(h))) return 0;
    if (h[0] != 'B' || h[1] != 'M') return 0;
    uint32_t file_size = recu_le32(h + 2);
    uint32_t pixel_off = recu_le32(h + 10);
    uint32_t dib = recu_le32(h + 14);
    if (file_size < 54 || file_size > max_len || start + file_size > src->size) return 0;
    if (pixel_off < 14 + dib || pixel_off >= file_size) return 0;
    if (!(dib == 12 || dib == 40 || dib == 52 || dib == 56 || dib == 108 || dib == 124)) return 0;
    return file_size;
}

static uint64_t sevenz_length(RecuSource *src, uint64_t start, uint64_t max_len) {
    static const uint8_t sig[6] = {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C};
    if (max_len < 32 || start + 32 > src->size) return 0;
    uint8_t h[32];
    if (!read_small(src, start, h, sizeof(h))) return 0;
    if (memcmp(h, sig, sizeof(sig)) != 0) return 0;
    uint64_t next_off = recu_le64(h + 12);
    uint64_t next_size = recu_le64(h + 20);
    if (next_off > max_len || next_size > max_len || 32ull + next_off + next_size > max_len) return 0;
    if (start + 32ull + next_off + next_size > src->size) return 0;
    return 32ull + next_off + next_size;
}

static uint64_t sqlite_length(RecuSource *src, uint64_t start, uint64_t max_len) {
    if (max_len < 100 || start + 100 > src->size) return 0;
    uint8_t h[100];
    if (!read_small(src, start, h, sizeof(h))) return 0;
    if (memcmp(h, "SQLite format 3\0", 16) != 0) return 0;
    uint32_t page_size = ((uint32_t)h[16] << 8) | h[17];
    if (page_size == 1) page_size = 65536;
    if (page_size < 512 || page_size > 65536 || (page_size & (page_size - 1u)) != 0) return 0;
    uint32_t pages = be32(h + 28);
    if (pages == 0) return 0;
    uint64_t len = (uint64_t)page_size * pages;
    if (len < 100 || len > max_len || start + len > src->size) return 0;
    return len;
}

static uint16_t tiff_u16(const uint8_t *p, int le) {
    return le ? recu_le16(p) : be16(p);
}

static uint32_t tiff_u32(const uint8_t *p, int le) {
    return le ? recu_le32(p) : be32(p);
}

static uint64_t tiff_type_size(uint16_t type) {
    switch (type) {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: case 13: return 4;
        case 5: case 10: case 12: return 8;
        default: return 0;
    }
}

static size_t tiff_read_long_values(RecuSource *src, uint64_t start, uint64_t limit, int le,
                                    uint16_t type, uint32_t count, const uint8_t value_field[4],
                                    uint64_t *out, size_t max_out) {
    if (!out || max_out == 0 || count == 0) return 0;
    uint64_t unit = tiff_type_size(type);
    if (!unit || !(type == 3 || type == 4 || type == 13)) return 0;
    size_t n = count < max_out ? (size_t)count : max_out;
    uint64_t bytes = unit * count;
    if (bytes <= 4) {
        for (size_t i = 0; i < n; i++) {
            if (type == 3) {
                if (le) out[i] = (i == 0) ? recu_le16(value_field) : recu_le16(value_field + 2);
                else out[i] = (i == 0) ? be16(value_field) : be16(value_field + 2);
            } else {
                out[i] = tiff_u32(value_field, le);
            }
        }
        return n;
    }
    uint32_t off = tiff_u32(value_field, le);
    if (off < 8 || start + off + bytes > limit) return 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t tmp[4];
        uint64_t pos = start + off + (uint64_t)i * unit;
        if (!read_small(src, pos, tmp, (size_t)unit)) return i;
        out[i] = type == 3 ? (le ? recu_le16(tmp) : be16(tmp)) : tiff_u32(tmp, le);
    }
    return n;
}

static void tiff_note_range(uint64_t off, uint64_t bytes, uint64_t max_len, uint64_t *max_end) {
    if (!off || !bytes) return;
    if (off + bytes < off || off + bytes > max_len) return;
    if (off + bytes > *max_end) *max_end = off + bytes;
}

static uint64_t tiff_length(RecuSource *src, uint64_t start, uint64_t max_len) {
    if (max_len < 8 || start + 8 > src->size) return 0;
    uint8_t h[8];
    if (!read_small(src, start, h, sizeof(h))) return 0;
    int le = 0;
    if (h[0] == 'I' && h[1] == 'I' && h[2] == 42 && h[3] == 0) le = 1;
    else if (h[0] == 'M' && h[1] == 'M' && h[2] == 0 && h[3] == 42) le = 0;
    else return 0;

    uint64_t limit = start + max_len;
    if (limit > src->size) limit = src->size;
    uint32_t queue[128];
    int qhead = 0, qtail = 0;
    uint32_t first_ifd = tiff_u32(h + 4, le);
    if (first_ifd >= 8) queue[qtail++] = first_ifd;
    uint64_t max_end = 8;
    int ifds = 0;
    while (qhead < qtail && ifds++ < 64) {
        uint32_t ifd = queue[qhead++];
        if (ifd < 8 || start + ifd + 2 > limit) continue;
        uint8_t nbuf[2];
        if (!read_small(src, start + ifd, nbuf, sizeof(nbuf))) break;
        uint16_t entries = tiff_u16(nbuf, le);
        if (entries > 4096) return 0;
        uint64_t table_end = (uint64_t)ifd + 2ull + (uint64_t)entries * 12ull + 4ull;
        if (start + table_end > limit) return 0;
        if (table_end > max_end) max_end = table_end;
        uint64_t strip_offsets[256], strip_counts[256], tile_offsets[256], tile_counts[256];
        uint64_t sub_ifds[64], jpeg_offs[8], jpeg_counts[8];
        size_t strip_off_n = 0, strip_count_n = 0, tile_off_n = 0, tile_count_n = 0;
        size_t sub_ifd_n = 0, jpeg_off_n = 0, jpeg_count_n = 0;
        for (uint16_t e = 0; e < entries; e++) {
            uint8_t ent[12];
            uint64_t ent_rel = (uint64_t)ifd + 2ull + (uint64_t)e * 12ull;
            if (!read_small(src, start + ent_rel, ent, sizeof(ent))) return 0;
            uint16_t tag = tiff_u16(ent, le);
            uint16_t type = tiff_u16(ent + 2, le);
            uint32_t count = tiff_u32(ent + 4, le);
            uint64_t unit = tiff_type_size(type);
            if (!unit || count > UINT32_MAX / unit) continue;
            uint64_t bytes = unit * count;
            if (bytes > 4) {
                uint32_t off = tiff_u32(ent + 8, le);
                if (off >= 8 && off + bytes > off && start + off + bytes <= limit) {
                    if ((uint64_t)off + bytes > max_end) max_end = (uint64_t)off + bytes;
                }
            }
            if (tag == 273) {
                strip_off_n = tiff_read_long_values(src, start, limit, le, type, count, ent + 8, strip_offsets, 256);
            } else if (tag == 279) {
                strip_count_n = tiff_read_long_values(src, start, limit, le, type, count, ent + 8, strip_counts, 256);
            } else if (tag == 324) {
                tile_off_n = tiff_read_long_values(src, start, limit, le, type, count, ent + 8, tile_offsets, 256);
            } else if (tag == 325) {
                tile_count_n = tiff_read_long_values(src, start, limit, le, type, count, ent + 8, tile_counts, 256);
            } else if (tag == 330 || tag == 34665 || tag == 34853 || tag == 40965) {
                sub_ifd_n = tiff_read_long_values(src, start, limit, le, type, count, ent + 8, sub_ifds, 64);
            } else if (tag == 513) {
                jpeg_off_n = tiff_read_long_values(src, start, limit, le, type, count, ent + 8, jpeg_offs, 8);
            } else if (tag == 514) {
                jpeg_count_n = tiff_read_long_values(src, start, limit, le, type, count, ent + 8, jpeg_counts, 8);
            }
        }
        size_t strip_n = strip_off_n < strip_count_n ? strip_off_n : strip_count_n;
        for (size_t s = 0; s < strip_n; s++) tiff_note_range(strip_offsets[s], strip_counts[s], max_len, &max_end);
        size_t tile_n = tile_off_n < tile_count_n ? tile_off_n : tile_count_n;
        for (size_t t = 0; t < tile_n; t++) tiff_note_range(tile_offsets[t], tile_counts[t], max_len, &max_end);
        size_t jpeg_n = jpeg_off_n < jpeg_count_n ? jpeg_off_n : jpeg_count_n;
        for (size_t j = 0; j < jpeg_n; j++) tiff_note_range(jpeg_offs[j], jpeg_counts[j], max_len, &max_end);
        for (size_t s = 0; s < sub_ifd_n && qtail < (int)(sizeof(queue) / sizeof(queue[0])); s++) {
            if (sub_ifds[s] >= 8 && sub_ifds[s] < max_len) queue[qtail++] = (uint32_t)sub_ifds[s];
        }
        uint8_t nextbuf[4];
        if (!read_small(src, start + table_end - 4, nextbuf, sizeof(nextbuf))) break;
        uint32_t next = tiff_u32(nextbuf, le);
        if (next >= 8 && next != ifd && qtail < (int)(sizeof(queue) / sizeof(queue[0]))) queue[qtail++] = next;
    }
    if (max_end < 16 || max_end > max_len || start + max_end > src->size) return 0;
    return max_end;
}

static int tiff_has_tag(RecuSource *src, uint64_t start, uint16_t wanted) {
    uint8_t h[8];
    if (!read_small(src, start, h, sizeof(h))) return 0;
    int le = 0;
    if (h[0] == 'I' && h[1] == 'I' && h[2] == 42 && h[3] == 0) le = 1;
    else if (h[0] == 'M' && h[1] == 'M' && h[2] == 0 && h[3] == 42) le = 0;
    else return 0;
    uint32_t ifd = tiff_u32(h + 4, le);
    for (int ifds = 0; ifd >= 8 && ifds < 8; ifds++) {
        uint8_t nbuf[2];
        if (!read_small(src, start + ifd, nbuf, sizeof(nbuf))) return 0;
        uint16_t entries = tiff_u16(nbuf, le);
        if (entries > 4096) return 0;
        for (uint16_t e = 0; e < entries; e++) {
            uint8_t ent[12];
            uint64_t ent_rel = (uint64_t)ifd + 2ull + (uint64_t)e * 12ull;
            if (!read_small(src, start + ent_rel, ent, sizeof(ent))) return 0;
            if (tiff_u16(ent, le) == wanted) return 1;
        }
        uint8_t nextbuf[4];
        uint64_t next_rel = (uint64_t)ifd + 2ull + (uint64_t)entries * 12ull;
        if (!read_small(src, start + next_rel, nextbuf, sizeof(nextbuf))) return 0;
        uint32_t next = tiff_u32(nextbuf, le);
        if (next == 0 || next == ifd) break;
        ifd = next;
    }
    return 0;
}

static int source_contains_ascii(RecuSource *src, uint64_t start, uint64_t size, const char *needle) {
    size_t needle_len = strlen(needle);
    size_t n = size > (1024u * 1024u) ? (1024u * 1024u) : (size_t)size;
    if (needle_len == 0 || n < needle_len) return 0;
    uint8_t *buf = (uint8_t *)malloc(n);
    if (!buf) return 0;
    size_t got = 0;
    RecuError err;
    recu_error_clear(&err);
    int ok = 0;
    if (recu_source_read_partial(src, start, buf, n, &got, &err)) {
        for (size_t i = 0; i + needle_len <= got; i++) {
            if (memcmp(buf + i, needle, needle_len) == 0) {
                ok = 1;
                break;
            }
        }
    }
    free(buf);
    return ok;
}

static RecuFileFormat identify_tiff_family(RecuSource *src, uint64_t start, uint64_t size) {
    uint8_t h[16];
    if (read_small(src, start, h, sizeof(h))) {
        if (h[0] == 'I' && h[1] == 'I' && h[2] == 42 && h[3] == 0 && h[8] == 'C' && h[9] == 'R') return RECU_FMT_CR2;
    }
    if (tiff_has_tag(src, start, 50706)) return RECU_FMT_DNG;
    if (source_contains_ascii(src, start, size, "NIKON")) return RECU_FMT_NEF;
    if (source_contains_ascii(src, start, size, "SONY")) return RECU_FMT_ARW;
    if (source_contains_ascii(src, start, size, "OLYMPUS")) return RECU_FMT_ORF;
    if (source_contains_ascii(src, start, size, "Panasonic")) return RECU_FMT_RW2;
    return RECU_FMT_TIFF;
}

static uint64_t raf_length(RecuSource *src, uint64_t start, uint64_t max_len) {
    if (max_len < 108 || start + 108 > src->size) return 0;
    uint8_t h[108];
    if (!read_small(src, start, h, sizeof(h))) return 0;
    if (memcmp(h, "FUJIFILMCCD-RAW ", 16) != 0) return 0;
    uint64_t max_end = 108;
    for (int i = 0; i < 3; i++) {
        uint32_t off = be32(h + 84 + i * 8);
        uint32_t len = be32(h + 88 + i * 8);
        if (off && len && (uint64_t)off + len > max_end) max_end = (uint64_t)off + len;
    }
    if (max_end < 108 || max_end > max_len || start + max_end > src->size) return 0;
    return max_end;
}

static uint64_t psd_length(RecuSource *src, uint64_t start, uint64_t max_len) {
    if (max_len < 30 || start + 30 > src->size) return 0;
    uint8_t h[30];
    if (!read_small(src, start, h, sizeof(h))) return 0;
    if (memcmp(h, "8BPS", 4) != 0 || be16(h + 4) != 1) return 0;
    uint16_t channels = be16(h + 12);
    uint32_t height = be32(h + 14);
    uint32_t width = be32(h + 18);
    uint16_t depth = be16(h + 22);
    if (channels == 0 || channels > 56 || !width || !height || width > 300000 || height > 300000) return 0;
    if (!(depth == 1 || depth == 8 || depth == 16 || depth == 32)) return 0;
    uint64_t p = 26;
    for (int section = 0; section < 3; section++) {
        uint8_t lenbuf[4];
        if (start + p + 4 > src->size || !read_small(src, start + p, lenbuf, sizeof(lenbuf))) return 0;
        uint32_t len = be32(lenbuf);
        p += 4ull + len;
        if (p > max_len || start + p > src->size) return 0;
    }
    uint8_t compbuf[2];
    if (!read_small(src, start + p, compbuf, sizeof(compbuf))) return 0;
    uint16_t compression = be16(compbuf);
    p += 2;
    uint64_t payload = 0;
    if (compression == 0) {
        uint64_t bits = (uint64_t)width * height * channels * depth;
        payload = (bits + 7ull) / 8ull;
    } else if (compression == 1) {
        uint64_t rows = (uint64_t)height * channels;
        if (rows > 10000000ull) return 0;
        uint64_t table_bytes = rows * (height < 65535 ? 2ull : 4ull);
        if (p + table_bytes > max_len || start + p + table_bytes > src->size) return 0;
        uint8_t *table = (uint8_t *)malloc((size_t)table_bytes);
        if (!table) return 0;
        int ok = read_small(src, start + p, table, (size_t)table_bytes);
        if (ok) {
            for (uint64_t r = 0; r < rows; r++) {
                payload += height < 65535 ? be16(table + r * 2) : be32(table + r * 4);
            }
        }
        free(table);
        if (!ok) return 0;
        p += table_bytes;
    } else {
        return 0;
    }
    if (p + payload < p || p + payload > max_len || start + p + payload > src->size) return 0;
    return p + payload;
}

static int rar_read_vint(RecuSource *src, uint64_t *p, uint64_t limit, uint64_t *value) {
    uint64_t v = 0;
    for (int i = 0; i < 10 && *p < limit; i++) {
        uint8_t b = 0;
        if (!read_small(src, *p, &b, 1)) return 0;
        (*p)++;
        v |= (uint64_t)(b & 0x7Fu) << (7 * i);
        if (!(b & 0x80)) {
            *value = v;
            return 1;
        }
    }
    return 0;
}

static uint64_t rar_length(RecuSource *src, uint64_t start, uint64_t max_len) {
    if (max_len < 7 || start + 7 > src->size) return 0;
    uint8_t sig[8] = {0};
    if (!read_small(src, start, sig, max_len >= 8 ? 8 : 7)) return 0;
    uint64_t limit = start + max_len;
    if (limit > src->size) limit = src->size;
    if (memcmp(sig, "Rar!\x1A\x07\x00", 7) == 0) {
        uint64_t p = start + 7;
        for (int blocks = 0; p + 7 <= limit && blocks < 100000; blocks++) {
            uint8_t h[11];
            if (!read_small(src, p, h, 7)) return 0;
            uint8_t type = h[2];
            uint16_t flags = recu_le16(h + 3);
            uint16_t head_size = recu_le16(h + 5);
            uint64_t block_size = head_size;
            if (head_size < 7) return 0;
            if (flags & 0x8000) {
                if (!read_small(src, p + 7, h + 7, 4)) return 0;
                block_size += recu_le32(h + 7);
            }
            if (block_size < head_size || p + block_size > limit) return 0;
            p += block_size;
            if (type == 0x7B) return p - start;
        }
        return 0;
    }
    if (max_len >= 8 && memcmp(sig, "Rar!\x1A\x07\x01\x00", 8) == 0) {
        uint64_t p = start + 8;
        for (int blocks = 0; p + 6 <= limit && blocks < 100000; blocks++) {
            uint64_t block_start = p;
            p += 4; /* header CRC */
            uint64_t header_size = 0, type = 0, flags = 0, extra = 0, data = 0;
            if (!rar_read_vint(src, &p, limit, &header_size)) return 0;
            uint64_t header_start = p;
            uint64_t header_end = header_start + header_size;
            if (header_end < header_start || header_end > limit) return 0;
            if (!rar_read_vint(src, &p, header_end, &type)) return 0;
            if (!rar_read_vint(src, &p, header_end, &flags)) return 0;
            if ((flags & 0x0001) && !rar_read_vint(src, &p, header_end, &extra)) return 0;
            if ((flags & 0x0002) && !rar_read_vint(src, &p, header_end, &data)) return 0;
            (void)extra;
            p = header_end + data;
            if (p <= block_start || p > limit) return 0;
            if (type == 5) return p - start;
        }
        return 0;
    }
    return 0;
}

static int ebml_read_vint(RecuSource *src, uint64_t *p, uint64_t limit, uint64_t *value, int keep_marker) {
    if (*p >= limit) return 0;
    uint8_t first = 0;
    if (!read_small(src, *p, &first, 1) || first == 0) return 0;
    int len = 1;
    uint8_t mask = 0x80;
    while (len <= 8 && !(first & mask)) {
        len++;
        mask >>= 1;
    }
    if (len > 8 || *p + (uint64_t)len > limit) return 0;
    uint64_t v = keep_marker ? first : (first & (uint8_t)(mask - 1));
    (*p)++;
    for (int i = 1; i < len; i++) {
        uint8_t b = 0;
        if (!read_small(src, *p, &b, 1)) return 0;
        (*p)++;
        v = (v << 8) | b;
    }
    *value = v;
    return 1;
}

static RecuFileFormat mkv_format_and_length(RecuSource *src, uint64_t start, uint64_t max_len, uint64_t *len_out) {
    *len_out = 0;
    if (max_len < 16 || start + 16 > src->size) return RECU_FMT_UNKNOWN;
    uint64_t limit = start + max_len;
    if (limit > src->size) limit = src->size;
    uint64_t p = start;
    uint64_t id = 0, size = 0;
    if (!ebml_read_vint(src, &p, limit, &id, 1) || id != 0x1A45DFA3ull) return RECU_FMT_UNKNOWN;
    if (!ebml_read_vint(src, &p, limit, &size, 0)) return RECU_FMT_UNKNOWN;
    uint64_t ebml_end = p + size;
    if (ebml_end <= p || ebml_end > limit) return RECU_FMT_UNKNOWN;
    RecuFileFormat fmt = RECU_FMT_MKV;
    while (p + 2 < ebml_end) {
        uint64_t cid = 0, csize = 0;
        if (!ebml_read_vint(src, &p, ebml_end, &cid, 1) || !ebml_read_vint(src, &p, ebml_end, &csize, 0)) break;
        if (p + csize > ebml_end) break;
        if (cid == 0x4282 && csize > 0 && csize < 32) {
            char doc[32];
            memset(doc, 0, sizeof(doc));
            if (read_small(src, p, (uint8_t *)doc, (size_t)csize)) {
                if (strcmp(doc, "webm") == 0) fmt = RECU_FMT_WEBM;
                else if (strcmp(doc, "matroska") != 0) return RECU_FMT_UNKNOWN;
            }
        }
        p += csize;
    }
    p = ebml_end;
    if (!ebml_read_vint(src, &p, limit, &id, 1) || id != 0x18538067ull) return RECU_FMT_UNKNOWN;
    if (!ebml_read_vint(src, &p, limit, &size, 0)) return RECU_FMT_UNKNOWN;
    uint64_t end = p + size;
    if (end <= p || end > limit) return RECU_FMT_UNKNOWN;
    *len_out = end - start;
    return fmt;
}

static uint64_t ts_length(RecuSource *src, uint64_t start, uint64_t max_len, size_t packet, size_t sync_off) {
    if (packet < 188 || max_len < packet * 5 || start + sync_off >= src->size) return 0;
    uint64_t limit = start + max_len;
    if (limit > src->size) limit = src->size;
    uint64_t p = start;
    int packets = 0;
    while (p + packet <= limit && packets < 10000000) {
        uint8_t b = 0;
        if (!read_small(src, p + sync_off, &b, 1) || b != 0x47) break;
        p += packet;
        packets++;
    }
    return packets >= 5 ? p - start : 0;
}

static uint64_t gif_length(RecuSource *src, uint64_t start, uint64_t max_len, const RecuScanOptions *opt, uint64_t progress_done, uint64_t progress_total, int *cancelled) {
    if (max_len < 14 || start + 14 > src->size) return 0;
    uint8_t h[16];
    if (!read_small(src, start, h, 13)) return 0;
    if (memcmp(h, "GIF87a", 6) != 0 && memcmp(h, "GIF89a", 6) != 0) return 0;
    uint64_t p = start + 13;
    uint8_t packed = h[10];
    if (packed & 0x80) p += 3ull * (1ull << ((packed & 0x07) + 1));
    uint64_t limit = start + max_len;
    if (limit > src->size) limit = src->size;
    while (p < limit) {
        if (!carve_progress(opt, progress_done, progress_total, cancelled)) return 0;
        uint8_t b = 0;
        if (!read_small(src, p++, &b, 1)) return 0;
        if (b == 0x3B) return p - start;
        if (b == 0x21) {
            p++;
            for (;;) {
                uint8_t n = 0;
                if (p >= limit || !read_small(src, p++, &n, 1)) return 0;
                if (n == 0) break;
                p += n;
                if (p > limit) return 0;
            }
        } else if (b == 0x2C) {
            if (p + 9 > limit || !read_small(src, p, h, 9)) return 0;
            uint8_t ipacked = h[8];
            p += 9;
            if (ipacked & 0x80) p += 3ull * (1ull << ((ipacked & 0x07) + 1));
            p++;
            for (;;) {
                uint8_t n = 0;
                if (p >= limit || !read_small(src, p++, &n, 1)) return 0;
                if (n == 0) break;
                p += n;
                if (p > limit) return 0;
            }
        } else {
            return 0;
        }
    }
    return 0;
}

static uint64_t ogg_length(RecuSource *src, uint64_t start, uint64_t max_len, const RecuScanOptions *opt, uint64_t progress_done, uint64_t progress_total, int *cancelled) {
    uint64_t p = start;
    uint64_t limit = start + max_len;
    if (limit > src->size) limit = src->size;
    int pages = 0;
    while (p + 27 <= limit && pages++ < 100000) {
        if (!carve_progress(opt, progress_done, progress_total, cancelled)) return 0;
        uint8_t h[27 + 255];
        if (!read_small(src, p, h, 27)) return 0;
        if (memcmp(h, "OggS", 4) != 0 || h[4] != 0) return 0;
        uint8_t segments = h[26];
        if (p + 27ull + segments > limit || !read_small(src, p + 27, h + 27, segments)) return 0;
        uint64_t data = 0;
        for (uint8_t i = 0; i < segments; i++) data += h[27 + i];
        uint64_t page_len = 27ull + segments + data;
        if (page_len < 27 || p + page_len > limit) return 0;
        p += page_len;
        if (h[5] & 0x04) return p - start;
    }
    return 0;
}

static int parse_mpeg_audio_header(uint32_t h, uint32_t *frame_len) {
    if ((h & 0xFFE00000u) != 0xFFE00000u) return 0;
    unsigned version = (h >> 19) & 3u;
    unsigned layer = (h >> 17) & 3u;
    unsigned bitrate_idx = (h >> 12) & 0xFu;
    unsigned sample_idx = (h >> 10) & 3u;
    unsigned padding = (h >> 9) & 1u;
    if (version == 1 || layer == 0 || bitrate_idx == 0 || bitrate_idx == 15 || sample_idx == 3) return 0;
    static const int br_v1_l1[16]  = {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0};
    static const int br_v1_l2[16]  = {0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0};
    static const int br_v1_l3[16]  = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    static const int br_v2_l1[16]  = {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0};
    static const int br_v2_l23[16] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
    static const int sr_v1[4] = {44100,48000,32000,0};
    static const int sr_v2[4] = {22050,24000,16000,0};
    static const int sr_v25[4] = {11025,12000,8000,0};
    const int *br_table = NULL;
    if (version == 3 && layer == 3) br_table = br_v1_l1;
    else if (version == 3 && layer == 2) br_table = br_v1_l2;
    else if (version == 3 && layer == 1) br_table = br_v1_l3;
    else if (layer == 3) br_table = br_v2_l1;
    else br_table = br_v2_l23;
    int bitrate = br_table[bitrate_idx] * 1000;
    int sample_rate = version == 3 ? sr_v1[sample_idx] : (version == 2 ? sr_v2[sample_idx] : sr_v25[sample_idx]);
    if (!bitrate || !sample_rate) return 0;
    if (layer == 3) *frame_len = (uint32_t)(((12 * bitrate) / sample_rate + padding) * 4);
    else *frame_len = (uint32_t)((((version == 3 || layer == 2) ? 144 : 72) * bitrate) / sample_rate + padding);
    return *frame_len >= 4 && *frame_len <= 4096;
}

static uint64_t skip_id3v2(RecuSource *src, uint64_t start, uint64_t max_len) {
    if (max_len < 10 || start + 10 > src->size) return 0;
    uint8_t h[10];
    if (!read_small(src, start, h, sizeof(h))) return 0;
    if (memcmp(h, "ID3", 3) != 0 || h[3] == 0xFF || h[4] == 0xFF ||
        (h[6] & 0x80) || (h[7] & 0x80) || (h[8] & 0x80) || (h[9] & 0x80)) {
        return 0;
    }
    return 10ull + (((uint64_t)h[6] << 21) | ((uint64_t)h[7] << 14) | ((uint64_t)h[8] << 7) | h[9]);
}

static uint64_t mp3_length(RecuSource *src, uint64_t start, uint64_t max_len) {
    uint64_t p = start + skip_id3v2(src, start, max_len);
    uint64_t limit = start + max_len;
    if (limit > src->size) limit = src->size;
    int frames = 0;
    while (p + 4 <= limit && frames < 100000) {
        uint8_t b[4];
        if (!read_small(src, p, b, sizeof(b))) break;
        uint32_t header = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
        uint32_t frame_len = 0;
        if (!parse_mpeg_audio_header(header, &frame_len)) break;
        if (p + frame_len > limit) break;
        p += frame_len;
        frames++;
    }
    return frames ? p - start : 0;
}

static int parse_adts_header(const uint8_t h[7], uint32_t *frame_len) {
    if (h[0] != 0xFF || (h[1] & 0xF6) != 0xF0) return 0;
    unsigned profile = (h[2] >> 6) & 3u;
    unsigned sf_index = (h[2] >> 2) & 0xFu;
    unsigned channel_cfg = ((h[2] & 1u) << 2) | ((h[3] >> 6) & 3u);
    uint32_t len = ((uint32_t)(h[3] & 3u) << 11) | ((uint32_t)h[4] << 3) | ((uint32_t)h[5] >> 5);
    if (profile == 3 || sf_index == 15 || channel_cfg == 0 || len < 7 || len > 8192) return 0;
    *frame_len = len;
    return 1;
}

static uint64_t aac_length(RecuSource *src, uint64_t start, uint64_t max_len) {
    uint64_t p = start;
    uint64_t limit = start + max_len;
    if (limit > src->size) limit = src->size;
    int frames = 0;
    while (p + 7 <= limit && frames < 100000) {
        uint8_t h[7];
        if (!read_small(src, p, h, sizeof(h))) break;
        uint32_t frame_len = 0;
        if (!parse_adts_header(h, &frame_len)) break;
        if (p + frame_len > limit) break;
        p += frame_len;
        frames++;
    }
    return frames ? p - start : 0;
}

static uint64_t flac_length(RecuSource *src, uint64_t start, uint64_t max_len) {
    if (max_len < 8 || start + 8 > src->size) return 0;
    uint8_t h[4];
    if (!read_small(src, start, h, 4) || memcmp(h, "fLaC", 4) != 0) return 0;
    uint64_t p = start + 4;
    uint64_t limit = start + max_len;
    if (limit > src->size) limit = src->size;
    int blocks = 0;
    while (p + 4 <= limit && blocks++ < 128) {
        if (!read_small(src, p, h, 4)) return 0;
        int last = (h[0] & 0x80) != 0;
        uint32_t len = ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
        p += 4ull + len;
        if (p > limit) return 0;
        if (last) {
            /* FLAC has no reliable container-level EOF marker, so deep scan only carves the header/metadata prefix. */
            return p - start;
        }
    }
    return 0;
}

static RecuFileFormat iso_bmff_format_at(const uint8_t *buf, size_t total, size_t i, uint64_t max_len) {
    if (i + 16 > total || memcmp(buf + i + 4, "ftyp", 4) != 0) return RECU_FMT_UNKNOWN;
    uint32_t box_size = be32(buf + i);
    if (box_size < 16 || box_size > max_len || i + box_size > total) return RECU_FMT_UNKNOWN;
    const char *heic_brands[] = {"heic", "heix", "hevc", "hevx", "heim", "heis", "mif1", "msf1", "avif"};
    int mov = 0;
    int alac = 0;
    int gp3 = 0;
    int g2 = 0;
    int cr3 = 0;
    for (size_t p = i + 8; p + 4 <= i + box_size; p += 4) {
        if (memcmp(buf + p, "qt  ", 4) == 0) mov = 1;
        if (memcmp(buf + p, "alac", 4) == 0 || memcmp(buf + p, "M4A ", 4) == 0) alac = 1;
        if (memcmp(buf + p, "crx ", 4) == 0) cr3 = 1;
        if (memcmp(buf + p, "3gp", 3) == 0 || memcmp(buf + p, "3gs", 3) == 0 || memcmp(buf + p, "3ge", 3) == 0) gp3 = 1;
        if (memcmp(buf + p, "3g2", 3) == 0) g2 = 1;
        for (size_t b = 0; b < sizeof(heic_brands) / sizeof(heic_brands[0]); b++) {
            if (memcmp(buf + p, heic_brands[b], 4) == 0) return RECU_FMT_HEIC;
        }
    }
    if (cr3) return RECU_FMT_CR3;
    if (g2) return RECU_FMT_3G2;
    if (gp3) return RECU_FMT_3GP;
    if (alac) return RECU_FMT_ALAC;
    if (mov) return RECU_FMT_MOV;
    return RECU_FMT_MP4;
}

static int plausible_pdf_at(const uint8_t *buf, size_t total, size_t i) {
    return i + 8 <= total &&
           memcmp(buf + i, "%PDF-", 5) == 0 &&
           buf[i + 5] >= '0' && buf[i + 5] <= '9' &&
           buf[i + 6] == '.' &&
           buf[i + 7] >= '0' && buf[i + 7] <= '9';
}

static int plausible_zip_at(const uint8_t *buf, size_t total, size_t i) {
    if (i + 30 > total || memcmp(buf + i, "PK\003\004", 4) != 0) return 0;
    uint16_t version = recu_le16(buf + i + 4);
    uint16_t method = recu_le16(buf + i + 8);
    uint16_t name_len = recu_le16(buf + i + 26);
    uint16_t extra_len = recu_le16(buf + i + 28);
    if (version < 10 || version > 63) return 0;
    if (!(method == 0 || method == 8 || method == 9 || method == 12 || method == 14 || method == 98)) return 0;
    if (name_len == 0 || name_len > 255 || extra_len > 4096) return 0;
    if (i + 30u + name_len > total) return 0;
    for (uint16_t j = 0; j < name_len; j++) {
        unsigned char c = buf[i + 30u + j];
        if (c < 32 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            return 0;
        }
    }
    return 1;
}

static int plausible_mp4_at(const uint8_t *buf, size_t total, size_t i, uint64_t max_len) {
    return iso_bmff_format_at(buf, total, i, max_len) != RECU_FMT_UNKNOWN;
}

static int plausible_mp3_at(const uint8_t *buf, size_t total, size_t i) {
    if (i + 10 <= total && memcmp(buf + i, "ID3", 3) == 0 &&
        buf[i + 3] != 0xFF && buf[i + 4] != 0xFF &&
        !(buf[i + 6] & 0x80) && !(buf[i + 7] & 0x80) && !(buf[i + 8] & 0x80) && !(buf[i + 9] & 0x80)) {
        return 1;
    }
    if (i + 4 > total) return 0;
    uint32_t header = ((uint32_t)buf[i] << 24) | ((uint32_t)buf[i + 1] << 16) |
                      ((uint32_t)buf[i + 2] << 8) | buf[i + 3];
    uint32_t frame_len = 0;
    return parse_mpeg_audio_header(header, &frame_len);
}

static int plausible_aac_at(const uint8_t *buf, size_t total, size_t i) {
    if (i + 7 > total) return 0;
    uint32_t frame_len = 0;
    return parse_adts_header(buf + i, &frame_len);
}

static int plausible_jpeg_at(const uint8_t *buf, size_t total, size_t i) {
    if (i + 4 > total) return 0;
    if (!(buf[i] == 0xFF && buf[i + 1] == 0xD8 && buf[i + 2] == 0xFF)) return 0;
    unsigned char marker = buf[i + 3];
    if (marker >= 0xE0 && marker <= 0xEF) return 1;
    return marker == 0xDB || marker == 0xC0 || marker == 0xC2;
}

static RecuFileFormat identify_zip_family(RecuSource *src, uint64_t start, uint64_t size) {
    size_t n = size > (2u * 1024u * 1024u) ? (2u * 1024u * 1024u) : (size_t)size;
    uint8_t *buf = (uint8_t *)malloc(n ? n : 1);
    if (!buf) return RECU_FMT_ZIP;
    RecuError err;
    recu_error_clear(&err);
    size_t got = 0;
    recu_source_read_partial(src, start, buf, n, &got, &err);
    RecuFileFormat fmt = RECU_FMT_ZIP;
    static const char odt[] = "application/vnd.oasis.opendocument.text";
    static const char ods[] = "application/vnd.oasis.opendocument.spreadsheet";
    static const char odp[] = "application/vnd.oasis.opendocument.presentation";
    static const char epub_mime[] = "application/epub+zip";
    static const char epub_container[] = "META-INF/container.xml";
    static const char apk_manifest[] = "AndroidManifest.xml";
    static const char jar_manifest[] = "META-INF/MANIFEST.MF";
    for (size_t i = 0; i + 6 < got; i++) {
        if (memcmp(buf + i, "word/", 5) == 0) { fmt = RECU_FMT_DOCX; break; }
        if (memcmp(buf + i, "xl/", 3) == 0) { fmt = RECU_FMT_XLSX; break; }
        if (memcmp(buf + i, "ppt/", 4) == 0) { fmt = RECU_FMT_PPTX; break; }
        if (i + sizeof(odt) - 1 <= got && memcmp(buf + i, odt, sizeof(odt) - 1) == 0) { fmt = RECU_FMT_ODT; break; }
        if (i + sizeof(ods) - 1 <= got && memcmp(buf + i, ods, sizeof(ods) - 1) == 0) { fmt = RECU_FMT_ODS; break; }
        if (i + sizeof(odp) - 1 <= got && memcmp(buf + i, odp, sizeof(odp) - 1) == 0) { fmt = RECU_FMT_ODP; break; }
        if (i + sizeof(epub_mime) - 1 <= got && memcmp(buf + i, epub_mime, sizeof(epub_mime) - 1) == 0) { fmt = RECU_FMT_EPUB; break; }
        if (i + sizeof(epub_container) - 1 <= got && memcmp(buf + i, epub_container, sizeof(epub_container) - 1) == 0) { fmt = RECU_FMT_EPUB; break; }
        if (i + sizeof(apk_manifest) - 1 <= got && memcmp(buf + i, apk_manifest, sizeof(apk_manifest) - 1) == 0) { fmt = RECU_FMT_APK; break; }
        if (i + sizeof(jar_manifest) - 1 <= got && memcmp(buf + i, jar_manifest, sizeof(jar_manifest) - 1) == 0) { fmt = RECU_FMT_JAR; break; }
    }
    free(buf);
    return fmt;
}

static int already_have_carve(const RecuCandidateList *out, uint64_t offset, uint64_t size) {
    for (size_t i = 0; i < out->count; i++) {
        const RecuCandidate *c = &out->items[i];
        if (c->kind != RECU_KIND_CARVED) continue;
        if (c->offset == offset && c->size == size) return 1;
    }
    return 0;
}

typedef struct CarveFatMap {
    uint8_t *data;
    uint64_t size;
    int valid;
} CarveFatMap;

static int load_fat_map(RecuSource *src, const RecuVolumeInfo *vol, CarveFatMap *map) {
    memset(map, 0, sizeof(*map));
    if (!src || !vol) return 0;
    if (!(vol->fs_type == RECU_FS_FAT16 || vol->fs_type == RECU_FS_FAT32)) return 0;
    if (!vol->fat_offset || !vol->fat_size || vol->fat_size > 128ull * 1024ull * 1024ull) return 0;
    map->data = (uint8_t *)malloc((size_t)vol->fat_size);
    if (!map->data) return 0;
    size_t got = 0;
    RecuError local;
    recu_error_clear(&local);
    if (!recu_source_read_partial(src, vol->fat_offset, map->data, (size_t)vol->fat_size, &got, &local) || got < vol->fat_size) {
        free(map->data);
        memset(map, 0, sizeof(*map));
        return 0;
    }
    map->size = vol->fat_size;
    map->valid = 1;
    return 1;
}

static void free_fat_map(CarveFatMap *map) {
    if (!map) return;
    free(map->data);
    memset(map, 0, sizeof(*map));
}

static uint32_t fat_map_entry(const CarveFatMap *map, const RecuVolumeInfo *vol, uint32_t cluster) {
    if (!map || !map->valid || !vol) return 1;
    uint64_t off = (vol->fs_type == RECU_FS_FAT16) ? (uint64_t)cluster * 2u : (uint64_t)cluster * 4u;
    if (vol->fs_type == RECU_FS_FAT16) {
        if (off + 2 > map->size) return 1;
        return recu_le16(map->data + off);
    }
    if (off + 4 > map->size) return 1;
    return recu_le32(map->data + off) & 0x0FFFFFFFu;
}

static int fat_map_cluster_free(const CarveFatMap *map, const RecuVolumeInfo *vol, uint32_t cluster) {
    if (!map || !map->valid) return 1;
    if (cluster < 2 || cluster >= vol->cluster_count + 2) return 0;
    return fat_map_entry(map, vol, cluster) == 0;
}

static void add_carved(RecuSource *src, RecuCandidateList *out, RecuFileFormat fmt, uint64_t offset, uint64_t size, RecuError *err) {
    if (size == 0 || already_have_carve(out, offset, size)) return;
    if (fmt == RECU_FMT_ZIP) fmt = identify_zip_family(src, offset, size);
    RecuCandidate c;
    memset(&c, 0, sizeof(c));
    c.kind = RECU_KIND_CARVED;
    c.fs_type = RECU_FS_UNKNOWN;
    c.format = fmt;
    c.size = size;
    c.offset = offset;
    c.recoverable = 1;
    int structured = (fmt == RECU_FMT_ZIP || fmt == RECU_FMT_DOCX || fmt == RECU_FMT_XLSX || fmt == RECU_FMT_PPTX ||
                      fmt == RECU_FMT_ODT || fmt == RECU_FMT_ODS || fmt == RECU_FMT_ODP || fmt == RECU_FMT_EPUB ||
                      fmt == RECU_FMT_APK || fmt == RECU_FMT_JAR || fmt == RECU_FMT_MP4 || fmt == RECU_FMT_MOV ||
                      fmt == RECU_FMT_3GP || fmt == RECU_FMT_3G2 || fmt == RECU_FMT_MKV || fmt == RECU_FMT_WEBM ||
                      fmt == RECU_FMT_TS || fmt == RECU_FMT_M2TS || fmt == RECU_FMT_ALAC || fmt == RECU_FMT_AAC ||
                      fmt == RECU_FMT_MP3 || fmt == RECU_FMT_HEIC || fmt == RECU_FMT_CR3 || fmt == RECU_FMT_FLAC ||
                      fmt == RECU_FMT_RAR || fmt == RECU_FMT_7Z || fmt == RECU_FMT_SQLITE);
    recu_candidate_confidence_reset(&c, structured ? 64 : 68, "source=signature-scan");
    recu_candidate_confidence_add(&c, 0, "name=generated");
    recu_candidate_confidence_add(&c, 0, "bounds=inside-volume");
    recu_candidate_confidence_add(&c, 0, "method=carving");
    snprintf(c.name, sizeof(c.name), "carved_%06u%s", (unsigned)(out->count + 1), recu_format_extension(fmt));
    snprintf(c.path, sizeof(c.path), "carved/%s", c.name);
    recu_safe_copy(c.extension, sizeof(c.extension), recu_format_extension(fmt) + 1);
    snprintf(c.note, sizeof(c.note), "deep scan: structure not validated yet; original name/path are unknown");
    RecuError local;
    recu_error_clear(&local);
    c.validation = recu_validate_candidate(src, NULL, &c, &local);
    c.validation_checked = 1;
    recu_candidate_apply_validation_confidence(&c);
    if (c.validation == RECU_VALIDATION_VALID) {
        snprintf(c.note, sizeof(c.note), "deep scan: valid structure; original name/path are unknown");
    } else if (c.validation == RECU_VALIDATION_PARTIAL) {
        snprintf(c.note, sizeof(c.note), "deep scan: partial/truncated structure; recover only if you want to inspect it");
    } else if (c.validation == RECU_VALIDATION_DAMAGED) {
        snprintf(c.note, sizeof(c.note), "deep scan: suspicious signature; validator says damaged");
    } else {
        c.confidence = 30;
        snprintf(c.note, sizeof(c.note), "deep scan: validator could not confirm this file");
    }
    recu_list_push(out, &c, err);
}

int recu_carve_scan(RecuSource *src, const RecuVolumeInfo *vol, RecuCandidateList *out, const RecuScanOptions *opt, RecuError *err) {
    if (!src || !out) return 0;
    const uint64_t default_max = 512ull * 1024ull * 1024ull;
    uint64_t max_len = opt && opt->max_carve_bytes ? opt->max_carve_bytes : default_max;
    int max_files = opt && opt->max_carved_files ? opt->max_carved_files : 2048;
    uint64_t start = 0;
    uint64_t end = src->size;
    if (vol && vol->data_offset && vol->data_offset < src->size) {
        start = vol->data_offset;
        end = vol->data_offset + vol->data_size;
        if (end > src->size || end <= start) end = src->size;
    }

    CarveFatMap fat_map;
    load_fat_map(src, vol, &fat_map);

    size_t chunk = 1024u * 1024u;
    if (src->is_raw_device) {
        if (fat_map.valid) {
            chunk = 64u * 1024u;
        } else {
            chunk = (vol && vol->cluster_size >= 512u && vol->cluster_size <= 64u * 1024u) ? vol->cluster_size : 4096u;
        }
    }
    const size_t overlap = 4096;
    uint8_t *buf = (uint8_t *)malloc(chunk + overlap);
    if (!buf) {
        free_fat_map(&fat_map);
        recu_error_set(err, "out of memory during deep scan");
        return 0;
    }
    if (opt && opt->progress && !opt->progress(opt->progress_user, "deep scan", 0, end - start)) {
        free_fat_map(&fat_map);
        free(buf);
        recu_error_set(err, "operation cancelled");
        return 0;
    }

    size_t carry = 0;
    int carved = 0;
    uint64_t pos = start;
    const uint64_t mb = 1024ull * 1024ull;
    uint64_t jpg_cap = max_len > 32ull * mb ? 32ull * mb : max_len;
    uint64_t pdf_cap = max_len > 32ull * mb ? 32ull * mb : max_len;
    uint64_t zip_cap = max_len > 64ull * mb ? 64ull * mb : max_len;
    uint64_t mp4_cap = max_len;
    uint64_t media_cap = max_len > 256ull * mb ? 256ull * mb : max_len;
    while (pos < end && carved < max_files) {
        if (fat_map.valid && vol && vol->cluster_size) {
            uint64_t rel = pos > vol->data_offset ? pos - vol->data_offset : 0;
            uint32_t cluster = 2u + (uint32_t)(rel / vol->cluster_size);
            while (cluster < vol->cluster_count + 2 && !fat_map_cluster_free(&fat_map, vol, cluster)) {
                cluster++;
            }
            if (cluster >= vol->cluster_count + 2) break;
            uint64_t new_pos = recu_cluster_offset(vol, cluster);
            if (new_pos >= end) break;
            if (new_pos != pos) {
                carry = 0;
                pos = new_pos;
            }
        }
        if (carry) memmove(buf, buf + chunk, carry);
        size_t want = (end - pos) > chunk ? chunk : (size_t)(end - pos);
        if (fat_map.valid && vol && vol->cluster_size) {
            uint64_t rel = pos > vol->data_offset ? pos - vol->data_offset : 0;
            uint32_t cluster = 2u + (uint32_t)(rel / vol->cluster_size);
            uint32_t max_run = (uint32_t)(chunk / vol->cluster_size);
            if (max_run == 0) max_run = 1;
            uint32_t run = 0;
            while (run < max_run &&
                   cluster + run < vol->cluster_count + 2 &&
                   fat_map_cluster_free(&fat_map, vol, cluster + run)) {
                run++;
            }
            if (run == 0) {
                pos += vol->cluster_size;
                carry = 0;
                continue;
            }
            uint64_t run_bytes = (uint64_t)run * vol->cluster_size;
            if (run_bytes < want) want = (size_t)run_bytes;
        }
        size_t got = 0;
        int read_cancelled = 0;
        if (!carve_progress_stage(opt, "deep scan read", pos - start, end - start, &read_cancelled)) {
            free_fat_map(&fat_map);
            free(buf);
            recu_error_set(err, "operation cancelled");
            return 0;
        }
        if (!recu_source_read_partial(src, pos, buf + carry, want, &got, err)) {
            if (src->is_raw_device) {
                recu_error_clear(err);
                carve_progress_stage(opt, "deep scan skip", pos - start, end - start, NULL);
                carry = 0;
                pos += want ? want : chunk;
                continue;
            }
            free_fat_map(&fat_map);
            free(buf);
            return 0;
        }
        size_t total = carry + got;
        uint64_t base = pos - carry;
        size_t before = out->count;
        int cancelled = 0;
        size_t next_progress = 0;
        for (size_t i = 0; i + 12 <= total && carved < max_files; i++) {
            uint64_t abs = base + i;
            uint64_t here = abs > start ? abs - start : 0;
            if (i >= next_progress) {
                if (!carve_progress(opt, here, end - start, &cancelled)) {
                    free_fat_map(&fat_map);
                    free(buf);
                    recu_error_set(err, "operation cancelled");
                    return 0;
                }
                next_progress = i + 256u * 1024u;
            }
            uint64_t len = 0;
            if (plausible_jpeg_at(buf, total, i)) {
                if (!carve_progress(opt, here, end - start, &cancelled)) {
                    free_fat_map(&fat_map);
                    free(buf);
                    recu_error_set(err, "operation cancelled");
                    return 0;
                }
                len = jpeg_length(src, abs, jpg_cap, opt, here, end - start, &cancelled);
                if (cancelled) {
                    free_fat_map(&fat_map);
                    free(buf);
                    recu_error_set(err, "operation cancelled");
                    return 0;
                }
                add_carved(src, out, RECU_FMT_JPG, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 8 <= total && memcmp(buf + i, "\x89PNG\r\n\x1A\n", 8) == 0) {
                if (!carve_progress(opt, here, end - start, &cancelled)) {
                    free_fat_map(&fat_map);
                    free(buf);
                    recu_error_set(err, "operation cancelled");
                    return 0;
                }
                len = png_length(src, abs, max_len, opt, here, end - start, &cancelled);
                if (cancelled) {
                    free_fat_map(&fat_map);
                    free(buf);
                    recu_error_set(err, "operation cancelled");
                    return 0;
                }
                add_carved(src, out, RECU_FMT_PNG, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 16 <= total &&
                       ((buf[i] == 'I' && buf[i + 1] == 'I' && buf[i + 2] == 42 && buf[i + 3] == 0) ||
                        (buf[i] == 'M' && buf[i + 1] == 'M' && buf[i + 2] == 0 && buf[i + 3] == 42))) {
                len = tiff_length(src, abs, max_len);
                if (len) {
                    add_carved(src, out, identify_tiff_family(src, abs, len), abs, len, err);
                    if (out->count > before) { carved++; before = out->count; }
                }
            } else if (i + 16 <= total && memcmp(buf + i, "FUJIFILMCCD-RAW ", 16) == 0) {
                len = raf_length(src, abs, max_len);
                add_carved(src, out, RECU_FMT_RAF, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 26 <= total && memcmp(buf + i, "8BPS", 4) == 0) {
                len = psd_length(src, abs, max_len);
                add_carved(src, out, RECU_FMT_PSD, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (plausible_pdf_at(buf, total, i)) {
                if (!carve_progress(opt, here, end - start, &cancelled)) {
                    free_fat_map(&fat_map);
                    free(buf);
                    recu_error_set(err, "operation cancelled");
                    return 0;
                }
                len = pdf_length(src, abs, pdf_cap, opt, here, end - start, &cancelled);
                if (cancelled) {
                    free_fat_map(&fat_map);
                    free(buf);
                    recu_error_set(err, "operation cancelled");
                    return 0;
                }
                add_carved(src, out, RECU_FMT_PDF, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (plausible_zip_at(buf, total, i)) {
                if (!carve_progress(opt, here, end - start, &cancelled)) {
                    free_fat_map(&fat_map);
                    free(buf);
                    recu_error_set(err, "operation cancelled");
                    return 0;
                }
                len = zip_length(src, abs, zip_cap, opt, here, end - start, &cancelled);
                if (cancelled) {
                    free_fat_map(&fat_map);
                    free(buf);
                    recu_error_set(err, "operation cancelled");
                    return 0;
                }
                add_carved(src, out, RECU_FMT_ZIP, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (plausible_mp4_at(buf, total, i, mp4_cap)) {
                if (!carve_progress(opt, here, end - start, &cancelled)) {
                    free_fat_map(&fat_map);
                    free(buf);
                    recu_error_set(err, "operation cancelled");
                    return 0;
                }
                len = mp4_length(src, abs, mp4_cap, opt, here, end - start, &cancelled);
                if (cancelled) {
                    free_fat_map(&fat_map);
                    free(buf);
                    recu_error_set(err, "operation cancelled");
                    return 0;
                }
                add_carved(src, out, iso_bmff_format_at(buf, total, i, mp4_cap), abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 4 <= total && memcmp(buf + i, "\x1A\x45\xDF\xA3", 4) == 0) {
                RecuFileFormat mkv_fmt = mkv_format_and_length(src, abs, max_len, &len);
                add_carved(src, out, mkv_fmt, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 8 <= total &&
                       (memcmp(buf + i, "Rar!\x1A\x07\x00", 7) == 0 ||
                        memcmp(buf + i, "Rar!\x1A\x07\x01\x00", 8) == 0)) {
                len = rar_length(src, abs, max_len);
                add_carved(src, out, RECU_FMT_RAR, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 12 <= total && memcmp(buf + i, "RIFF", 4) == 0 && memcmp(buf + i + 8, "WAVE", 4) == 0) {
                len = riff_length(src, abs, media_cap, "WAVE");
                add_carved(src, out, RECU_FMT_WAV, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 12 <= total && memcmp(buf + i, "RIFF", 4) == 0 && memcmp(buf + i + 8, "AVI ", 4) == 0) {
                len = riff_length(src, abs, media_cap, "AVI ");
                add_carved(src, out, RECU_FMT_AVI, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 12 <= total && memcmp(buf + i, "RIFF", 4) == 0 && memcmp(buf + i + 8, "WEBP", 4) == 0) {
                len = riff_length(src, abs, media_cap, "WEBP");
                add_carved(src, out, RECU_FMT_WEBP, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 54 <= total && buf[i] == 'B' && buf[i + 1] == 'M') {
                len = bmp_length(src, abs, max_len);
                add_carved(src, out, RECU_FMT_BMP, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 13 <= total && (memcmp(buf + i, "GIF87a", 6) == 0 || memcmp(buf + i, "GIF89a", 6) == 0)) {
                len = gif_length(src, abs, max_len > 32ull * mb ? 32ull * mb : max_len, opt, here, end - start, &cancelled);
                if (cancelled) {
                    free_fat_map(&fat_map);
                    free(buf);
                    recu_error_set(err, "operation cancelled");
                    return 0;
                }
                add_carved(src, out, RECU_FMT_GIF, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 27 <= total && memcmp(buf + i, "OggS", 4) == 0 && buf[i + 4] == 0) {
                len = ogg_length(src, abs, media_cap, opt, here, end - start, &cancelled);
                if (cancelled) {
                    free_fat_map(&fat_map);
                    free(buf);
                    recu_error_set(err, "operation cancelled");
                    return 0;
                }
                add_carved(src, out, RECU_FMT_OGG, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 8 <= total && memcmp(buf + i, "fLaC", 4) == 0) {
                len = flac_length(src, abs, media_cap);
                add_carved(src, out, RECU_FMT_FLAC, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (plausible_mp3_at(buf, total, i)) {
                len = mp3_length(src, abs, media_cap);
                add_carved(src, out, RECU_FMT_MP3, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (plausible_aac_at(buf, total, i)) {
                len = aac_length(src, abs, media_cap);
                add_carved(src, out, RECU_FMT_AAC, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 32 <= total && memcmp(buf + i, "\x37\x7A\xBC\xAF\x27\x1C", 6) == 0) {
                len = sevenz_length(src, abs, max_len);
                add_carved(src, out, RECU_FMT_7Z, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 100 <= total && memcmp(buf + i, "SQLite format 3\0", 16) == 0) {
                len = sqlite_length(src, abs, max_len);
                add_carved(src, out, RECU_FMT_SQLITE, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 5u * 188u <= total &&
                       buf[i] == 0x47 && buf[i + 188] == 0x47 && buf[i + 376] == 0x47 &&
                       buf[i + 564] == 0x47 && buf[i + 752] == 0x47) {
                len = ts_length(src, abs, media_cap, 188, 0);
                add_carved(src, out, RECU_FMT_TS, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            } else if (i + 5u * 192u <= total &&
                       buf[i + 4] == 0x47 && buf[i + 196] == 0x47 && buf[i + 388] == 0x47 &&
                       buf[i + 580] == 0x47 && buf[i + 772] == 0x47) {
                len = ts_length(src, abs, media_cap, 192, 4);
                add_carved(src, out, RECU_FMT_M2TS, abs, len, err);
                if (out->count > before) { carved++; before = out->count; }
            }
        }
        if (got == 0) break;
        carry = total < overlap ? total : overlap;
        if (carry) memcpy(buf + chunk, buf + total - carry, carry);
        pos += got;
        if (opt && opt->progress && !opt->progress(opt->progress_user, "deep scan", pos - start, end - start)) {
            free_fat_map(&fat_map);
            free(buf);
            recu_error_set(err, "operation cancelled");
            return 0;
        }
    }
    if (opt && opt->progress) opt->progress(opt->progress_user, "deep scan", end - start, end - start);
    free_fat_map(&fat_map);
    free(buf);
    return 1;
}
