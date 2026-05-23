#ifndef RECU_H
#define RECU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define RECU_MAX_NAME 260
#define RECU_MAX_PATH 768
#define RECU_MAX_ERR 512
#define RECU_MAX_PREVIEW 65536

typedef enum RecuFsType {
    RECU_FS_UNKNOWN = 0,
    RECU_FS_FAT16,
    RECU_FS_FAT32,
    RECU_FS_EXFAT
} RecuFsType;

typedef enum RecuCandidateKind {
    RECU_KIND_DELETED_ENTRY = 1,
    RECU_KIND_CARVED = 2
} RecuCandidateKind;

typedef enum RecuFileFormat {
    RECU_FMT_UNKNOWN = 0,
    RECU_FMT_JPG,
    RECU_FMT_PNG,
    RECU_FMT_PDF,
    RECU_FMT_ZIP,
    RECU_FMT_DOCX,
    RECU_FMT_XLSX,
    RECU_FMT_PPTX,
    RECU_FMT_ODT,
    RECU_FMT_ODS,
    RECU_FMT_ODP,
    RECU_FMT_EPUB,
    RECU_FMT_APK,
    RECU_FMT_JAR,
    RECU_FMT_MP4,
    RECU_FMT_MOV,
    RECU_FMT_3GP,
    RECU_FMT_3G2,
    RECU_FMT_MKV,
    RECU_FMT_WEBM,
    RECU_FMT_TS,
    RECU_FMT_M2TS,
    RECU_FMT_ALAC,
    RECU_FMT_AAC,
    RECU_FMT_MP3,
    RECU_FMT_HEIC,
    RECU_FMT_CR2,
    RECU_FMT_CR3,
    RECU_FMT_DNG,
    RECU_FMT_NEF,
    RECU_FMT_ARW,
    RECU_FMT_ORF,
    RECU_FMT_RW2,
    RECU_FMT_RAF,
    RECU_FMT_FLAC,
    RECU_FMT_WAV,
    RECU_FMT_OGG,
    RECU_FMT_AVI,
    RECU_FMT_BMP,
    RECU_FMT_GIF,
    RECU_FMT_WEBP,
    RECU_FMT_TIFF,
    RECU_FMT_RAR,
    RECU_FMT_7Z,
    RECU_FMT_SQLITE,
    RECU_FMT_PSD,
    RECU_FMT_TEXT
} RecuFileFormat;

typedef enum RecuValidationStatus {
    RECU_VALIDATION_UNKNOWN = 0,
    RECU_VALIDATION_VALID,
    RECU_VALIDATION_PARTIAL,
    RECU_VALIDATION_DAMAGED,
    RECU_VALIDATION_UNSUPPORTED
} RecuValidationStatus;

typedef enum RecuReportFormat {
    RECU_REPORT_CSV = 0,
    RECU_REPORT_JSON,
    RECU_REPORT_LOG
} RecuReportFormat;

typedef struct RecuScanReportInfo {
    const char *product_name;
    const char *source_path;
    const char *scan_mode;
    const char *created_at;
    size_t visible_count;
} RecuScanReportInfo;

typedef struct RecuError {
    char message[RECU_MAX_ERR];
} RecuError;

typedef struct RecuSource {
#ifdef _WIN32
    HANDLE handle;
#else
    FILE *file;
#endif
    char path[RECU_MAX_PATH];
    uint64_t size;
    uint32_t sector_size;
    int is_open;
    int is_raw_device;
} RecuSource;

typedef struct RecuVolumeInfo {
    RecuFsType fs_type;
    char fs_name[16];
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t cluster_size;
    uint64_t total_sectors;
    uint64_t total_bytes;

    uint64_t fat_offset;
    uint64_t fat_size;
    uint32_t fat_count;
    uint64_t root_dir_offset;
    uint64_t root_dir_size;
    uint64_t data_offset;
    uint64_t data_size;
    uint32_t root_cluster;
    uint32_t cluster_count;

    uint64_t exfat_cluster_heap_offset;
    uint64_t exfat_allocation_bitmap_offset;
    uint64_t exfat_allocation_bitmap_size;
    uint32_t exfat_allocation_bitmap_cluster;
} RecuVolumeInfo;

typedef struct RecuCandidate {
    uint32_t id;
    RecuCandidateKind kind;
    RecuFsType fs_type;
    RecuFileFormat format;
    char name[RECU_MAX_NAME];
    char path[RECU_MAX_PATH];
    char extension[32];
    char modified_utc[32];
    uint64_t size;
    uint64_t offset;
    uint32_t first_cluster;
    uint32_t cluster_count;
    int is_directory;
    int no_fat_chain;
    int likely_overwritten;
    int confidence;
    char confidence_reasons[256];
    uint32_t duplicate_of;
    uint32_t same_offset_as;
    uint32_t overlaps_with;
    uint64_t overlap_bytes;
    int recoverable;
    int validation_checked;
    RecuValidationStatus validation;
    int metadata_checked;
    int photo_metadata_present;
    int photo_exif_present;
    uint32_t photo_width;
    uint32_t photo_height;
    uint16_t photo_orientation;
    char photo_datetime[32];
    char photo_make[64];
    char photo_model[96];
    char photo_software[64];
    char photo_gps[64];
    char note[160];
} RecuCandidate;

typedef struct RecuCandidateList {
    RecuCandidate *items;
    size_t count;
    size_t capacity;
} RecuCandidateList;

typedef int (*RecuProgressFn)(void *user, const char *stage, uint64_t done, uint64_t total);

typedef struct RecuScanOptions {
    int quick_scan;
    int deep_scan;
    int max_carved_files;
    uint64_t max_carve_bytes;
    RecuProgressFn progress;
    void *progress_user;
} RecuScanOptions;

typedef struct RecuRecoverOptions {
    const char *output_dir;
    int preserve_paths;
    int write_report;
    RecuProgressFn progress;
    void *progress_user;
} RecuRecoverOptions;

typedef struct RecuScanReport {
    RecuVolumeInfo volume;
    RecuCandidateList candidates;
} RecuScanReport;

void recu_error_clear(RecuError *err);
void recu_error_set(RecuError *err, const char *fmt, ...);

const char *recu_fs_name(RecuFsType fs);
const char *recu_format_name(RecuFileFormat fmt);
const char *recu_format_extension(RecuFileFormat fmt);
int recu_format_requires_data(RecuFileFormat fmt);
RecuFileFormat recu_format_from_extension(const char *ext);
const char *recu_validation_name(RecuValidationStatus status);
const char *recu_report_format_name(RecuReportFormat format);
const char *recu_report_format_extension(RecuReportFormat format);
void recu_candidate_confidence_reset(RecuCandidate *candidate, int score, const char *reason);
void recu_candidate_confidence_add(RecuCandidate *candidate, int delta, const char *reason);
void recu_candidate_apply_validation_confidence(RecuCandidate *candidate);

uint16_t recu_le16(const uint8_t *p);
uint32_t recu_le32(const uint8_t *p);
uint64_t recu_le64(const uint8_t *p);
void recu_safe_copy(char *dst, size_t dst_size, const char *src);
void recu_safe_append(char *dst, size_t dst_size, const char *src);
void recu_path_join(char *dst, size_t dst_size, const char *a, const char *b);
void recu_sanitize_filename(char *s);
void recu_make_parent_dirs(const char *path);
int recu_utf16le_to_utf8(const uint8_t *src, size_t code_units, char *dst, size_t dst_size);
int recu_oem_to_utf8(const uint8_t *src, size_t len, char *dst, size_t dst_size);
void recu_dos_datetime(uint16_t date, uint16_t time, char *dst, size_t dst_size);

int recu_list_init(RecuCandidateList *list);
void recu_list_free(RecuCandidateList *list);
int recu_list_push(RecuCandidateList *list, const RecuCandidate *candidate, RecuError *err);

int recu_source_open(RecuSource *src, const char *path, RecuError *err);
void recu_source_close(RecuSource *src);
int recu_source_read(RecuSource *src, uint64_t offset, void *buffer, size_t size, RecuError *err);
int recu_source_read_partial(RecuSource *src, uint64_t offset, void *buffer, size_t size, size_t *read_out, RecuError *err);

int recu_probe_volume(RecuSource *src, RecuVolumeInfo *vol, RecuError *err);
uint64_t recu_cluster_offset(const RecuVolumeInfo *vol, uint32_t cluster);

int recu_scan_fat(RecuSource *src, RecuVolumeInfo *vol, RecuCandidateList *out, const RecuScanOptions *opt, RecuError *err);
int recu_scan_exfat(RecuSource *src, RecuVolumeInfo *vol, RecuCandidateList *out, const RecuScanOptions *opt, RecuError *err);
int recu_carve_scan(RecuSource *src, const RecuVolumeInfo *vol, RecuCandidateList *out, const RecuScanOptions *opt, RecuError *err);
void recu_analyze_candidate_overlaps(RecuSource *src, RecuCandidateList *list);

int recu_scan_source(RecuSource *src, RecuScanReport *report, const RecuScanOptions *opt, RecuError *err);
void recu_scan_report_free(RecuScanReport *report);

int recu_recover_candidate(RecuSource *src, const RecuVolumeInfo *vol, const RecuCandidate *candidate, const RecuRecoverOptions *opt, char *written_path, size_t written_path_size, RecuError *err);
int recu_create_image(RecuSource *src, const char *output_path, RecuProgressFn progress, void *progress_user, RecuError *err);
int recu_write_report(const char *report_path, const RecuVolumeInfo *vol, const RecuCandidateList *list, RecuError *err);
int recu_write_scan_report(const char *report_path, const RecuVolumeInfo *vol, const RecuCandidateList *list, const RecuScanReportInfo *info, RecuReportFormat format, RecuError *err);

int recu_preview_candidate(RecuSource *src, const RecuVolumeInfo *vol, const RecuCandidate *candidate, char *text, size_t text_size, RecuError *err);
RecuValidationStatus recu_validate_candidate(RecuSource *src, const RecuVolumeInfo *vol, const RecuCandidate *candidate, RecuError *err);
int recu_enrich_candidate_metadata(RecuSource *src, const RecuVolumeInfo *vol, RecuCandidate *candidate, RecuError *err);
int recu_enrich_scan_metadata(RecuSource *src, const RecuVolumeInfo *vol, RecuCandidateList *list);

#ifdef _WIN32
typedef struct RecuDriveInfo {
    char volume_path[16];
    char display[128];
    uint64_t size;
    uint32_t drive_type;
} RecuDriveInfo;

int recu_list_windows_drives(RecuDriveInfo *drives, int max_drives);
#endif

#ifdef __cplusplus
}
#endif

#endif
