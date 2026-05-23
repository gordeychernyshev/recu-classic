#include "recu.h"

#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static int read_candidate_at(RecuSource *src, const RecuCandidate *c, uint64_t relative, void *buf, size_t n, RecuError *err) {
    if (!src || !src->is_open) {
        recu_error_set(err, "source is not open");
        return 0;
    }
    if (!c || relative > c->size || n > c->size - relative) {
        recu_error_set(err, "validation read is outside candidate bounds");
        return 0;
    }
    if (relative > UINT64_MAX - c->offset) {
        recu_error_set(err, "validation read offset overflow");
        return 0;
    }
    uint64_t absolute = c->offset + relative;
    if (absolute > src->size || (uint64_t)n > src->size - absolute) {
        recu_error_set(err, "validation read is outside source bounds");
        return 0;
    }
    return recu_source_read(src, absolute, buf, n, err);
}

static int read_candidate_tail(RecuSource *src, const RecuCandidate *c, uint8_t **buf, size_t *n, size_t max_tail, RecuError *err) {
    *buf = NULL;
    *n = 0;
    if (!c || c->size == 0) return 0;
    size_t tail = c->size > max_tail ? max_tail : (size_t)c->size;
    uint8_t *tmp = (uint8_t *)malloc(tail);
    if (!tmp) {
        recu_error_set(err, "out of memory while validating tail");
        return 0;
    }
    if (!read_candidate_at(src, c, c->size - tail, tmp, tail, err)) {
        free(tmp);
        return 0;
    }
    *buf = tmp;
    *n = tail;
    return 1;
}

static int contains_bytes(const uint8_t *buf, size_t n, const uint8_t *needle, size_t needle_n, size_t *pos_out) {
    if (!buf || !needle || needle_n == 0 || n < needle_n) return 0;
    for (size_t i = 0; i + needle_n <= n; i++) {
        if (memcmp(buf + i, needle, needle_n) == 0) {
            if (pos_out) *pos_out = i;
            return 1;
        }
    }
    return 0;
}

static int candidate_contains_bytes(RecuSource *src, const RecuCandidate *c, const uint8_t *needle, size_t needle_n, size_t max_scan, RecuError *err) {
    if (!src || !c || !needle || needle_n == 0) return 0;
    size_t n = c->size > max_scan ? max_scan : (size_t)c->size;
    if (n < needle_n) return 0;
    uint8_t *buf = (uint8_t *)malloc(n);
    if (!buf) {
        recu_error_set(err, "out of memory while validating signature markers");
        return 0;
    }
    int ok = 0;
    if (read_candidate_at(src, c, 0, buf, n, err)) {
        ok = contains_bytes(buf, n, needle, needle_n, NULL);
    }
    free(buf);
    return ok;
}

static uint16_t be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static RecuValidationStatus validate_jpg(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size < 4) return RECU_VALIDATION_DAMAGED;
    uint8_t head[3];
    uint8_t tail[2];
    if (!read_candidate_at(src, c, 0, head, sizeof(head), err)) return RECU_VALIDATION_UNKNOWN;
    if (!(head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF)) return RECU_VALIDATION_DAMAGED;
    if (!read_candidate_at(src, c, c->size - 2, tail, sizeof(tail), err)) return RECU_VALIDATION_UNKNOWN;
    int has_eoi = tail[0] == 0xFF && tail[1] == 0xD9;
    uint64_t p = 2;
    int segments = 0;
    int saw_sos = 0;
    int saw_sof = 0;
    while (p + 4 <= c->size && segments++ < 256) {
        uint8_t h[4];
        if (!read_candidate_at(src, c, p, h, 2, err)) return RECU_VALIDATION_UNKNOWN;
        if (h[0] != 0xFF) {
            return saw_sos ? (has_eoi ? RECU_VALIDATION_VALID : RECU_VALIDATION_PARTIAL) : RECU_VALIDATION_DAMAGED;
        }
        while (p + 2 <= c->size) {
            if (!read_candidate_at(src, c, p, h, 2, err)) return RECU_VALIDATION_UNKNOWN;
            if (h[0] != 0xFF) return RECU_VALIDATION_DAMAGED;
            if (h[1] != 0xFF) break;
            p++;
        }
        if (!read_candidate_at(src, c, p, h, sizeof(h), err)) return RECU_VALIDATION_UNKNOWN;
        unsigned char marker = h[1];
        if (marker == 0xD9) return saw_sof ? RECU_VALIDATION_VALID : RECU_VALIDATION_DAMAGED;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) return RECU_VALIDATION_DAMAGED;
        uint16_t seg_len = be16(h + 2);
        if (seg_len < 2 || p + 2ull + seg_len > c->size) return RECU_VALIDATION_PARTIAL;
        if ((marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
            (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF)) {
            saw_sof = 1;
        }
        if (marker == 0xDA) {
            saw_sos = 1;
            break;
        }
        p += 2ull + seg_len;
    }
    if (!saw_sof) return RECU_VALIDATION_DAMAGED;
    if (!saw_sos) return RECU_VALIDATION_PARTIAL;
    return has_eoi ? RECU_VALIDATION_VALID : RECU_VALIDATION_PARTIAL;
}

static RecuValidationStatus validate_pdf(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size < 8) return RECU_VALIDATION_DAMAGED;
    uint8_t head[5];
    if (!read_candidate_at(src, c, 0, head, sizeof(head), err)) return RECU_VALIDATION_UNKNOWN;
    if (memcmp(head, "%PDF-", 5) != 0) return RECU_VALIDATION_DAMAGED;
    uint8_t *tail = NULL;
    size_t tail_n = 0;
    const uint8_t eof[] = {'%', '%', 'E', 'O', 'F'};
    if (!read_candidate_tail(src, c, &tail, &tail_n, 65536, err)) return RECU_VALIDATION_UNKNOWN;
    int ok = contains_bytes(tail, tail_n, eof, sizeof(eof), NULL);
    free(tail);
    return ok ? RECU_VALIDATION_VALID : RECU_VALIDATION_PARTIAL;
}

static RecuValidationStatus validate_zip(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size < 22) return RECU_VALIDATION_DAMAGED;
    uint8_t head[30];
    if (!read_candidate_at(src, c, 0, head, sizeof(head), err)) return RECU_VALIDATION_UNKNOWN;
    if (memcmp(head, "PK\003\004", 4) != 0) {
        return RECU_VALIDATION_DAMAGED;
    }
    uint16_t version = recu_le16(head + 4);
    uint16_t method = recu_le16(head + 8);
    uint16_t name_len = recu_le16(head + 26);
    uint16_t extra_len = recu_le16(head + 28);
    if (version < 10 || version > 63) return RECU_VALIDATION_DAMAGED;
    if (!(method == 0 || method == 8 || method == 9 || method == 12 || method == 14 || method == 98)) return RECU_VALIDATION_DAMAGED;
    if (name_len == 0 || name_len > 255 || extra_len > 4096 || 30ull + name_len + extra_len > c->size) return RECU_VALIDATION_DAMAGED;
    uint8_t *tail = NULL;
    size_t tail_n = 0;
    const uint8_t eocd[] = {0x50, 0x4B, 0x05, 0x06};
    if (!read_candidate_tail(src, c, &tail, &tail_n, 66000, err)) return RECU_VALIDATION_UNKNOWN;
    size_t eocd_pos = 0;
    int found = 0;
    for (size_t i = 0; i + 22 <= tail_n; i++) {
        if (memcmp(tail + i, eocd, sizeof(eocd)) == 0) {
            uint16_t comment = recu_le16(tail + i + 20);
            if (i + 22u + comment <= tail_n) {
                eocd_pos = i;
                found = 1;
            }
        }
    }
    if (!found) {
        free(tail);
        return RECU_VALIDATION_DAMAGED;
    }
    uint16_t disk_no = recu_le16(tail + eocd_pos + 4);
    uint16_t cd_disk = recu_le16(tail + eocd_pos + 6);
    uint32_t cd_size = recu_le32(tail + eocd_pos + 12);
    uint32_t cd_offset = recu_le32(tail + eocd_pos + 16);
    free(tail);
    if (disk_no != 0 || cd_disk != 0) return RECU_VALIDATION_PARTIAL;
    if (cd_size == 0 || (uint64_t)cd_offset + cd_size > c->size) return RECU_VALIDATION_DAMAGED;
    uint8_t cd_sig[4];
    if (!read_candidate_at(src, c, cd_offset, cd_sig, sizeof(cd_sig), err)) return RECU_VALIDATION_UNKNOWN;
    if (memcmp(cd_sig, "PK\001\002", 4) != 0) return RECU_VALIDATION_DAMAGED;
    if (c->format == RECU_FMT_DOCX || c->format == RECU_FMT_XLSX || c->format == RECU_FMT_PPTX) {
        const uint8_t content_types[] = "[Content_Types].xml";
        const uint8_t word_dir[] = "word/";
        const uint8_t xl_dir[] = "xl/";
        const uint8_t ppt_dir[] = "ppt/";
        int has_content_types = candidate_contains_bytes(src, c, content_types, sizeof(content_types) - 1, 4u * 1024u * 1024u, err);
        int has_family = 0;
        if (c->format == RECU_FMT_DOCX) has_family = candidate_contains_bytes(src, c, word_dir, sizeof(word_dir) - 1, 4u * 1024u * 1024u, err);
        else if (c->format == RECU_FMT_XLSX) has_family = candidate_contains_bytes(src, c, xl_dir, sizeof(xl_dir) - 1, 4u * 1024u * 1024u, err);
        else has_family = candidate_contains_bytes(src, c, ppt_dir, sizeof(ppt_dir) - 1, 4u * 1024u * 1024u, err);
        if (!has_content_types || !has_family) return RECU_VALIDATION_DAMAGED;
    }
    if (c->format == RECU_FMT_ODT || c->format == RECU_FMT_ODS || c->format == RECU_FMT_ODP) {
        const uint8_t odt[] = "application/vnd.oasis.opendocument.text";
        const uint8_t ods[] = "application/vnd.oasis.opendocument.spreadsheet";
        const uint8_t odp[] = "application/vnd.oasis.opendocument.presentation";
        const uint8_t content_xml[] = "content.xml";
        int has_mime = 0;
        if (c->format == RECU_FMT_ODT) has_mime = candidate_contains_bytes(src, c, odt, sizeof(odt) - 1, 1024u * 1024u, err);
        else if (c->format == RECU_FMT_ODS) has_mime = candidate_contains_bytes(src, c, ods, sizeof(ods) - 1, 1024u * 1024u, err);
        else has_mime = candidate_contains_bytes(src, c, odp, sizeof(odp) - 1, 1024u * 1024u, err);
        if (!has_mime || !candidate_contains_bytes(src, c, content_xml, sizeof(content_xml) - 1, 4u * 1024u * 1024u, err)) {
            return RECU_VALIDATION_DAMAGED;
        }
    }
    if (c->format == RECU_FMT_EPUB) {
        const uint8_t epub_mime[] = "application/epub+zip";
        const uint8_t container[] = "META-INF/container.xml";
        if (!candidate_contains_bytes(src, c, epub_mime, sizeof(epub_mime) - 1, 1024u * 1024u, err) &&
            !candidate_contains_bytes(src, c, container, sizeof(container) - 1, 4u * 1024u * 1024u, err)) {
            return RECU_VALIDATION_DAMAGED;
        }
    }
    if (c->format == RECU_FMT_APK) {
        const uint8_t manifest[] = "AndroidManifest.xml";
        if (!candidate_contains_bytes(src, c, manifest, sizeof(manifest) - 1, 4u * 1024u * 1024u, err)) {
            return RECU_VALIDATION_DAMAGED;
        }
    }
    if (c->format == RECU_FMT_JAR) {
        const uint8_t manifest[] = "META-INF/MANIFEST.MF";
        if (!candidate_contains_bytes(src, c, manifest, sizeof(manifest) - 1, 4u * 1024u * 1024u, err)) {
            return RECU_VALIDATION_DAMAGED;
        }
    }
    return RECU_VALIDATION_VALID;
}

static RecuValidationStatus validate_png(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (c->size < 20) return RECU_VALIDATION_DAMAGED;
    uint8_t h[12];
    if (!read_candidate_at(src, c, 0, h, 8, err)) return RECU_VALIDATION_UNKNOWN;
    if (memcmp(h, sig, sizeof(sig)) != 0) return RECU_VALIDATION_DAMAGED;
    uint64_t p = 8;
    int chunks = 0;
    while (p + 12 <= c->size && chunks++ < 100000) {
        if (!read_candidate_at(src, c, p, h, 8, err)) return RECU_VALIDATION_UNKNOWN;
        uint32_t len = be32(h);
        if (p + 12ull + len > c->size) return RECU_VALIDATION_PARTIAL;
        p += 8ull + len + 4ull;
        if (memcmp(h + 4, "IEND", 4) == 0) return RECU_VALIDATION_VALID;
    }
    return RECU_VALIDATION_PARTIAL;
}

static RecuValidationStatus validate_mp4(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size < 16) return RECU_VALIDATION_DAMAGED;
    uint8_t h[16];
    if (!read_candidate_at(src, c, 0, h, 12, err)) return RECU_VALIDATION_UNKNOWN;
    if (memcmp(h + 4, "ftyp", 4) != 0) return RECU_VALIDATION_DAMAGED;
    uint64_t p = 0;
    int boxes = 0;
    int saw_ftyp = 0;
    int saw_moov = 0;
    int saw_mdat = 0;
    while (p + 8 <= c->size && boxes++ < 100000) {
        if (!read_candidate_at(src, c, p, h, 8, err)) return RECU_VALIDATION_UNKNOWN;
        uint64_t size = be32(h);
        char type[5] = {(char)h[4], (char)h[5], (char)h[6], (char)h[7], 0};
        uint64_t header = 8;
        if (size == 1) {
            if (p + 16 > c->size || !read_candidate_at(src, c, p, h, 16, err)) return RECU_VALIDATION_PARTIAL;
            size = be64(h + 8);
            header = 16;
        } else if (size == 0) {
            size = c->size - p;
        }
        if (size < header) return RECU_VALIDATION_DAMAGED;
        if (p + size > c->size) return RECU_VALIDATION_PARTIAL;
        if (memcmp(type, "ftyp", 4) == 0) saw_ftyp = 1;
        else if (memcmp(type, "moov", 4) == 0) saw_moov = 1;
        else if (memcmp(type, "mdat", 4) == 0) saw_mdat = 1;
        p += size;
    }
    if (p != c->size) return RECU_VALIDATION_PARTIAL;
    if (saw_ftyp && saw_moov && saw_mdat) return RECU_VALIDATION_VALID;
    if (saw_ftyp && (saw_moov || saw_mdat)) return RECU_VALIDATION_PARTIAL;
    return RECU_VALIDATION_DAMAGED;
}

static int candidate_has_iso_brand(RecuSource *src, const RecuCandidate *c, const char brand[4], RecuError *err) {
    if (c->size < 16) return 0;
    uint8_t h[256];
    size_t n = c->size > sizeof(h) ? sizeof(h) : (size_t)c->size;
    if (!read_candidate_at(src, c, 0, h, n, err)) return 0;
    if (n < 16 || memcmp(h + 4, "ftyp", 4) != 0) return 0;
    uint32_t ftyp_size = be32(h);
    if (ftyp_size < 16 || ftyp_size > n) return 0;
    for (size_t p = 8; p + 4 <= ftyp_size; p += 4) {
        if (memcmp(h + p, brand, 4) == 0) return 1;
    }
    return 0;
}

static RecuValidationStatus validate_alac(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    RecuValidationStatus base = validate_mp4(src, c, err);
    if (base == RECU_VALIDATION_DAMAGED || base == RECU_VALIDATION_UNKNOWN) return base;
    static const uint8_t alac_atom[] = {'a', 'l', 'a', 'c'};
    if (candidate_has_iso_brand(src, c, "alac", err) ||
        candidate_contains_bytes(src, c, alac_atom, sizeof(alac_atom), 4u * 1024u * 1024u, err)) {
        return base;
    }
    return RECU_VALIDATION_DAMAGED;
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
    if (layer == 3) {
        *frame_len = (uint32_t)(((12 * bitrate) / sample_rate + padding) * 4);
    } else {
        int coeff = (version == 3 || layer == 2) ? 144 : 72;
        *frame_len = (uint32_t)((coeff * bitrate) / sample_rate + padding);
    }
    return *frame_len >= 4 && *frame_len <= 4096;
}

static uint64_t skip_id3v2(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size < 10) return 0;
    uint8_t h[10];
    if (!read_candidate_at(src, c, 0, h, sizeof(h), err)) return 0;
    if (memcmp(h, "ID3", 3) != 0 || h[3] == 0xFF || h[4] == 0xFF ||
        (h[6] & 0x80) || (h[7] & 0x80) || (h[8] & 0x80) || (h[9] & 0x80)) {
        return 0;
    }
    uint32_t size = ((uint32_t)h[6] << 21) | ((uint32_t)h[7] << 14) | ((uint32_t)h[8] << 7) | h[9];
    return 10ull + size;
}

static RecuValidationStatus validate_mp3(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    uint64_t p = skip_id3v2(src, c, err);
    int frames = 0;
    while (p + 4 <= c->size && frames < 128) {
        uint8_t h[4];
        if (!read_candidate_at(src, c, p, h, sizeof(h), err)) return RECU_VALIDATION_UNKNOWN;
        uint32_t header = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
        uint32_t frame_len = 0;
        if (!parse_mpeg_audio_header(header, &frame_len)) break;
        if (p + frame_len > c->size) return frames ? RECU_VALIDATION_PARTIAL : RECU_VALIDATION_DAMAGED;
        p += frame_len;
        frames++;
    }
    if (frames == 0) return RECU_VALIDATION_DAMAGED;
    if (p == c->size || (c->size >= 128 && p + 128 >= c->size)) return RECU_VALIDATION_VALID;
    return RECU_VALIDATION_PARTIAL;
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

static RecuValidationStatus validate_aac(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    uint64_t p = 0;
    int frames = 0;
    while (p + 7 <= c->size && frames < 256) {
        uint8_t h[7];
        if (!read_candidate_at(src, c, p, h, sizeof(h), err)) return RECU_VALIDATION_UNKNOWN;
        uint32_t frame_len = 0;
        if (!parse_adts_header(h, &frame_len)) break;
        if (p + frame_len > c->size) return frames ? RECU_VALIDATION_PARTIAL : RECU_VALIDATION_DAMAGED;
        p += frame_len;
        frames++;
    }
    if (frames == 0) return RECU_VALIDATION_DAMAGED;
    return p == c->size ? RECU_VALIDATION_VALID : RECU_VALIDATION_PARTIAL;
}

static int iso_brand_is_heic(const uint8_t *brand) {
    const char *brands[] = {"heic", "heix", "hevc", "hevx", "heim", "heis", "mif1", "msf1", "avif"};
    for (size_t i = 0; i < sizeof(brands) / sizeof(brands[0]); i++) {
        if (memcmp(brand, brands[i], 4) == 0) return 1;
    }
    return 0;
}

static RecuValidationStatus validate_heic(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size < 16) return RECU_VALIDATION_DAMAGED;
    uint8_t h[32];
    if (!read_candidate_at(src, c, 0, h, 16, err)) return RECU_VALIDATION_UNKNOWN;
    uint32_t ftyp_size = be32(h);
    if (memcmp(h + 4, "ftyp", 4) != 0 || ftyp_size < 16 || ftyp_size > c->size) return RECU_VALIDATION_DAMAGED;
    size_t n = ftyp_size > sizeof(h) ? sizeof(h) : ftyp_size;
    if (!read_candidate_at(src, c, 0, h, n, err)) return RECU_VALIDATION_UNKNOWN;
    int heic = 0;
    for (size_t p = 8; p + 4 <= n; p += 4) {
        if (iso_brand_is_heic(h + p)) {
            heic = 1;
            break;
        }
    }
    if (!heic) return RECU_VALIDATION_DAMAGED;
    uint64_t p = 0;
    int boxes = 0;
    int saw_meta = 0;
    while (p + 8 <= c->size && boxes++ < 100000) {
        if (!read_candidate_at(src, c, p, h, 8, err)) return RECU_VALIDATION_UNKNOWN;
        uint64_t size = be32(h);
        char type[5] = {(char)h[4], (char)h[5], (char)h[6], (char)h[7], 0};
        uint64_t header = 8;
        if (size == 1) {
            if (p + 16 > c->size || !read_candidate_at(src, c, p, h, 16, err)) return RECU_VALIDATION_PARTIAL;
            size = be64(h + 8);
            header = 16;
        } else if (size == 0) {
            size = c->size - p;
        }
        if (size < header) return RECU_VALIDATION_DAMAGED;
        if (p + size > c->size) return RECU_VALIDATION_PARTIAL;
        if (memcmp(type, "meta", 4) == 0) saw_meta = 1;
        p += size;
    }
    if (p != c->size) return RECU_VALIDATION_PARTIAL;
    return saw_meta ? RECU_VALIDATION_VALID : RECU_VALIDATION_PARTIAL;
}

static RecuValidationStatus validate_riff(RecuSource *src, const RecuCandidate *c, const char type[4], RecuError *err) {
    if (c->size < 12) return RECU_VALIDATION_DAMAGED;
    uint8_t h[12];
    if (!read_candidate_at(src, c, 0, h, sizeof(h), err)) return RECU_VALIDATION_UNKNOWN;
    if (memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, type, 4) != 0) return RECU_VALIDATION_DAMAGED;
    uint64_t riff_size = (uint64_t)recu_le32(h + 4) + 8ull;
    if (riff_size < 12) return RECU_VALIDATION_DAMAGED;
    if (riff_size > c->size) return RECU_VALIDATION_PARTIAL;
    if (riff_size < c->size && c->kind == RECU_KIND_CARVED) return RECU_VALIDATION_PARTIAL;
    return RECU_VALIDATION_VALID;
}

static RecuValidationStatus validate_bmp(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size < 54) return RECU_VALIDATION_DAMAGED;
    uint8_t h[54];
    if (!read_candidate_at(src, c, 0, h, sizeof(h), err)) return RECU_VALIDATION_UNKNOWN;
    if (h[0] != 'B' || h[1] != 'M') return RECU_VALIDATION_DAMAGED;
    uint32_t file_size = recu_le32(h + 2);
    uint32_t pixel_off = recu_le32(h + 10);
    uint32_t dib = recu_le32(h + 14);
    if (file_size < 54 || file_size > c->size) return RECU_VALIDATION_PARTIAL;
    if (pixel_off < 14 + dib || pixel_off >= file_size) return RECU_VALIDATION_DAMAGED;
    if (!(dib == 12 || dib == 40 || dib == 52 || dib == 56 || dib == 108 || dib == 124)) return RECU_VALIDATION_DAMAGED;
    return file_size == c->size ? RECU_VALIDATION_VALID : RECU_VALIDATION_PARTIAL;
}

static RecuValidationStatus validate_gif(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size < 14) return RECU_VALIDATION_DAMAGED;
    uint8_t h[16];
    if (!read_candidate_at(src, c, 0, h, 13, err)) return RECU_VALIDATION_UNKNOWN;
    if (memcmp(h, "GIF87a", 6) != 0 && memcmp(h, "GIF89a", 6) != 0) return RECU_VALIDATION_DAMAGED;
    uint64_t p = 13;
    if (h[10] & 0x80) p += 3ull * (1ull << ((h[10] & 0x07) + 1));
    while (p < c->size) {
        uint8_t b = 0;
        if (!read_candidate_at(src, c, p++, &b, 1, err)) return RECU_VALIDATION_UNKNOWN;
        if (b == 0x3B) return p == c->size ? RECU_VALIDATION_VALID : RECU_VALIDATION_PARTIAL;
        if (b == 0x21) {
            p++;
            for (;;) {
                uint8_t n = 0;
                if (p >= c->size || !read_candidate_at(src, c, p++, &n, 1, err)) return RECU_VALIDATION_PARTIAL;
                if (n == 0) break;
                p += n;
                if (p > c->size) return RECU_VALIDATION_PARTIAL;
            }
        } else if (b == 0x2C) {
            if (p + 9 > c->size || !read_candidate_at(src, c, p, h, 9, err)) return RECU_VALIDATION_PARTIAL;
            uint8_t packed = h[8];
            p += 9;
            if (packed & 0x80) p += 3ull * (1ull << ((packed & 0x07) + 1));
            p++;
            for (;;) {
                uint8_t n = 0;
                if (p >= c->size || !read_candidate_at(src, c, p++, &n, 1, err)) return RECU_VALIDATION_PARTIAL;
                if (n == 0) break;
                p += n;
                if (p > c->size) return RECU_VALIDATION_PARTIAL;
            }
        } else {
            return RECU_VALIDATION_DAMAGED;
        }
    }
    return RECU_VALIDATION_PARTIAL;
}

static RecuValidationStatus validate_ogg(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    uint64_t p = 0;
    int pages = 0;
    while (p + 27 <= c->size && pages++ < 100000) {
        uint8_t h[27 + 255];
        if (!read_candidate_at(src, c, p, h, 27, err)) return RECU_VALIDATION_UNKNOWN;
        if (memcmp(h, "OggS", 4) != 0 || h[4] != 0) return pages == 1 ? RECU_VALIDATION_DAMAGED : RECU_VALIDATION_PARTIAL;
        uint8_t segments = h[26];
        if (p + 27ull + segments > c->size || !read_candidate_at(src, c, p + 27, h + 27, segments, err)) return RECU_VALIDATION_PARTIAL;
        uint64_t data = 0;
        for (uint8_t i = 0; i < segments; i++) data += h[27 + i];
        uint64_t page_len = 27ull + segments + data;
        if (p + page_len > c->size) return RECU_VALIDATION_PARTIAL;
        p += page_len;
        if (h[5] & 0x04) return p == c->size ? RECU_VALIDATION_VALID : RECU_VALIDATION_PARTIAL;
    }
    return pages > 0 ? RECU_VALIDATION_PARTIAL : RECU_VALIDATION_DAMAGED;
}

static RecuValidationStatus validate_flac(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size < 8) return RECU_VALIDATION_DAMAGED;
    uint8_t h[4];
    if (!read_candidate_at(src, c, 0, h, 4, err)) return RECU_VALIDATION_UNKNOWN;
    if (memcmp(h, "fLaC", 4) != 0) return RECU_VALIDATION_DAMAGED;
    uint64_t p = 4;
    int blocks = 0;
    while (p + 4 <= c->size && blocks++ < 128) {
        if (!read_candidate_at(src, c, p, h, 4, err)) return RECU_VALIDATION_UNKNOWN;
        int last = (h[0] & 0x80) != 0;
        unsigned type = h[0] & 0x7F;
        uint32_t len = ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
        if (type > 6 && type != 127) return RECU_VALIDATION_DAMAGED;
        p += 4ull + len;
        if (p > c->size) return RECU_VALIDATION_PARTIAL;
        if (last) return p < c->size ? RECU_VALIDATION_VALID : RECU_VALIDATION_PARTIAL;
    }
    return RECU_VALIDATION_PARTIAL;
}

static RecuValidationStatus validate_7z(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    static const uint8_t sig[6] = {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C};
    if (c->size < 32) return RECU_VALIDATION_DAMAGED;
    uint8_t h[32];
    if (!read_candidate_at(src, c, 0, h, sizeof(h), err)) return RECU_VALIDATION_UNKNOWN;
    if (memcmp(h, sig, sizeof(sig)) != 0) return RECU_VALIDATION_DAMAGED;
    uint64_t next_off = recu_le64(h + 12);
    uint64_t next_size = recu_le64(h + 20);
    uint64_t total = 32ull + next_off + next_size;
    if (total < 32 || total > c->size) return RECU_VALIDATION_PARTIAL;
    return total == c->size ? RECU_VALIDATION_VALID : RECU_VALIDATION_PARTIAL;
}

static RecuValidationStatus validate_sqlite(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size < 100) return RECU_VALIDATION_DAMAGED;
    uint8_t h[100];
    if (!read_candidate_at(src, c, 0, h, sizeof(h), err)) return RECU_VALIDATION_UNKNOWN;
    if (memcmp(h, "SQLite format 3\0", 16) != 0) return RECU_VALIDATION_DAMAGED;
    uint32_t page_size = ((uint32_t)h[16] << 8) | h[17];
    if (page_size == 1) page_size = 65536;
    if (page_size < 512 || page_size > 65536 || (page_size & (page_size - 1u)) != 0) return RECU_VALIDATION_DAMAGED;
    if (h[18] != 1 || h[19] != 1) return RECU_VALIDATION_DAMAGED;
    uint32_t pages = be32(h + 28);
    uint64_t total = pages ? (uint64_t)pages * page_size : 0;
    if (total && total > c->size) return RECU_VALIDATION_PARTIAL;
    return total && total == c->size ? RECU_VALIDATION_VALID : RECU_VALIDATION_PARTIAL;
}

static RecuValidationStatus validate_rar(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size < 7) return RECU_VALIDATION_DAMAGED;
    uint8_t h[8];
    if (!read_candidate_at(src, c, 0, h, c->size >= 8 ? 8 : 7, err)) return RECU_VALIDATION_UNKNOWN;
    if (memcmp(h, "Rar!\x1A\x07\x00", 7) == 0) return RECU_VALIDATION_VALID;
    if (c->size >= 8 && memcmp(h, "Rar!\x1A\x07\x01\x00", 8) == 0) return RECU_VALIDATION_VALID;
    return RECU_VALIDATION_DAMAGED;
}

static RecuValidationStatus validate_psd(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size < 26) return RECU_VALIDATION_DAMAGED;
    uint8_t h[26];
    if (!read_candidate_at(src, c, 0, h, sizeof(h), err)) return RECU_VALIDATION_UNKNOWN;
    if (memcmp(h, "8BPS", 4) != 0 || be16(h + 4) != 1) return RECU_VALIDATION_DAMAGED;
    uint16_t channels = be16(h + 12);
    uint32_t height = be32(h + 14);
    uint32_t width = be32(h + 18);
    uint16_t depth = be16(h + 22);
    if (channels == 0 || channels > 56 || width == 0 || height == 0 || width > 300000 || height > 300000) return RECU_VALIDATION_DAMAGED;
    if (!(depth == 1 || depth == 8 || depth == 16 || depth == 32)) return RECU_VALIDATION_DAMAGED;
    return RECU_VALIDATION_VALID;
}

static RecuValidationStatus validate_tiff(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size < 8) return RECU_VALIDATION_DAMAGED;
    uint8_t h[8];
    if (!read_candidate_at(src, c, 0, h, sizeof(h), err)) return RECU_VALIDATION_UNKNOWN;
    int le = 0;
    if (h[0] == 'I' && h[1] == 'I' && h[2] == 42 && h[3] == 0) le = 1;
    else if (h[0] == 'M' && h[1] == 'M' && h[2] == 0 && h[3] == 42) le = 0;
    else return RECU_VALIDATION_DAMAGED;
    uint32_t ifd = le ? recu_le32(h + 4) : be32(h + 4);
    if (ifd < 8 || ifd + 2 > c->size) return RECU_VALIDATION_PARTIAL;
    uint8_t nbuf[2];
    if (!read_candidate_at(src, c, ifd, nbuf, 2, err)) return RECU_VALIDATION_UNKNOWN;
    uint16_t entries = le ? recu_le16(nbuf) : be16(nbuf);
    if (entries > 4096 || ifd + 2ull + (uint64_t)entries * 12ull > c->size) return RECU_VALIDATION_PARTIAL;
    return RECU_VALIDATION_VALID;
}

static RecuValidationStatus validate_tiff_family(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    RecuValidationStatus base = validate_tiff(src, c, err);
    if (base == RECU_VALIDATION_DAMAGED || base == RECU_VALIDATION_UNKNOWN) return base;
    if (c->format == RECU_FMT_CR2) {
        uint8_t h[10];
        if (!read_candidate_at(src, c, 0, h, sizeof(h), err)) return RECU_VALIDATION_UNKNOWN;
        return (h[0] == 'I' && h[1] == 'I' && h[2] == 42 && h[3] == 0 && h[8] == 'C' && h[9] == 'R')
            ? base : RECU_VALIDATION_DAMAGED;
    }
    if (c->format == RECU_FMT_DNG) {
        uint8_t h[8];
        if (!read_candidate_at(src, c, 0, h, sizeof(h), err)) return RECU_VALIDATION_UNKNOWN;
        int le = (h[0] == 'I' && h[1] == 'I');
        uint32_t ifd = le ? recu_le32(h + 4) : be32(h + 4);
        for (int ifds = 0; ifd >= 8 && ifds < 8; ifds++) {
            uint8_t nbuf[2];
            if (ifd + 2 > c->size || !read_candidate_at(src, c, ifd, nbuf, 2, err)) return base;
            uint16_t entries = le ? recu_le16(nbuf) : be16(nbuf);
            if (entries > 4096 || ifd + 2ull + (uint64_t)entries * 12ull + 4ull > c->size) return base;
            for (uint16_t e = 0; e < entries; e++) {
                uint8_t ent[12];
                if (!read_candidate_at(src, c, ifd + 2ull + (uint64_t)e * 12ull, ent, sizeof(ent), err)) return base;
                uint16_t tag = le ? recu_le16(ent) : be16(ent);
                if (tag == 50706) return base;
            }
            uint8_t nextbuf[4];
            if (!read_candidate_at(src, c, ifd + 2ull + (uint64_t)entries * 12ull, nextbuf, 4, err)) return base;
            uint32_t next = le ? recu_le32(nextbuf) : be32(nextbuf);
            if (!next || next == ifd) break;
            ifd = next;
        }
        return RECU_VALIDATION_PARTIAL;
    }
    return base;
}

static RecuValidationStatus validate_raf(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size < 108) return RECU_VALIDATION_DAMAGED;
    uint8_t h[108];
    if (!read_candidate_at(src, c, 0, h, sizeof(h), err)) return RECU_VALIDATION_UNKNOWN;
    if (memcmp(h, "FUJIFILMCCD-RAW ", 16) != 0) return RECU_VALIDATION_DAMAGED;
    uint64_t max_end = 108;
    for (int i = 0; i < 3; i++) {
        uint32_t off = be32(h + 84 + i * 8);
        uint32_t len = be32(h + 88 + i * 8);
        if (off && len && (uint64_t)off + len > max_end) max_end = (uint64_t)off + len;
    }
    if (max_end > c->size) return RECU_VALIDATION_PARTIAL;
    return max_end == c->size ? RECU_VALIDATION_VALID : RECU_VALIDATION_PARTIAL;
}

static int candidate_has_iso_brand_prefix(RecuSource *src, const RecuCandidate *c, const char *prefix, size_t prefix_len, RecuError *err) {
    if (c->size < 16 || !prefix || !prefix_len) return 0;
    uint8_t h[256];
    size_t n = c->size > sizeof(h) ? sizeof(h) : (size_t)c->size;
    if (!read_candidate_at(src, c, 0, h, n, err)) return 0;
    if (n < 16 || memcmp(h + 4, "ftyp", 4) != 0) return 0;
    uint32_t ftyp_size = be32(h);
    if (ftyp_size < 16 || ftyp_size > n) return 0;
    for (size_t p = 8; p + 4 <= ftyp_size; p += 4) {
        if (memcmp(h + p, prefix, prefix_len) == 0) return 1;
    }
    return 0;
}

static RecuValidationStatus validate_iso_family(RecuSource *src, const RecuCandidate *c, const char *prefix, size_t prefix_len, RecuError *err) {
    RecuValidationStatus base = validate_mp4(src, c, err);
    if (base == RECU_VALIDATION_DAMAGED || base == RECU_VALIDATION_UNKNOWN) return base;
    return candidate_has_iso_brand_prefix(src, c, prefix, prefix_len, err) ? base : RECU_VALIDATION_DAMAGED;
}

static int ebml_read_vint_candidate(RecuSource *src, const RecuCandidate *c, uint64_t *p, uint64_t limit, uint64_t *value, int keep_marker, RecuError *err) {
    if (*p >= limit) return 0;
    uint8_t first = 0;
    if (!read_candidate_at(src, c, *p, &first, 1, err) || first == 0) return 0;
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
        if (!read_candidate_at(src, c, *p, &b, 1, err)) return 0;
        (*p)++;
        v = (v << 8) | b;
    }
    *value = v;
    return 1;
}

static RecuValidationStatus validate_mkv_webm(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    uint64_t p = 0, id = 0, size = 0;
    if (!ebml_read_vint_candidate(src, c, &p, c->size, &id, 1, err) || id != 0x1A45DFA3ull) return RECU_VALIDATION_DAMAGED;
    if (!ebml_read_vint_candidate(src, c, &p, c->size, &size, 0, err)) return RECU_VALIDATION_DAMAGED;
    uint64_t ebml_end = p + size;
    if (ebml_end <= p || ebml_end > c->size) return RECU_VALIDATION_PARTIAL;
    int doc_ok = c->format == RECU_FMT_MKV;
    while (p + 2 < ebml_end) {
        uint64_t cid = 0, csize = 0;
        if (!ebml_read_vint_candidate(src, c, &p, ebml_end, &cid, 1, err) ||
            !ebml_read_vint_candidate(src, c, &p, ebml_end, &csize, 0, err)) break;
        if (p + csize > ebml_end) break;
        if (cid == 0x4282 && csize > 0 && csize < 32) {
            char doc[32];
            memset(doc, 0, sizeof(doc));
            if (!read_candidate_at(src, c, p, doc, (size_t)csize, err)) return RECU_VALIDATION_UNKNOWN;
            doc_ok = (c->format == RECU_FMT_WEBM) ? strcmp(doc, "webm") == 0 : strcmp(doc, "matroska") == 0;
        }
        p += csize;
    }
    if (!doc_ok) return RECU_VALIDATION_DAMAGED;
    p = ebml_end;
    if (!ebml_read_vint_candidate(src, c, &p, c->size, &id, 1, err) || id != 0x18538067ull) return RECU_VALIDATION_PARTIAL;
    if (!ebml_read_vint_candidate(src, c, &p, c->size, &size, 0, err)) return RECU_VALIDATION_PARTIAL;
    uint64_t end = p + size;
    if (end > c->size) return RECU_VALIDATION_PARTIAL;
    return end == c->size ? RECU_VALIDATION_VALID : RECU_VALIDATION_PARTIAL;
}

static RecuValidationStatus validate_ts_stream(RecuSource *src, const RecuCandidate *c, size_t packet, size_t sync_off, RecuError *err) {
    if (c->size < packet * 5ull) return RECU_VALIDATION_DAMAGED;
    uint64_t p = 0;
    int packets = 0;
    while (p + packet <= c->size && packets < 10000000) {
        uint8_t b = 0;
        if (!read_candidate_at(src, c, p + sync_off, &b, 1, err)) return RECU_VALIDATION_UNKNOWN;
        if (b != 0x47) break;
        p += packet;
        packets++;
    }
    if (packets < 5) return RECU_VALIDATION_DAMAGED;
    return p == c->size ? RECU_VALIDATION_VALID : RECU_VALIDATION_PARTIAL;
}

static RecuValidationStatus validate_text(RecuSource *src, const RecuCandidate *c, RecuError *err) {
    if (c->size == 0) return RECU_VALIDATION_VALID;
    size_t n = c->size > 65536 ? 65536 : (size_t)c->size;
    uint8_t *buf = (uint8_t *)malloc(n);
    if (!buf) {
        recu_error_set(err, "out of memory while validating text");
        return RECU_VALIDATION_UNKNOWN;
    }
    if (!read_candidate_at(src, c, 0, buf, n, err)) {
        free(buf);
        return RECU_VALIDATION_UNKNOWN;
    }
    size_t good = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = buf[i];
        if (ch == 9 || ch == 10 || ch == 13 || (ch >= 32 && ch < 127) || ch >= 0xC0) good++;
    }
    free(buf);
    return good * 100 / n > 85 ? RECU_VALIDATION_VALID : RECU_VALIDATION_DAMAGED;
}

RecuValidationStatus recu_validate_candidate(RecuSource *src, const RecuVolumeInfo *vol, const RecuCandidate *candidate, RecuError *err) {
    (void)vol;
    if (!src || !candidate || !candidate->recoverable) return RECU_VALIDATION_UNKNOWN;
    if (candidate->size == 0) {
        return recu_format_requires_data(candidate->format) ? RECU_VALIDATION_DAMAGED : RECU_VALIDATION_VALID;
    }
    if (candidate->offset >= src->size || candidate->size > src->size - candidate->offset) {
        return RECU_VALIDATION_DAMAGED;
    }
    switch (candidate->format) {
        case RECU_FMT_JPG: return validate_jpg(src, candidate, err);
        case RECU_FMT_PNG: return validate_png(src, candidate, err);
        case RECU_FMT_PDF: return validate_pdf(src, candidate, err);
        case RECU_FMT_ZIP:
        case RECU_FMT_DOCX:
        case RECU_FMT_XLSX:
        case RECU_FMT_PPTX:
        case RECU_FMT_ODT:
        case RECU_FMT_ODS:
        case RECU_FMT_ODP:
        case RECU_FMT_EPUB:
        case RECU_FMT_APK:
        case RECU_FMT_JAR: return validate_zip(src, candidate, err);
        case RECU_FMT_MP4: return validate_mp4(src, candidate, err);
        case RECU_FMT_MOV: return validate_mp4(src, candidate, err);
        case RECU_FMT_3GP: return validate_iso_family(src, candidate, "3g", 2, err);
        case RECU_FMT_3G2: return validate_iso_family(src, candidate, "3g2", 3, err);
        case RECU_FMT_MKV:
        case RECU_FMT_WEBM: return validate_mkv_webm(src, candidate, err);
        case RECU_FMT_TS: return validate_ts_stream(src, candidate, 188, 0, err);
        case RECU_FMT_M2TS: return validate_ts_stream(src, candidate, 192, 4, err);
        case RECU_FMT_ALAC: return validate_alac(src, candidate, err);
        case RECU_FMT_AAC: return validate_aac(src, candidate, err);
        case RECU_FMT_MP3: return validate_mp3(src, candidate, err);
        case RECU_FMT_HEIC: return validate_heic(src, candidate, err);
        case RECU_FMT_CR2:
        case RECU_FMT_DNG:
        case RECU_FMT_NEF:
        case RECU_FMT_ARW:
        case RECU_FMT_ORF:
        case RECU_FMT_RW2: return validate_tiff_family(src, candidate, err);
        case RECU_FMT_CR3: return validate_iso_family(src, candidate, "crx ", 4, err);
        case RECU_FMT_RAF: return validate_raf(src, candidate, err);
        case RECU_FMT_WAV: return validate_riff(src, candidate, "WAVE", err);
        case RECU_FMT_AVI: return validate_riff(src, candidate, "AVI ", err);
        case RECU_FMT_WEBP: return validate_riff(src, candidate, "WEBP", err);
        case RECU_FMT_BMP: return validate_bmp(src, candidate, err);
        case RECU_FMT_GIF: return validate_gif(src, candidate, err);
        case RECU_FMT_OGG: return validate_ogg(src, candidate, err);
        case RECU_FMT_FLAC: return validate_flac(src, candidate, err);
        case RECU_FMT_7Z: return validate_7z(src, candidate, err);
        case RECU_FMT_SQLITE: return validate_sqlite(src, candidate, err);
        case RECU_FMT_RAR: return validate_rar(src, candidate, err);
        case RECU_FMT_PSD: return validate_psd(src, candidate, err);
        case RECU_FMT_TIFF: return validate_tiff(src, candidate, err);
        case RECU_FMT_TEXT: return validate_text(src, candidate, err);
        default: return RECU_VALIDATION_UNSUPPORTED;
    }
}
