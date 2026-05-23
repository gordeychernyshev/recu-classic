#include "recu.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sys/stat.h>

void recu_error_clear(RecuError *err) {
    if (err) {
        err->message[0] = 0;
    }
}

void recu_error_set(RecuError *err, const char *fmt, ...) {
    if (!err) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt, ap);
    va_end(ap);
}

const char *recu_fs_name(RecuFsType fs) {
    switch (fs) {
        case RECU_FS_FAT16: return "FAT16";
        case RECU_FS_FAT32: return "FAT32";
        case RECU_FS_EXFAT: return "exFAT";
        default: return "unknown";
    }
}

const char *recu_format_name(RecuFileFormat fmt) {
    switch (fmt) {
        case RECU_FMT_JPG: return "JPEG";
        case RECU_FMT_PNG: return "PNG";
        case RECU_FMT_PDF: return "PDF";
        case RECU_FMT_ZIP: return "ZIP";
        case RECU_FMT_DOCX: return "DOCX";
        case RECU_FMT_XLSX: return "XLSX";
        case RECU_FMT_PPTX: return "PPTX";
        case RECU_FMT_ODT: return "ODT";
        case RECU_FMT_ODS: return "ODS";
        case RECU_FMT_ODP: return "ODP";
        case RECU_FMT_EPUB: return "EPUB";
        case RECU_FMT_APK: return "APK";
        case RECU_FMT_JAR: return "JAR";
        case RECU_FMT_MP4: return "MP4";
        case RECU_FMT_MOV: return "MOV";
        case RECU_FMT_3GP: return "3GP";
        case RECU_FMT_3G2: return "3G2";
        case RECU_FMT_MKV: return "MKV";
        case RECU_FMT_WEBM: return "WEBM";
        case RECU_FMT_TS: return "MPEG-TS";
        case RECU_FMT_M2TS: return "M2TS";
        case RECU_FMT_ALAC: return "ALAC";
        case RECU_FMT_AAC: return "AAC";
        case RECU_FMT_MP3: return "MP3";
        case RECU_FMT_HEIC: return "HEIC";
        case RECU_FMT_CR2: return "CR2";
        case RECU_FMT_CR3: return "CR3";
        case RECU_FMT_DNG: return "DNG";
        case RECU_FMT_NEF: return "NEF";
        case RECU_FMT_ARW: return "ARW";
        case RECU_FMT_ORF: return "ORF";
        case RECU_FMT_RW2: return "RW2";
        case RECU_FMT_RAF: return "RAF";
        case RECU_FMT_FLAC: return "FLAC";
        case RECU_FMT_WAV: return "WAV";
        case RECU_FMT_OGG: return "OGG";
        case RECU_FMT_AVI: return "AVI";
        case RECU_FMT_BMP: return "BMP";
        case RECU_FMT_GIF: return "GIF";
        case RECU_FMT_WEBP: return "WEBP";
        case RECU_FMT_TIFF: return "TIFF";
        case RECU_FMT_RAR: return "RAR";
        case RECU_FMT_7Z: return "7Z";
        case RECU_FMT_SQLITE: return "SQLite";
        case RECU_FMT_PSD: return "PSD";
        case RECU_FMT_TEXT: return "text";
        default: return "unknown";
    }
}

const char *recu_format_extension(RecuFileFormat fmt) {
    switch (fmt) {
        case RECU_FMT_JPG: return ".jpg";
        case RECU_FMT_PNG: return ".png";
        case RECU_FMT_PDF: return ".pdf";
        case RECU_FMT_ZIP: return ".zip";
        case RECU_FMT_DOCX: return ".docx";
        case RECU_FMT_XLSX: return ".xlsx";
        case RECU_FMT_PPTX: return ".pptx";
        case RECU_FMT_ODT: return ".odt";
        case RECU_FMT_ODS: return ".ods";
        case RECU_FMT_ODP: return ".odp";
        case RECU_FMT_EPUB: return ".epub";
        case RECU_FMT_APK: return ".apk";
        case RECU_FMT_JAR: return ".jar";
        case RECU_FMT_MP4: return ".mp4";
        case RECU_FMT_MOV: return ".mov";
        case RECU_FMT_3GP: return ".3gp";
        case RECU_FMT_3G2: return ".3g2";
        case RECU_FMT_MKV: return ".mkv";
        case RECU_FMT_WEBM: return ".webm";
        case RECU_FMT_TS: return ".ts";
        case RECU_FMT_M2TS: return ".m2ts";
        case RECU_FMT_ALAC: return ".m4a";
        case RECU_FMT_AAC: return ".aac";
        case RECU_FMT_MP3: return ".mp3";
        case RECU_FMT_HEIC: return ".heic";
        case RECU_FMT_CR2: return ".cr2";
        case RECU_FMT_CR3: return ".cr3";
        case RECU_FMT_DNG: return ".dng";
        case RECU_FMT_NEF: return ".nef";
        case RECU_FMT_ARW: return ".arw";
        case RECU_FMT_ORF: return ".orf";
        case RECU_FMT_RW2: return ".rw2";
        case RECU_FMT_RAF: return ".raf";
        case RECU_FMT_FLAC: return ".flac";
        case RECU_FMT_WAV: return ".wav";
        case RECU_FMT_OGG: return ".ogg";
        case RECU_FMT_AVI: return ".avi";
        case RECU_FMT_BMP: return ".bmp";
        case RECU_FMT_GIF: return ".gif";
        case RECU_FMT_WEBP: return ".webp";
        case RECU_FMT_TIFF: return ".tif";
        case RECU_FMT_RAR: return ".rar";
        case RECU_FMT_7Z: return ".7z";
        case RECU_FMT_SQLITE: return ".sqlite";
        case RECU_FMT_PSD: return ".psd";
        case RECU_FMT_TEXT: return ".txt";
        default: return ".bin";
    }
}

int recu_format_requires_data(RecuFileFormat fmt) {
    switch (fmt) {
        case RECU_FMT_JPG:
        case RECU_FMT_PNG:
        case RECU_FMT_PDF:
        case RECU_FMT_ZIP:
        case RECU_FMT_DOCX:
        case RECU_FMT_XLSX:
        case RECU_FMT_PPTX:
        case RECU_FMT_ODT:
        case RECU_FMT_ODS:
        case RECU_FMT_ODP:
        case RECU_FMT_EPUB:
        case RECU_FMT_APK:
        case RECU_FMT_JAR:
        case RECU_FMT_MP4:
        case RECU_FMT_MOV:
        case RECU_FMT_3GP:
        case RECU_FMT_3G2:
        case RECU_FMT_MKV:
        case RECU_FMT_WEBM:
        case RECU_FMT_TS:
        case RECU_FMT_M2TS:
        case RECU_FMT_ALAC:
        case RECU_FMT_AAC:
        case RECU_FMT_MP3:
        case RECU_FMT_HEIC:
        case RECU_FMT_CR2:
        case RECU_FMT_CR3:
        case RECU_FMT_DNG:
        case RECU_FMT_NEF:
        case RECU_FMT_ARW:
        case RECU_FMT_ORF:
        case RECU_FMT_RW2:
        case RECU_FMT_RAF:
        case RECU_FMT_FLAC:
        case RECU_FMT_WAV:
        case RECU_FMT_OGG:
        case RECU_FMT_AVI:
        case RECU_FMT_BMP:
        case RECU_FMT_GIF:
        case RECU_FMT_WEBP:
        case RECU_FMT_TIFF:
        case RECU_FMT_RAR:
        case RECU_FMT_7Z:
        case RECU_FMT_SQLITE:
        case RECU_FMT_PSD:
            return 1;
        default:
            return 0;
    }
}

const char *recu_validation_name(RecuValidationStatus status) {
    switch (status) {
        case RECU_VALIDATION_VALID: return "valid";
        case RECU_VALIDATION_PARTIAL: return "partial";
        case RECU_VALIDATION_DAMAGED: return "damaged";
        case RECU_VALIDATION_UNSUPPORTED: return "unsupported";
        default: return "unknown";
    }
}

const char *recu_report_format_name(RecuReportFormat format) {
    switch (format) {
        case RECU_REPORT_JSON: return "JSON";
        case RECU_REPORT_LOG: return "LOG";
        case RECU_REPORT_CSV:
        default: return "CSV";
    }
}

const char *recu_report_format_extension(RecuReportFormat format) {
    switch (format) {
        case RECU_REPORT_JSON: return "json";
        case RECU_REPORT_LOG: return "log";
        case RECU_REPORT_CSV:
        default: return "csv";
    }
}

static int eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

RecuFileFormat recu_format_from_extension(const char *ext) {
    if (!ext || !*ext) return RECU_FMT_UNKNOWN;
    if (*ext == '.') ext++;
    if (eq_ci(ext, "jpg") || eq_ci(ext, "jpeg")) return RECU_FMT_JPG;
    if (eq_ci(ext, "png")) return RECU_FMT_PNG;
    if (eq_ci(ext, "pdf")) return RECU_FMT_PDF;
    if (eq_ci(ext, "zip")) return RECU_FMT_ZIP;
    if (eq_ci(ext, "docx")) return RECU_FMT_DOCX;
    if (eq_ci(ext, "xlsx")) return RECU_FMT_XLSX;
    if (eq_ci(ext, "pptx")) return RECU_FMT_PPTX;
    if (eq_ci(ext, "odt")) return RECU_FMT_ODT;
    if (eq_ci(ext, "ods")) return RECU_FMT_ODS;
    if (eq_ci(ext, "odp")) return RECU_FMT_ODP;
    if (eq_ci(ext, "epub")) return RECU_FMT_EPUB;
    if (eq_ci(ext, "apk")) return RECU_FMT_APK;
    if (eq_ci(ext, "jar")) return RECU_FMT_JAR;
    if (eq_ci(ext, "mp4") || eq_ci(ext, "m4v")) return RECU_FMT_MP4;
    if (eq_ci(ext, "mov")) return RECU_FMT_MOV;
    if (eq_ci(ext, "3gp")) return RECU_FMT_3GP;
    if (eq_ci(ext, "3g2")) return RECU_FMT_3G2;
    if (eq_ci(ext, "mkv")) return RECU_FMT_MKV;
    if (eq_ci(ext, "webm")) return RECU_FMT_WEBM;
    if (eq_ci(ext, "ts")) return RECU_FMT_TS;
    if (eq_ci(ext, "mts") || eq_ci(ext, "m2ts")) return RECU_FMT_M2TS;
    if (eq_ci(ext, "m4a") || eq_ci(ext, "alac")) return RECU_FMT_ALAC;
    if (eq_ci(ext, "aac") || eq_ci(ext, "adts")) return RECU_FMT_AAC;
    if (eq_ci(ext, "mp3")) return RECU_FMT_MP3;
    if (eq_ci(ext, "heic") || eq_ci(ext, "heif")) return RECU_FMT_HEIC;
    if (eq_ci(ext, "cr2")) return RECU_FMT_CR2;
    if (eq_ci(ext, "cr3")) return RECU_FMT_CR3;
    if (eq_ci(ext, "dng")) return RECU_FMT_DNG;
    if (eq_ci(ext, "nef")) return RECU_FMT_NEF;
    if (eq_ci(ext, "arw")) return RECU_FMT_ARW;
    if (eq_ci(ext, "orf")) return RECU_FMT_ORF;
    if (eq_ci(ext, "rw2")) return RECU_FMT_RW2;
    if (eq_ci(ext, "raf")) return RECU_FMT_RAF;
    if (eq_ci(ext, "flac")) return RECU_FMT_FLAC;
    if (eq_ci(ext, "wav")) return RECU_FMT_WAV;
    if (eq_ci(ext, "ogg") || eq_ci(ext, "oga") || eq_ci(ext, "ogv")) return RECU_FMT_OGG;
    if (eq_ci(ext, "avi")) return RECU_FMT_AVI;
    if (eq_ci(ext, "bmp")) return RECU_FMT_BMP;
    if (eq_ci(ext, "gif")) return RECU_FMT_GIF;
    if (eq_ci(ext, "webp")) return RECU_FMT_WEBP;
    if (eq_ci(ext, "tif") || eq_ci(ext, "tiff")) return RECU_FMT_TIFF;
    if (eq_ci(ext, "rar")) return RECU_FMT_RAR;
    if (eq_ci(ext, "7z")) return RECU_FMT_7Z;
    if (eq_ci(ext, "sqlite") || eq_ci(ext, "sqlite3") || eq_ci(ext, "db")) return RECU_FMT_SQLITE;
    if (eq_ci(ext, "psd")) return RECU_FMT_PSD;
    if (eq_ci(ext, "txt") || eq_ci(ext, "log") || eq_ci(ext, "csv")) return RECU_FMT_TEXT;
    return RECU_FMT_UNKNOWN;
}

uint16_t recu_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

uint32_t recu_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint64_t recu_le64(const uint8_t *p) {
    return (uint64_t)recu_le32(p) | ((uint64_t)recu_le32(p + 4) << 32);
}

void recu_safe_copy(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    snprintf(dst, dst_size, "%s", src);
}

void recu_safe_append(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0 || !src) return;
    size_t len = strlen(dst);
    if (len >= dst_size - 1) return;
    snprintf(dst + len, dst_size - len, "%s", src);
}

static int confidence_clamp(int score) {
    if (score < 0) return 0;
    if (score > 98) return 98;
    return score;
}

static int confidence_has_reason(const char *reasons, const char *reason) {
    if (!reasons || !reason || !*reason) return 0;
    size_t needle_len = strlen(reason);
    const char *p = reasons;
    while (*p) {
        while (*p == ' ' || *p == ';') p++;
        if (strncmp(p, reason, needle_len) == 0 && (p[needle_len] == 0 || p[needle_len] == ';')) {
            return 1;
        }
        p = strchr(p, ';');
        if (!p) break;
    }
    return 0;
}

static void confidence_append_reason(RecuCandidate *candidate, const char *reason) {
    if (!candidate || !reason || !*reason) return;
    if (confidence_has_reason(candidate->confidence_reasons, reason)) return;
    if (candidate->confidence_reasons[0]) recu_safe_append(candidate->confidence_reasons, sizeof(candidate->confidence_reasons), "; ");
    recu_safe_append(candidate->confidence_reasons, sizeof(candidate->confidence_reasons), reason);
}

void recu_candidate_confidence_reset(RecuCandidate *candidate, int score, const char *reason) {
    if (!candidate) return;
    candidate->confidence = confidence_clamp(score);
    candidate->confidence_reasons[0] = 0;
    confidence_append_reason(candidate, reason);
}

void recu_candidate_confidence_add(RecuCandidate *candidate, int delta, const char *reason) {
    if (!candidate) return;
    candidate->confidence = confidence_clamp(candidate->confidence + delta);
    confidence_append_reason(candidate, reason);
}

static void confidence_cap(RecuCandidate *candidate, int max_score, const char *reason) {
    if (!candidate) return;
    if (candidate->confidence > max_score) candidate->confidence = confidence_clamp(max_score);
    confidence_append_reason(candidate, reason);
}

static int format_validation_means_eof(RecuFileFormat fmt) {
    switch (fmt) {
        case RECU_FMT_JPG:
        case RECU_FMT_PNG:
        case RECU_FMT_PDF:
        case RECU_FMT_ZIP:
        case RECU_FMT_DOCX:
        case RECU_FMT_XLSX:
        case RECU_FMT_PPTX:
        case RECU_FMT_ODT:
        case RECU_FMT_ODS:
        case RECU_FMT_ODP:
        case RECU_FMT_EPUB:
        case RECU_FMT_APK:
        case RECU_FMT_JAR:
        case RECU_FMT_MP4:
        case RECU_FMT_MOV:
        case RECU_FMT_3GP:
        case RECU_FMT_3G2:
        case RECU_FMT_MKV:
        case RECU_FMT_WEBM:
        case RECU_FMT_OGG:
        case RECU_FMT_RAR:
        case RECU_FMT_7Z:
        case RECU_FMT_FLAC:
        case RECU_FMT_WAV:
        case RECU_FMT_AVI:
        case RECU_FMT_BMP:
        case RECU_FMT_GIF:
        case RECU_FMT_WEBP:
        case RECU_FMT_TIFF:
        case RECU_FMT_SQLITE:
        case RECU_FMT_PSD:
            return 1;
        default:
            return 0;
    }
}

static int format_validation_means_structure(RecuFileFormat fmt) {
    switch (fmt) {
        case RECU_FMT_ZIP:
        case RECU_FMT_DOCX:
        case RECU_FMT_XLSX:
        case RECU_FMT_PPTX:
        case RECU_FMT_ODT:
        case RECU_FMT_ODS:
        case RECU_FMT_ODP:
        case RECU_FMT_EPUB:
        case RECU_FMT_APK:
        case RECU_FMT_JAR:
        case RECU_FMT_MP4:
        case RECU_FMT_MOV:
        case RECU_FMT_3GP:
        case RECU_FMT_3G2:
        case RECU_FMT_MKV:
        case RECU_FMT_WEBM:
        case RECU_FMT_RAR:
        case RECU_FMT_7Z:
        case RECU_FMT_SQLITE:
            return 1;
        default:
            return 0;
    }
}

void recu_candidate_apply_validation_confidence(RecuCandidate *candidate) {
    if (!candidate || !candidate->validation_checked) return;
    if (confidence_has_reason(candidate->confidence_reasons, "validation=valid") ||
        confidence_has_reason(candidate->confidence_reasons, "validation=partial") ||
        confidence_has_reason(candidate->confidence_reasons, "validation=damaged") ||
        confidence_has_reason(candidate->confidence_reasons, "validation=unsupported") ||
        confidence_has_reason(candidate->confidence_reasons, "validation=unknown")) {
        return;
    }
    switch (candidate->validation) {
        case RECU_VALIDATION_VALID:
            recu_candidate_confidence_add(candidate, candidate->likely_overwritten ? 18 : 10, "validation=valid");
            if (format_validation_means_eof(candidate->format)) {
                confidence_append_reason(candidate, "eof=found");
            }
            if (format_validation_means_structure(candidate->format)) {
                confidence_append_reason(candidate, "structure=complete");
            }
            break;
        case RECU_VALIDATION_PARTIAL:
            recu_candidate_confidence_add(candidate, -15, "validation=partial");
            confidence_cap(candidate, 55, "structure=truncated");
            break;
        case RECU_VALIDATION_DAMAGED:
            confidence_cap(candidate, 15, "validation=damaged");
            break;
        case RECU_VALIDATION_UNSUPPORTED:
            confidence_append_reason(candidate, "validation=unsupported");
            break;
        default:
            confidence_append_reason(candidate, "validation=unknown");
            break;
    }
}

void recu_path_join(char *dst, size_t dst_size, const char *a, const char *b) {
    if (!dst || dst_size == 0) return;
    if (!a || !*a) {
        recu_safe_copy(dst, dst_size, b ? b : "");
        return;
    }
    if (!b || !*b) {
        recu_safe_copy(dst, dst_size, a);
        return;
    }
    char sep = '\\';
    size_t alen = strlen(a);
    if (alen > 0 && (a[alen - 1] == '\\' || a[alen - 1] == '/')) {
        snprintf(dst, dst_size, "%s%s", a, b);
    } else {
        snprintf(dst, dst_size, "%s%c%s", a, sep, b);
    }
}

void recu_sanitize_filename(char *s) {
    if (!s) return;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || strchr("<>:\"/\\|?*", c)) {
            *s = '_';
        }
    }
}

void recu_make_parent_dirs(const char *path) {
    if (!path || !*path) return;
#ifdef _WIN32
    wchar_t wtmp[RECU_MAX_PATH];
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wtmp, (int)(sizeof(wtmp) / sizeof(wtmp[0])));
    if (n <= 0) {
        n = MultiByteToWideChar(CP_ACP, 0, path, -1, wtmp, (int)(sizeof(wtmp) / sizeof(wtmp[0])));
    }
    if (n <= 0) return;
    wtmp[(sizeof(wtmp) / sizeof(wtmp[0])) - 1] = 0;
    for (wchar_t *p = wtmp; *p; p++) {
        if (*p == L'/' || *p == L'\\') {
            wchar_t saved = *p;
            *p = 0;
            if (wcslen(wtmp) > 2) {
                CreateDirectoryW(wtmp, NULL);
            }
            *p = saved;
        }
    }
#else
    char tmp[RECU_MAX_PATH];
    recu_safe_copy(tmp, sizeof(tmp), path);
    for (char *p = tmp; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = 0;
            if (strlen(tmp) > 2) {
                mkdir(tmp, 0777);
            }
            *p = saved;
        }
    }
#endif
}

static int utf8_put(uint32_t cp, char *dst, size_t dst_size, size_t *pos) {
    if (cp < 0x80) {
        if (*pos + 1 >= dst_size) return 0;
        dst[(*pos)++] = (char)cp;
    } else if (cp < 0x800) {
        if (*pos + 2 >= dst_size) return 0;
        dst[(*pos)++] = (char)(0xC0 | (cp >> 6));
        dst[(*pos)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        if (*pos + 3 >= dst_size) return 0;
        dst[(*pos)++] = (char)(0xE0 | (cp >> 12));
        dst[(*pos)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[(*pos)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        if (*pos + 4 >= dst_size) return 0;
        dst[(*pos)++] = (char)(0xF0 | (cp >> 18));
        dst[(*pos)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        dst[(*pos)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[(*pos)++] = (char)(0x80 | (cp & 0x3F));
    }
    return 1;
}

int recu_utf16le_to_utf8(const uint8_t *src, size_t code_units, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return 0;
    size_t out = 0;
    for (size_t i = 0; i < code_units; i++) {
        uint16_t w = recu_le16(src + i * 2);
        if (w == 0x0000 || w == 0xFFFF) break;
        uint32_t cp = w;
        if (w >= 0xD800 && w <= 0xDBFF && i + 1 < code_units) {
            uint16_t w2 = recu_le16(src + (i + 1) * 2);
            if (w2 >= 0xDC00 && w2 <= 0xDFFF) {
                cp = 0x10000u + (((uint32_t)w - 0xD800u) << 10) + ((uint32_t)w2 - 0xDC00u);
                i++;
            }
        }
        if (!utf8_put(cp, dst, dst_size, &out)) break;
    }
    dst[out] = 0;
    return (int)out;
}

int recu_oem_to_utf8(const uint8_t *src, size_t len, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return 0;
    dst[0] = 0;
    if (!src || len == 0) return 0;
#ifdef _WIN32
    char tmp[RECU_MAX_NAME];
    if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
    memcpy(tmp, src, len);
    tmp[len] = 0;
    wchar_t wtmp[RECU_MAX_NAME];
    int wn = MultiByteToWideChar(CP_OEMCP, 0, tmp, -1, wtmp, (int)(sizeof(wtmp) / sizeof(wtmp[0])));
    if (wn <= 0) {
        wn = MultiByteToWideChar(CP_ACP, 0, tmp, -1, wtmp, (int)(sizeof(wtmp) / sizeof(wtmp[0])));
    }
    if (wn <= 0) {
        recu_safe_copy(dst, dst_size, tmp);
        return (int)strlen(dst);
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, wtmp, -1, dst, (int)dst_size, NULL, NULL);
    if (n <= 0) {
        dst[0] = 0;
        return 0;
    }
    dst[dst_size - 1] = 0;
    return (int)strlen(dst);
#else
    size_t out = 0;
    for (size_t i = 0; i < len && out + 1 < dst_size; i++) {
        unsigned char ch = src[i];
        dst[out++] = (ch >= 32 && ch < 127) ? (char)ch : '_';
    }
    dst[out] = 0;
    return (int)out;
#endif
}

void recu_dos_datetime(uint16_t date, uint16_t time, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (date == 0) {
        dst[0] = 0;
        return;
    }
    int year = 1980 + ((date >> 9) & 0x7F);
    int month = (date >> 5) & 0x0F;
    int day = date & 0x1F;
    int hour = (time >> 11) & 0x1F;
    int minute = (time >> 5) & 0x3F;
    int second = (time & 0x1F) * 2;
    snprintf(dst, dst_size, "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
}

int recu_list_init(RecuCandidateList *list) {
    if (!list) return 0;
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    return 1;
}

void recu_list_free(RecuCandidateList *list) {
    if (!list) return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

int recu_list_push(RecuCandidateList *list, const RecuCandidate *candidate, RecuError *err) {
    if (!list || !candidate) return 0;
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2 : 128;
        RecuCandidate *items = (RecuCandidate *)realloc(list->items, next * sizeof(*items));
        if (!items) {
            recu_error_set(err, "out of memory while growing candidate list");
            return 0;
        }
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count] = *candidate;
    list->items[list->count].id = (uint32_t)(list->count + 1);
    list->count++;
    return 1;
}
