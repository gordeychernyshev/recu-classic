#include "recu.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbt.h>
#include <objidl.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define IDC_SOURCE   1001
#define IDC_BROWSE   1002
#define IDC_DRIVES   1003
#define IDC_SCAN     1004
#define IDC_DEEP     1005
#define IDC_FILTER   1006
#define IDC_LIST     1007
#define IDC_PREVIEW  1008
#define IDC_RECOVER  1009
#define IDC_IMAGE    1010
#define IDC_REPORT   1011
#define IDC_STATUS   1012
#define IDC_LANG     1013
#define IDC_CATEGORY 1014
#define IDC_SELECT_HIGH 1015
#define IDC_CLEAR_CHECKS 1016
#define IDC_IMAGE_PREVIEW 1017
#define IDC_CANCEL 1018
#define IDC_PROGRESS 1019
#define IDC_VALIDATION 1020
#define IDC_SHOW_OUTPUT 1021
#define IDC_SAFE_MODE 1022
#define IDC_RECOVERY_LOG 1023
#define IDC_REPORT_FORMAT_LABEL 1024
#define IDC_REPORT_FORMAT 1025
#define IDC_LOG_FORMAT_LABEL 1026
#define IDC_LOG_FORMAT 1027
#define IDC_PRESERVE_PATHS 1028
#define IDC_OPEN_OUTPUT 1029
#define IDC_HIDE_ZERO 1030

#define WM_RECU_PROGRESS (WM_APP + 1)
#define WM_RECU_SCAN_DONE (WM_APP + 2)

#define DRIVE_REFRESH_TIMER_ID 42
#define DRIVE_REFRESH_INTERVAL_MS 2000
#define RECU_DEFAULT_WINDOW_W 1360
#define RECU_DEFAULT_WINDOW_H 760
#define RECU_SETTINGS_DIR L"Recu Classic"
#define RECU_SETTINGS_FILE L"settings.ini"
#define RECU_SETTINGS_SECTION L"settings"

typedef enum GuiLang {
    GUI_LANG_EN = 0,
    GUI_LANG_RU = 1
} GuiLang;

typedef enum UiText {
    UI_TITLE,
    UI_BROWSE,
    UI_SCAN,
    UI_DEEP_SCAN,
    UI_SAFE_MODE,
    UI_SAFE_TOOLTIP,
    UI_CREATE_IMAGE,
    UI_RECOVER_SELECTED,
    UI_SAVE_REPORT,
    UI_REPORT_FORMAT,
    UI_RECOVERY_LOG,
    UI_RECOVERY_LOG_TOOLTIP,
    UI_PRESERVE_PATHS,
    UI_PRESERVE_PATHS_TOOLTIP,
    UI_LOG_FORMAT,
    UI_LOG_CREATE_FAILED,
    UI_LOG_CONTINUE_QUESTION,
    UI_REPORT_WARNING,
    UI_SHOW_OUTPUT,
    UI_OPEN_OUTPUT,
    UI_FILTER_CUE,
    UI_READY,
    UI_SCANNING,
    UI_SCAN_COMPLETE_STATUS,
    UI_RECOVERY_COMPLETE_STATUS,
    UI_IMAGE_CREATED_STATUS,
    UI_SAFE_CREATING_IMAGE,
    UI_SAFE_SCANNING_IMAGE,
    UI_REPORT_SAVED_STATUS,
    UI_COL_ID,
    UI_COL_KIND,
    UI_COL_CONF,
    UI_COL_SIZE,
    UI_COL_FORMAT,
    UI_COL_NAME,
    UI_COL_VALIDATION,
    UI_COL_NOTE,
    UI_KIND_DELETED,
    UI_KIND_CARVED,
    UI_FMT_UNKNOWN,
    UI_FMT_TEXT,
    UI_VAL_UNKNOWN,
    UI_VAL_VALID,
    UI_VAL_PARTIAL,
    UI_VAL_DAMAGED,
    UI_VAL_UNSUPPORTED,
    UI_CAT_ALL,
    UI_CAT_RELIABLE,
    UI_CAT_DOCUMENTS,
    UI_CAT_PHOTOS,
    UI_CAT_VIDEO,
    UI_CAT_ARCHIVES,
    UI_CAT_NO_NAME,
    UI_CAT_LOW_CHANCE,
    UI_VAL_FILTER_ALL,
    UI_VAL_FILTER_VALID,
    UI_VAL_FILTER_HIDE_BAD,
    UI_HIDE_ZERO,
    UI_SELECT_HIGH,
    UI_CLEAR_CHECKS,
    UI_RECOVER_CHECKED,
    UI_CANCEL,
    UI_NO_SOURCE,
    UI_OPEN_FAILED,
    UI_SCAN_FAILED,
    UI_SCAN_CANCELLED,
    UI_SELECT_CANDIDATE,
    UI_NO_CHECKED,
    UI_RECOVERY_FAILED,
    UI_SAME_DRIVE_BLOCKED,
    UI_RECOVERY_COMPLETE_TITLE,
    UI_RECOVERED_PREFIX,
    UI_RECOVERED_MANY,
    UI_IMAGE_FAILED,
    UI_IMAGE_CREATED,
    UI_SCAN_FIRST_REPORT,
    UI_REPORT_FAILED,
    UI_CHOOSE_OUTPUT_FOLDER,
    UI_VISIBLE_STATUS,
    UI_PREVIEW_FAILED,
    UI_PREVIEW_NAME,
    UI_PREVIEW_KIND,
    UI_PREVIEW_FS,
    UI_PREVIEW_FORMAT,
    UI_PREVIEW_SIZE,
    UI_PREVIEW_OFFSET,
    UI_PREVIEW_FIRST_CLUSTER,
    UI_PREVIEW_CONFIDENCE,
    UI_PREVIEW_NOTE,
    UI_PREVIEW_BYTES,
    UI_PREVIEW_HEX,
    UI_PREVIEW_TEXT,
    UI_PREVIEW_EMPTY,
    UI_NOTE_CARVED,
    UI_NOTE_FAT_FREE,
    UI_NOTE_FAT_REUSED,
    UI_NOTE_EXFAT_FREE_CONTIG,
    UI_NOTE_EXFAT_REUSED_CONTIG,
    UI_NOTE_EXFAT_FREE_CHAIN,
    UI_NOTE_EXFAT_REUSED_CHAIN,
    UI_STAGE_FAT,
    UI_STAGE_EXFAT,
    UI_STAGE_DEEP,
    UI_STAGE_RECOVER_CONTIG,
    UI_STAGE_RECOVER_CHAIN,
    UI_STAGE_RECOVER_CARVED,
    UI_STAGE_IMAGE,
    UI_COUNT
} UiText;

typedef struct GuiState {
    HINSTANCE inst;
    HWND hwnd;
    HWND source;
    HWND drives;
    HWND lang_combo;
    HWND category_combo;
    HWND validation_combo;
    HWND deep;
    HWND filter;
    HWND list;
    HWND preview;
    HWND image_preview;
    HWND status;
    HWND progress;
    HWND tooltip;
    HWND recovery_log;
    HWND preserve_paths;
    HWND hide_zero;
    HWND report_format_label;
    HWND report_format_combo;
    HWND log_format_label;
    HWND log_format_combo;
    GuiLang lang;
    wchar_t safe_tooltip[512];
    wchar_t log_tooltip[512];
    wchar_t preserve_tooltip[512];
    int safe_tooltip_added;
    int log_tooltip_added;
    int preserve_tooltip_added;
    ULONG_PTR gdiplus_token;
    HBITMAP preview_bitmap;
    uint8_t *checked;
    size_t checked_count;
    char (*recovered_paths)[RECU_MAX_PATH];
    uint8_t *post_checked;
    uint8_t *post_size_ok;
    RecuValidationStatus *post_validation;
    uint64_t *post_written_size;
    int sort_col;
    int sort_desc;
    HANDLE scan_thread;
    volatile LONG cancel_requested;
    int scan_running;
    RecuDriveInfo drive_info[64];
    int drive_count;
    RecuSource src;
    int source_open;
    RecuScanReport report;
    int has_report;
    char last_recovery_path[RECU_MAX_PATH];
    int last_recovery_is_file;
    int suppress_check_sync_once;
    wchar_t settings_path[RECU_MAX_PATH];
    int settings_ready;
    RECT saved_window_rect;
    int has_saved_window_rect;
    int saved_window_maximized;
    int settings_saved;
} GuiState;

typedef struct ProgressMessage {
    char stage[64];
    uint64_t done;
    uint64_t total;
} ProgressMessage;

typedef struct ScanJob {
    HWND hwnd;
    char path[RECU_MAX_PATH];
    int deep;
    volatile LONG *cancel_requested;
    RecuSource src;
    RecuScanReport report;
    RecuError err;
    ULONGLONG last_progress_tick;
    char last_stage[64];
    int ok;
    int cancelled;
} ScanJob;

typedef struct PostCheckResult {
    uint64_t written_size;
    int size_ok;
    int checked;
    RecuValidationStatus validation;
    char message[192];
} PostCheckResult;

static GuiState g;

static void layout(HWND hwnd);
static void update_safe_tooltip(void);
static void update_preserve_tooltip(void);
static RecuReportFormat selected_report_format(HWND combo);
static void populate_report_format_combo(HWND combo, RecuReportFormat selected);
static void update_log_format_enabled(void);
static int selected_category(void);
static int selected_validation_filter(void);
static int selected_candidate_index(void);
static void update_output_buttons(void);
static const char *selected_recovered_path(void);
static void append_post_check_line(char *dst, size_t dst_size, const PostCheckResult *check);
static void load_startup_settings(void);
static void load_control_settings(void);
static void save_settings(void);

static const char *UI_EN[UI_COUNT] = {
    [UI_TITLE] = "Recu Classic - FAT16/FAT32/exFAT Recovery",
    [UI_BROWSE] = "...",
    [UI_SCAN] = "Scan",
    [UI_DEEP_SCAN] = "Deep scan",
    [UI_SAFE_MODE] = "Safe mode",
    [UI_SAFE_TOOLTIP] = "Creates an .img copy first and scans that copy. Lower risk for weak drives, but needs free space and takes longer before scan starts.",
    [UI_CREATE_IMAGE] = "Create image",
    [UI_RECOVER_SELECTED] = "Recover selected",
    [UI_SAVE_REPORT] = "Save report",
    [UI_REPORT_FORMAT] = "Report:",
    [UI_RECOVERY_LOG] = "Write recovery log",
    [UI_RECOVERY_LOG_TOOLTIP] = "Records recovered files, failed files, error reasons, written paths and recovery status.",
    [UI_PRESERVE_PATHS] = "Keep folders",
    [UI_PRESERVE_PATHS_TOOLTIP] = "Restores files into their original folders when the deleted directory path is known, for example DCIM/100CANON/IMG_1234.JPG.",
    [UI_LOG_FORMAT] = "Recovery log:",
    [UI_LOG_CREATE_FAILED] = "Recovery log could not be created.",
    [UI_LOG_CONTINUE_QUESTION] = "Continue recovery without a log?",
    [UI_REPORT_WARNING] = "Files were recovered, but the scan report could not be saved.",
    [UI_SHOW_OUTPUT] = "Show",
    [UI_OPEN_OUTPUT] = "Open",
    [UI_FILTER_CUE] = "filter by name/type",
    [UI_READY] = "Ready",
    [UI_SCANNING] = "Scanning...",
    [UI_SCAN_COMPLETE_STATUS] = "Scan complete: %zu candidate(s), %d visible. FS: %s, cluster: %u bytes",
    [UI_RECOVERY_COMPLETE_STATUS] = "Recovery complete",
    [UI_IMAGE_CREATED_STATUS] = "Image created",
    [UI_SAFE_CREATING_IMAGE] = "Safe mode: creating image...",
    [UI_SAFE_SCANNING_IMAGE] = "Safe mode: scanning image copy",
    [UI_REPORT_SAVED_STATUS] = "Report saved",
    [UI_COL_ID] = "ID",
    [UI_COL_KIND] = "Kind",
    [UI_COL_CONF] = "Conf",
    [UI_COL_SIZE] = "Size",
    [UI_COL_FORMAT] = "Format",
    [UI_COL_NAME] = "Name",
    [UI_COL_VALIDATION] = "Status",
    [UI_COL_NOTE] = "Note",
    [UI_KIND_DELETED] = "deleted",
    [UI_KIND_CARVED] = "carved",
    [UI_FMT_UNKNOWN] = "unknown",
    [UI_FMT_TEXT] = "text",
    [UI_VAL_UNKNOWN] = "unknown",
    [UI_VAL_VALID] = "valid",
    [UI_VAL_PARTIAL] = "partial",
    [UI_VAL_DAMAGED] = "damaged",
    [UI_VAL_UNSUPPORTED] = "n/a",
    [UI_CAT_ALL] = "All",
    [UI_CAT_RELIABLE] = "Reliable",
    [UI_CAT_DOCUMENTS] = "Documents",
    [UI_CAT_PHOTOS] = "Photos",
    [UI_CAT_VIDEO] = "Video",
    [UI_CAT_ARCHIVES] = "Archives",
    [UI_CAT_NO_NAME] = "No name",
    [UI_CAT_LOW_CHANCE] = "Low chance",
    [UI_VAL_FILTER_ALL] = "All statuses",
    [UI_VAL_FILTER_VALID] = "Show valid",
    [UI_VAL_FILTER_HIDE_BAD] = "Hide damaged",
    [UI_HIDE_ZERO] = "Hide 0 B",
    [UI_SELECT_HIGH] = "Check valid",
    [UI_CLEAR_CHECKS] = "Clear",
    [UI_RECOVER_CHECKED] = "Recover checked",
    [UI_CANCEL] = "Cancel",
    [UI_NO_SOURCE] = "Choose a source image or drive first.",
    [UI_OPEN_FAILED] = "Open failed",
    [UI_SCAN_FAILED] = "Scan failed",
    [UI_SCAN_CANCELLED] = "Scan cancelled",
    [UI_SELECT_CANDIDATE] = "Select a recoverable candidate first.",
    [UI_NO_CHECKED] = "Check one or more files, or select a row.",
    [UI_RECOVERY_FAILED] = "Recovery failed",
    [UI_SAME_DRIVE_BLOCKED] = "Cannot recover files to the same drive you are scanning. Choose a folder on another disk to avoid overwriting deleted data.",
    [UI_RECOVERY_COMPLETE_TITLE] = "Recovery complete",
    [UI_RECOVERED_PREFIX] = "Recovered:\n%s",
    [UI_RECOVERED_MANY] = "Recovered %d file(s).\nOutput folder:\n%s",
    [UI_IMAGE_FAILED] = "Image failed",
    [UI_IMAGE_CREATED] = "Image created.",
    [UI_SCAN_FIRST_REPORT] = "Scan first, then save a report.",
    [UI_REPORT_FAILED] = "Report failed",
    [UI_CHOOSE_OUTPUT_FOLDER] = "Choose output folder",
    [UI_VISIBLE_STATUS] = "%zu candidate(s), %d visible. FS: %s, cluster: %u bytes",
    [UI_PREVIEW_FAILED] = "Preview failed: %s",
    [UI_PREVIEW_NAME] = "Name",
    [UI_PREVIEW_KIND] = "Kind",
    [UI_PREVIEW_FS] = "FS",
    [UI_PREVIEW_FORMAT] = "Format",
    [UI_PREVIEW_SIZE] = "Size",
    [UI_PREVIEW_OFFSET] = "Offset",
    [UI_PREVIEW_FIRST_CLUSTER] = "First cluster",
    [UI_PREVIEW_CONFIDENCE] = "Confidence",
    [UI_PREVIEW_NOTE] = "Note",
    [UI_PREVIEW_BYTES] = "bytes",
    [UI_PREVIEW_HEX] = "Hex preview",
    [UI_PREVIEW_TEXT] = "Text preview",
    [UI_PREVIEW_EMPTY] = "(empty file)",
    [UI_NOTE_CARVED] = "raw signature scan; original name/path are unknown",
    [UI_NOTE_FAT_FREE] = "clusters look free; FAT chain is usually cleared after delete, so recovery assumes contiguous clusters",
    [UI_NOTE_FAT_REUSED] = "clusters look allocated/reused; FAT chain is usually cleared after delete, so recovery assumes contiguous clusters",
    [UI_NOTE_EXFAT_FREE_CONTIG] = "bitmap says first clusters are free; NoFatChain contiguous stream",
    [UI_NOTE_EXFAT_REUSED_CONTIG] = "bitmap says clusters may be reused; NoFatChain contiguous stream",
    [UI_NOTE_EXFAT_FREE_CHAIN] = "bitmap says first clusters are free; chain-based stream",
    [UI_NOTE_EXFAT_REUSED_CHAIN] = "bitmap says clusters may be reused; chain-based stream",
    [UI_STAGE_FAT] = "FAT quick scan",
    [UI_STAGE_EXFAT] = "exFAT quick scan",
    [UI_STAGE_DEEP] = "deep scan",
    [UI_STAGE_RECOVER_CONTIG] = "recover contiguous",
    [UI_STAGE_RECOVER_CHAIN] = "recover chain",
    [UI_STAGE_RECOVER_CARVED] = "recover carved",
    [UI_STAGE_IMAGE] = "create image"
};

static const char *UI_RU[UI_COUNT] = {
    [UI_TITLE] = "Recu Classic - восстановление FAT16/FAT32/exFAT",
    [UI_BROWSE] = "...",
    [UI_SCAN] = "Сканировать",
    [UI_DEEP_SCAN] = "Глубокий поиск",
    [UI_SAFE_MODE] = "Безопасный режим",
    [UI_SAFE_TOOLTIP] = "Сначала создается .img-копия, потом сканируется она. Меньше риск для флешки, но нужно место под образ и первый старт дольше.",
    [UI_CREATE_IMAGE] = "Создать образ",
    [UI_RECOVER_SELECTED] = "Восстановить выбранное",
    [UI_SAVE_REPORT] = "Сохранить отчет",
    [UI_REPORT_FORMAT] = "Отчет:",
    [UI_RECOVERY_LOG] = "Вести лог восстановления",
    [UI_RECOVERY_LOG_TOOLTIP] = "Записывает, какие файлы восстановлены, какие не удалось, причины ошибок и пути сохранения.",
    [UI_PRESERVE_PATHS] = "Сохранять исходные папки",
    [UI_PRESERVE_PATHS_TOOLTIP] = "Восстанавливает файлы в исходную структуру папок, если путь известен: например DCIM/100CANON/IMG_1234.JPG.",
    [UI_LOG_FORMAT] = "Лог:",
    [UI_LOG_CREATE_FAILED] = "Не удалось создать лог восстановления.",
    [UI_LOG_CONTINUE_QUESTION] = "Продолжить восстановление без лога?",
    [UI_REPORT_WARNING] = "Файлы восстановлены, но отчет сканирования не удалось сохранить.",
    [UI_SHOW_OUTPUT] = "Показать",
    [UI_OPEN_OUTPUT] = "Открыть",
    [UI_FILTER_CUE] = "фильтр по имени/типу",
    [UI_READY] = "Готово",
    [UI_SCANNING] = "Сканирование...",
    [UI_SCAN_COMPLETE_STATUS] = "Сканирование завершено: найдено %zu, показано %d. ФС: %s, кластер: %u байт",
    [UI_RECOVERY_COMPLETE_STATUS] = "Восстановление завершено",
    [UI_IMAGE_CREATED_STATUS] = "Образ создан",
    [UI_SAFE_CREATING_IMAGE] = "Безопасный режим: создаем образ...",
    [UI_SAFE_SCANNING_IMAGE] = "Безопасный режим: сканируем копию",
    [UI_REPORT_SAVED_STATUS] = "Отчет сохранен",
    [UI_COL_ID] = "ID",
    [UI_COL_KIND] = "Тип",
    [UI_COL_CONF] = "Шанс",
    [UI_COL_SIZE] = "Размер",
    [UI_COL_FORMAT] = "Формат",
    [UI_COL_NAME] = "Имя",
    [UI_COL_VALIDATION] = "Статус",
    [UI_COL_NOTE] = "Заметка",
    [UI_KIND_DELETED] = "удален",
    [UI_KIND_CARVED] = "сигнатура",
    [UI_FMT_UNKNOWN] = "неизвестно",
    [UI_FMT_TEXT] = "текст",
    [UI_VAL_UNKNOWN] = "неизвестно",
    [UI_VAL_VALID] = "валиден",
    [UI_VAL_PARTIAL] = "частично",
    [UI_VAL_DAMAGED] = "поврежден",
    [UI_VAL_UNSUPPORTED] = "н/д",
    [UI_CAT_ALL] = "Все",
    [UI_CAT_RELIABLE] = "Надежные",
    [UI_CAT_DOCUMENTS] = "Документы",
    [UI_CAT_PHOTOS] = "Фото",
    [UI_CAT_VIDEO] = "Видео",
    [UI_CAT_ARCHIVES] = "Архивы",
    [UI_CAT_NO_NAME] = "Без имени",
    [UI_CAT_LOW_CHANCE] = "Низкий шанс",
    [UI_VAL_FILTER_ALL] = "Все статусы",
    [UI_VAL_FILTER_VALID] = "Показать валидные",
    [UI_VAL_FILTER_HIDE_BAD] = "Скрыть поврежденные",
    [UI_HIDE_ZERO] = "Скрыть 0 байт",
    [UI_SELECT_HIGH] = "Отметить валидные",
    [UI_CLEAR_CHECKS] = "Сбросить",
    [UI_RECOVER_CHECKED] = "Восстановить отмеченные",
    [UI_CANCEL] = "Отмена",
    [UI_NO_SOURCE] = "Сначала выберите образ или диск.",
    [UI_OPEN_FAILED] = "Не удалось открыть",
    [UI_SCAN_FAILED] = "Сканирование не удалось",
    [UI_SCAN_CANCELLED] = "Сканирование отменено",
    [UI_SELECT_CANDIDATE] = "Сначала выберите файл, который можно восстановить.",
    [UI_NO_CHECKED] = "Отметьте один или несколько файлов либо выделите строку.",
    [UI_RECOVERY_FAILED] = "Восстановление не удалось",
    [UI_SAME_DRIVE_BLOCKED] = "Так нельзя: нельзя восстанавливать файлы на тот же диск, который сканируется. Выберите папку на другом диске, чтобы не перезаписать удаленные данные.",
    [UI_RECOVERY_COMPLETE_TITLE] = "Восстановление завершено",
    [UI_RECOVERED_PREFIX] = "Восстановлено:\n%s",
    [UI_RECOVERED_MANY] = "Восстановлено файлов: %d.\nПапка:\n%s",
    [UI_IMAGE_FAILED] = "Не удалось создать образ",
    [UI_IMAGE_CREATED] = "Образ создан.",
    [UI_SCAN_FIRST_REPORT] = "Сначала выполните сканирование, затем сохраните отчет.",
    [UI_REPORT_FAILED] = "Не удалось сохранить отчет",
    [UI_CHOOSE_OUTPUT_FOLDER] = "Выберите папку для восстановления",
    [UI_VISIBLE_STATUS] = "Найдено: %zu, показано: %d. ФС: %s, кластер: %u байт",
    [UI_PREVIEW_FAILED] = "Не удалось построить предпросмотр: %s",
    [UI_PREVIEW_NAME] = "Имя",
    [UI_PREVIEW_KIND] = "Тип",
    [UI_PREVIEW_FS] = "ФС",
    [UI_PREVIEW_FORMAT] = "Формат",
    [UI_PREVIEW_SIZE] = "Размер",
    [UI_PREVIEW_OFFSET] = "Смещение",
    [UI_PREVIEW_FIRST_CLUSTER] = "Первый кластер",
    [UI_PREVIEW_CONFIDENCE] = "Шанс восстановления",
    [UI_PREVIEW_NOTE] = "Заметка",
    [UI_PREVIEW_BYTES] = "байт",
    [UI_PREVIEW_HEX] = "Hex-превью",
    [UI_PREVIEW_TEXT] = "Текстовое превью",
    [UI_PREVIEW_EMPTY] = "(пустой файл)",
    [UI_NOTE_CARVED] = "найдено глубоким поиском по сигнатуре; исходные имя и путь неизвестны",
    [UI_NOTE_FAT_FREE] = "кластеры выглядят свободными; после удаления FAT-цепочка обычно очищается, поэтому восстановление предполагает непрерывные кластеры",
    [UI_NOTE_FAT_REUSED] = "кластеры выглядят занятыми/переиспользованными; после удаления FAT-цепочка обычно очищается, поэтому восстановление предполагает непрерывные кластеры",
    [UI_NOTE_EXFAT_FREE_CONTIG] = "bitmap показывает, что первые кластеры свободны; поток NoFatChain, данные идут непрерывно",
    [UI_NOTE_EXFAT_REUSED_CONTIG] = "bitmap показывает, что кластеры могли быть переиспользованы; поток NoFatChain, данные идут непрерывно",
    [UI_NOTE_EXFAT_FREE_CHAIN] = "bitmap показывает, что первые кластеры свободны; поток использует цепочку FAT",
    [UI_NOTE_EXFAT_REUSED_CHAIN] = "bitmap показывает, что кластеры могли быть переиспользованы; поток использует цепочку FAT",
    [UI_STAGE_FAT] = "быстрое сканирование FAT",
    [UI_STAGE_EXFAT] = "быстрое сканирование exFAT",
    [UI_STAGE_DEEP] = "глубокий поиск",
    [UI_STAGE_RECOVER_CONTIG] = "восстановление непрерывного файла",
    [UI_STAGE_RECOVER_CHAIN] = "восстановление по цепочке",
    [UI_STAGE_RECOVER_CARVED] = "восстановление найденного по сигнатуре",
    [UI_STAGE_IMAGE] = "создание образа"
};

static const char *tr(UiText id) {
    const char *s = (g.lang == GUI_LANG_RU) ? UI_RU[id] : UI_EN[id];
    return s ? s : "";
}

static void utf8_to_wide_buf(const char *text, wchar_t *out, int out_count) {
    if (!out || out_count <= 0) return;
    out[0] = 0;
    if (!text) return;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, out_count);
    if (n <= 0) {
        MultiByteToWideChar(CP_UTF8, 0, text, -1, out, out_count);
    }
    out[out_count - 1] = 0;
}

static void utf8_to_wide_lossy_buf(const char *text, wchar_t *out, int out_count) {
    if (!out || out_count <= 0) return;
    out[0] = 0;
    if (!text) return;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, out_count);
    if (n > 0) {
        out[out_count - 1] = 0;
        return;
    }
    int pos = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p && pos + 1 < out_count) {
        uint32_t cp = 0;
        int len = 0;
        if (*p < 0x80) {
            cp = *p++;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = *p & 0x1F;
            len = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = *p & 0x0F;
            len = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = *p & 0x07;
            len = 4;
        } else {
            p++;
            out[pos++] = L'?';
            continue;
        }
        if (len) {
            const unsigned char *start = p;
            int ok = 1;
            p++;
            for (int i = 1; i < len; i++, p++) {
                if ((*p & 0xC0) != 0x80) {
                    ok = 0;
                    break;
                }
                cp = (cp << 6) | (*p & 0x3F);
            }
            if (!ok || (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
                (len == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                p = start + 1;
                out[pos++] = L'?';
                continue;
            }
        }
        if (cp <= 0xFFFF) {
            out[pos++] = (wchar_t)cp;
        } else if (pos + 2 < out_count) {
            cp -= 0x10000;
            out[pos++] = (wchar_t)(0xD800 + (cp >> 10));
            out[pos++] = (wchar_t)(0xDC00 + (cp & 0x3FF));
        } else {
            break;
        }
    }
    out[pos] = 0;
}

static void utf8_multistring_to_wide_buf(const char *text, wchar_t *out, int out_count) {
    if (!out || out_count <= 0) return;
    out[0] = 0;
    if (!text) return;
    int used = 0;
    const char *p = text;
    while (*p && used + 1 < out_count) {
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, -1, out + used, out_count - used);
        if (n <= 0) n = MultiByteToWideChar(CP_UTF8, 0, p, -1, out + used, out_count - used);
        if (n <= 0) break;
        used += n;
        p += strlen(p) + 1;
    }
    if (used < out_count) out[used] = 0;
    out[out_count - 1] = 0;
}

static void wide_to_utf8_buf(const wchar_t *text, char *out, int out_count) {
    if (!out || out_count <= 0) return;
    out[0] = 0;
    if (!text) return;
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, out, out_count, NULL, NULL);
    if (n <= 0) out[0] = 0;
    out[out_count - 1] = 0;
}

static void get_window_text_utf8(HWND hwnd, char *out, int out_count) {
    wchar_t w[RECU_MAX_PATH];
    GetWindowTextW(hwnd, w, (int)(sizeof(w) / sizeof(w[0])));
    wide_to_utf8_buf(w, out, out_count);
}

static void set_window_text_utf8(HWND hwnd, const char *text) {
    wchar_t w[RECU_MAX_PREVIEW + 8192];
    utf8_to_wide_lossy_buf(text, w, (int)(sizeof(w) / sizeof(w[0])));
    SetWindowTextW(hwnd, w);
}

static void wide_safe_copy(wchar_t *dst, size_t dst_count, const wchar_t *src) {
    if (!dst || dst_count == 0) return;
    if (!src) src = L"";
    wcsncpy(dst, src, dst_count - 1);
    dst[dst_count - 1] = 0;
}

static void wide_safe_append(wchar_t *dst, size_t dst_count, const wchar_t *src) {
    if (!dst || dst_count == 0 || !src) return;
    size_t used = wcslen(dst);
    if (used >= dst_count - 1) return;
    wcsncpy(dst + used, src, dst_count - used - 1);
    dst[dst_count - 1] = 0;
}

static int init_settings_path(void) {
    if (g.settings_path[0]) return g.settings_ready;
    wchar_t dir[RECU_MAX_PATH];
    if (GetModuleFileNameW(NULL, dir, (DWORD)(sizeof(dir) / sizeof(dir[0])))) {
        wchar_t exe_dir[RECU_MAX_PATH];
        wide_safe_copy(exe_dir, sizeof(exe_dir) / sizeof(exe_dir[0]), dir);
        wchar_t *slash = wcsrchr(exe_dir, L'\\');
        wchar_t *slash2 = wcsrchr(exe_dir, L'/');
        if (!slash || (slash2 && slash2 > slash)) slash = slash2;
        if (slash) slash[1] = 0;
        else exe_dir[0] = 0;

        wchar_t portable_dir[RECU_MAX_PATH];
        wide_safe_copy(portable_dir, sizeof(portable_dir) / sizeof(portable_dir[0]), exe_dir);
        wide_safe_append(portable_dir, sizeof(portable_dir) / sizeof(portable_dir[0]), L"config");
        DWORD attrs = GetFileAttributesW(portable_dir);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            wide_safe_append(portable_dir, sizeof(portable_dir) / sizeof(portable_dir[0]), L"\\");
            wide_safe_append(portable_dir, sizeof(portable_dir) / sizeof(portable_dir[0]), RECU_SETTINGS_FILE);
            wide_safe_copy(g.settings_path, sizeof(g.settings_path) / sizeof(g.settings_path[0]), portable_dir);
            g.settings_ready = 1;
            return 1;
        }

        wchar_t legacy_portable[RECU_MAX_PATH];
        wide_safe_copy(legacy_portable, sizeof(legacy_portable) / sizeof(legacy_portable[0]), exe_dir);
        wide_safe_append(legacy_portable, sizeof(legacy_portable) / sizeof(legacy_portable[0]), L"recu-classic.ini");
        attrs = GetFileAttributesW(legacy_portable);
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            wide_safe_copy(g.settings_path, sizeof(g.settings_path) / sizeof(g.settings_path[0]), legacy_portable);
            g.settings_ready = 1;
            return 1;
        }
    }

    HRESULT hr = SHGetFolderPathW(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, dir);
    if (SUCCEEDED(hr) && dir[0]) {
        wide_safe_append(dir, sizeof(dir) / sizeof(dir[0]), L"\\");
        wide_safe_append(dir, sizeof(dir) / sizeof(dir[0]), RECU_SETTINGS_DIR);
        if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            dir[0] = 0;
        }
        if (dir[0]) {
            wide_safe_append(dir, sizeof(dir) / sizeof(dir[0]), L"\\");
            wide_safe_append(dir, sizeof(dir) / sizeof(dir[0]), RECU_SETTINGS_FILE);
            wide_safe_copy(g.settings_path, sizeof(g.settings_path) / sizeof(g.settings_path[0]), dir);
            g.settings_ready = 1;
            return 1;
        }
    }

    if (GetModuleFileNameW(NULL, dir, (DWORD)(sizeof(dir) / sizeof(dir[0])))) {
        wchar_t *slash = wcsrchr(dir, L'\\');
        wchar_t *slash2 = wcsrchr(dir, L'/');
        if (!slash || (slash2 && slash2 > slash)) slash = slash2;
        if (slash) slash[1] = 0;
        else dir[0] = 0;
        wide_safe_append(dir, sizeof(dir) / sizeof(dir[0]), L"recu-classic.ini");
        wide_safe_copy(g.settings_path, sizeof(g.settings_path) / sizeof(g.settings_path[0]), dir);
        g.settings_ready = 1;
        return 1;
    }
    return 0;
}

static int settings_read_int(const wchar_t *key, int *out) {
    if (!out || !init_settings_path()) return 0;
    wchar_t buf[64];
    GetPrivateProfileStringW(RECU_SETTINGS_SECTION, key, L"", buf, (DWORD)(sizeof(buf) / sizeof(buf[0])), g.settings_path);
    if (!buf[0]) return 0;
    *out = _wtoi(buf);
    return 1;
}

static int settings_get_int(const wchar_t *key, int def, int min_value, int max_value) {
    int value = def;
    if (settings_read_int(key, &value)) {
        if (value < min_value) value = min_value;
        if (value > max_value) value = max_value;
    }
    return value;
}

static void settings_write_int(const wchar_t *key, int value) {
    if (!init_settings_path()) return;
    wchar_t buf[32];
    swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%d", value);
    WritePrivateProfileStringW(RECU_SETTINGS_SECTION, key, buf, g.settings_path);
}

static int rect_visible_on_monitor(const RECT *rc) {
    if (!rc) return 0;
    if (rc->right <= rc->left || rc->bottom <= rc->top) return 0;
    return MonitorFromRect(rc, MONITOR_DEFAULTTONULL) != NULL;
}

static void load_startup_settings(void) {
    if (!init_settings_path()) return;
    int lang = settings_get_int(L"language", (int)g.lang, 0, 1);
    g.lang = lang == 1 ? GUI_LANG_RU : GUI_LANG_EN;
    int x, y, w, h;
    if (settings_read_int(L"window_x", &x) &&
        settings_read_int(L"window_y", &y) &&
        settings_read_int(L"window_w", &w) &&
        settings_read_int(L"window_h", &h)) {
        if (w < RECU_DEFAULT_WINDOW_W) w = RECU_DEFAULT_WINDOW_W;
        if (h < RECU_DEFAULT_WINDOW_H) h = RECU_DEFAULT_WINDOW_H;
        RECT rc = {x, y, x + w, y + h};
        if (rect_visible_on_monitor(&rc)) {
            g.saved_window_rect = rc;
            g.has_saved_window_rect = 1;
        }
    }
    g.saved_window_maximized = settings_get_int(L"window_maximized", 0, 0, 1);
    g.sort_col = settings_get_int(L"sort_col", -1, -1, 7);
    g.sort_desc = settings_get_int(L"sort_desc", 0, 0, 1);
}

static void select_report_format_combo(HWND combo, RecuReportFormat format) {
    if (!combo) return;
    int count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++) {
        LRESULT data = SendMessageW(combo, CB_GETITEMDATA, i, 0);
        if (data == format) {
            SendMessageW(combo, CB_SETCURSEL, i, 0);
            return;
        }
    }
}

static RecuReportFormat settings_get_report_format(const wchar_t *key, RecuReportFormat def) {
    int value = settings_get_int(key, (int)def, (int)RECU_REPORT_CSV, (int)RECU_REPORT_LOG);
    return (RecuReportFormat)value;
}

static void set_checkbox_from_settings(HWND hwnd, const wchar_t *key, int def) {
    if (!hwnd) return;
    SendMessageW(hwnd, BM_SETCHECK, settings_get_int(key, def, 0, 1) ? BST_CHECKED : BST_UNCHECKED, 0);
}

static int checkbox_is_checked(HWND hwnd) {
    return hwnd && SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void load_control_settings(void) {
    if (!init_settings_path()) return;
    set_checkbox_from_settings(g.deep, L"deep_scan", 0);
    set_checkbox_from_settings(GetDlgItem(g.hwnd, IDC_SAFE_MODE), L"safe_mode", 0);
    set_checkbox_from_settings(g.preserve_paths, L"preserve_paths", 1);
    set_checkbox_from_settings(g.hide_zero, L"hide_zero", 1);
    set_checkbox_from_settings(g.recovery_log, L"recovery_log", 1);
    SendMessageW(g.category_combo, CB_SETCURSEL, settings_get_int(L"category", 0, 0, 7), 0);
    SendMessageW(g.validation_combo, CB_SETCURSEL, settings_get_int(L"validation_filter", 2, 0, 2), 0);
    select_report_format_combo(g.report_format_combo, settings_get_report_format(L"report_format", RECU_REPORT_CSV));
    select_report_format_combo(g.log_format_combo, settings_get_report_format(L"log_format", RECU_REPORT_CSV));
    update_log_format_enabled();
}

static void save_settings(void) {
    if (g.settings_saved || !init_settings_path() || !g.hwnd) return;
    g.settings_saved = 1;
    settings_write_int(L"language", (int)g.lang);
    settings_write_int(L"deep_scan", checkbox_is_checked(g.deep));
    settings_write_int(L"safe_mode", checkbox_is_checked(GetDlgItem(g.hwnd, IDC_SAFE_MODE)));
    settings_write_int(L"preserve_paths", checkbox_is_checked(g.preserve_paths));
    settings_write_int(L"hide_zero", checkbox_is_checked(g.hide_zero));
    settings_write_int(L"recovery_log", checkbox_is_checked(g.recovery_log));
    settings_write_int(L"category", selected_category());
    settings_write_int(L"validation_filter", selected_validation_filter());
    settings_write_int(L"report_format", (int)selected_report_format(g.report_format_combo));
    settings_write_int(L"log_format", (int)selected_report_format(g.log_format_combo));
    settings_write_int(L"sort_col", g.sort_col);
    settings_write_int(L"sort_desc", g.sort_desc);

    WINDOWPLACEMENT wp;
    memset(&wp, 0, sizeof(wp));
    wp.length = sizeof(wp);
    if (GetWindowPlacement(g.hwnd, &wp)) {
        RECT rc = wp.rcNormalPosition;
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        if (width >= RECU_DEFAULT_WINDOW_W && height >= RECU_DEFAULT_WINDOW_H) {
            settings_write_int(L"window_x", rc.left);
            settings_write_int(L"window_y", rc.top);
            settings_write_int(L"window_w", width);
            settings_write_int(L"window_h", height);
        }
        settings_write_int(L"window_maximized", wp.showCmd == SW_SHOWMAXIMIZED ? 1 : 0);
    }
}

static void set_status(const char *text) {
    set_window_text_utf8(g.status, text ? text : "");
    UpdateWindow(g.status);
}

static void set_progress_value(uint64_t done, uint64_t total) {
    if (!g.progress) return;
    if (!total) {
        SendMessageW(g.progress, PBM_SETPOS, 0, 0);
        return;
    }
    double ratio = (double)done / (double)total;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    SendMessageW(g.progress, PBM_SETPOS, (int)(ratio * 10000.0 + 0.5), 0);
}

static void clear_recovery_state(void) {
    free(g.recovered_paths);
    free(g.post_checked);
    free(g.post_size_ok);
    free(g.post_validation);
    free(g.post_written_size);
    g.recovered_paths = NULL;
    g.post_checked = NULL;
    g.post_size_ok = NULL;
    g.post_validation = NULL;
    g.post_written_size = NULL;
    g.last_recovery_path[0] = 0;
    g.last_recovery_is_file = 0;
    if (g.hwnd) {
        EnableWindow(GetDlgItem(g.hwnd, IDC_SHOW_OUTPUT), FALSE);
        EnableWindow(GetDlgItem(g.hwnd, IDC_OPEN_OUTPUT), FALSE);
    }
}

static int allocate_recovery_state(size_t count) {
    clear_recovery_state();
    if (count == 0) return 1;
    g.recovered_paths = (char (*)[RECU_MAX_PATH])calloc(count, sizeof(*g.recovered_paths));
    g.post_checked = (uint8_t *)calloc(count, 1);
    g.post_size_ok = (uint8_t *)calloc(count, 1);
    g.post_validation = (RecuValidationStatus *)calloc(count, sizeof(*g.post_validation));
    g.post_written_size = (uint64_t *)calloc(count, sizeof(*g.post_written_size));
    if (!g.recovered_paths || !g.post_checked || !g.post_size_ok || !g.post_validation || !g.post_written_size) {
        clear_recovery_state();
        return 0;
    }
    return 1;
}

static void set_last_recovery_output(const char *path, int is_file) {
    recu_safe_copy(g.last_recovery_path, sizeof(g.last_recovery_path), path ? path : "");
    g.last_recovery_is_file = is_file;
    update_output_buttons();
}

static void msgbox_utf8(const char *text, const char *title, UINT flags) {
    wchar_t wtext[RECU_MAX_PREVIEW + 8192];
    wchar_t wtitle[256];
    utf8_to_wide_buf(text, wtext, (int)(sizeof(wtext) / sizeof(wtext[0])));
    utf8_to_wide_buf(title, wtitle, (int)(sizeof(wtitle) / sizeof(wtitle[0])));
    MessageBoxW(g.hwnd, wtext, wtitle, flags);
}

static int msgbox_utf8_ret(const char *text, const char *title, UINT flags) {
    wchar_t wtext[RECU_MAX_PREVIEW + 8192];
    wchar_t wtitle[256];
    utf8_to_wide_buf(text, wtext, (int)(sizeof(wtext) / sizeof(wtext[0])));
    utf8_to_wide_buf(title, wtitle, (int)(sizeof(wtitle) / sizeof(wtitle[0])));
    return MessageBoxW(g.hwnd, wtext, wtitle, flags);
}

static int error_contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return 0;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return 1;
    }
    return 0;
}

static int gui_error_is_device_lost(const char *raw) {
    return error_contains_ci(raw, "device disconnected") ||
           error_contains_ci(raw, "became unreadable") ||
           error_contains_ci(raw, "stopped responding") ||
           error_contains_ci(raw, "Win32 error 21") ||
           error_contains_ci(raw, "Win32 error 23") ||
           error_contains_ci(raw, "Win32 error 31") ||
           error_contains_ci(raw, "Win32 error 121") ||
           error_contains_ci(raw, "Win32 error 1117") ||
           error_contains_ci(raw, "Win32 error 1167") ||
           error_contains_ci(raw, "device is not ready");
}

static const char *device_lost_status(void) {
    return g.lang == GUI_LANG_RU ? "Носитель отключился или перестал отвечать" : "Device disconnected or stopped responding";
}

static void local_error_brief(const char *raw, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = 0;
    if (!raw || !*raw) {
        recu_safe_copy(out, out_size,
                       g.lang == GUI_LANG_RU ? "Операция не удалась." : "The operation failed.");
        return;
    }

    int ru = g.lang == GUI_LANG_RU;
    if (error_contains_ci(raw, "Win32 error 5") || error_contains_ci(raw, "Administrator rights") ||
        error_contains_ci(raw, "access is denied")) {
        recu_safe_copy(out, out_size, ru
            ? "Нет доступа к источнику. Если это флешка или диск, запустите Recu Classic от имени администратора. Также проверьте, что носитель подключен и не открыт другой программой."
            : "The source cannot be opened for reading. If this is a physical drive, run Recu Classic as Administrator. Also check that the device is connected and not locked by another program.");
    } else if (error_contains_ci(raw, "Win32 error 32") || error_contains_ci(raw, "sharing violation")) {
        recu_safe_copy(out, out_size, ru
            ? "Источник занят другой программой. Закройте Проводник, просмотрщики, камеры/телефоны или другие утилиты, которые могут держать этот диск, и попробуйте снова."
            : "The source is in use by another program. Close Explorer, viewers, cameras/phones or other tools that may hold this drive, then try again.");
    } else if (gui_error_is_device_lost(raw)) {
        recu_safe_copy(out, out_size, ru
            ? "Носитель отключился, был выдернут или перестал отвечать во время чтения. Операция остановлена без краша; если создавался обычный образ, неполный файл удален. Подключите носитель заново и попробуйте снова."
            : "The source device was removed, disconnected, or stopped responding during reading. The operation was stopped cleanly; if a normal image was being created, the incomplete file was deleted. Reconnect the device and try again.");
    } else if (error_contains_ci(raw, "Win32 error 21") || error_contains_ci(raw, "device is not ready")) {
        recu_safe_copy(out, out_size, ru
            ? "Устройство не готово. Переподключите носитель, дождитесь появления диска в Windows и попробуйте снова."
            : "The device is not ready. Reconnect it, wait until Windows shows the drive, then try again.");
    } else if (error_contains_ci(raw, "Win32 error 87")) {
        recu_safe_copy(out, out_size, ru
            ? "Windows отклонила низкоуровневое чтение с носителя. Попробуйте запустить от имени администратора, переподключить флешку или сначала создать образ в безопасном режиме."
            : "Windows rejected the low-level read request. Try running as Administrator, reconnecting the device, or creating an image first with Safe mode.");
    } else if (error_contains_ci(raw, "unsupported or unrecognized filesystem") ||
               error_contains_ci(raw, "not a FAT16/FAT32 volume") ||
               error_contains_ci(raw, "not an exFAT volume") ||
               error_contains_ci(raw, "invalid FAT") ||
               error_contains_ci(raw, "invalid exFAT")) {
        recu_safe_copy(out, out_size, ru
            ? "Это не похоже на FAT16, FAT32 или exFAT-диск, либо загрузочный сектор поврежден. Recu Classic сейчас работает только с FAT16/FAT32/exFAT; NTFS и другие файловые системы этим режимом не сканируются."
            : "This does not look like a FAT16, FAT32 or exFAT volume, or the boot sector is damaged. Recu Classic currently supports FAT16/FAT32/exFAT only; NTFS and other filesystems are not scanned by this mode.");
    } else if (error_contains_ci(raw, "FAT12 is not supported")) {
        recu_safe_copy(out, out_size, ru
            ? "На носителе обнаружен FAT12. Эта сборка пока поддерживает FAT16, FAT32 и exFAT, но не FAT12."
            : "FAT12 was detected. This build supports FAT16, FAT32 and exFAT, but not FAT12 yet.");
    } else if (error_contains_ci(raw, "empty source path")) {
        recu_safe_copy(out, out_size, ru
            ? "Сначала выберите диск или файл образа."
            : "Choose a drive or image file first.");
    } else if (error_contains_ci(raw, "cannot get size")) {
        recu_safe_copy(out, out_size, ru
            ? "Не удалось определить размер источника. Переподключите носитель, проверьте права администратора и попробуйте снова."
            : "Could not determine the source size. Reconnect the device, check Administrator rights and try again.");
    } else if (error_contains_ci(raw, "read failed") ||
               error_contains_ci(raw, "short read") ||
               error_contains_ci(raw, "raw read timed out") ||
               error_contains_ci(raw, "raw read wait failed") ||
               error_contains_ci(raw, "seek failed")) {
        recu_safe_copy(out, out_size, ru
            ? "Не удалось прочитать часть носителя. Это может быть плохой блок, отвалившаяся флешка, кардридер или ограничение Windows. Лучше попробовать безопасный режим: сначала создать образ, потом сканировать его."
            : "Part of the source could not be read. This may be a bad block, a disconnected device/card reader, or a Windows raw-read limitation. Try Safe mode: create an image first, then scan the image.");
    } else if (error_contains_ci(raw, "same as the source image")) {
        recu_safe_copy(out, out_size, ru
            ? "Нельзя создать образ поверх исходного образа. Выберите другой файл назначения, иначе исходные данные будут испорчены."
            : "The image cannot be created over the source image. Choose a different destination file to avoid corrupting the source.");
    } else if (error_contains_ci(raw, "cannot create output file") ||
               error_contains_ci(raw, "cannot create image") ||
               error_contains_ci(raw, "cannot create '") ||
               error_contains_ci(raw, "cannot write report")) {
        recu_safe_copy(out, out_size, ru
            ? "Не удалось создать файл результата. Выберите другую папку, проверьте свободное место, права записи и длину пути."
            : "The output file could not be created. Choose another folder, check free space, write permissions and path length.");
    } else if (error_contains_ci(raw, "write failed")) {
        recu_safe_copy(out, out_size, ru
            ? "Запись результата оборвалась. Проверьте свободное место, права записи и состояние диска, куда сохраняете файлы."
            : "Writing the output failed. Check free space, write permissions and the health of the destination drive.");
    } else if (error_contains_ci(raw, "out of memory")) {
        recu_safe_copy(out, out_size, ru
            ? "Не хватило оперативной памяти. Закройте лишние программы или попробуйте сканировать образ/носитель меньшими шагами."
            : "Not enough RAM. Close other programs or try scanning an image/device in smaller steps.");
    } else if (error_contains_ci(raw, "cannot start scan thread")) {
        recu_safe_copy(out, out_size, ru
            ? "Не удалось запустить фоновое сканирование. Перезапустите программу и попробуйте снова."
            : "Could not start the background scan. Restart the app and try again.");
    } else if (error_contains_ci(raw, "operation cancelled")) {
        recu_safe_copy(out, out_size, ru
            ? "Операция отменена пользователем."
            : "The operation was cancelled.");
    } else if (error_contains_ci(raw, "candidate is not recoverable") ||
               error_contains_ci(raw, "not a recoverable file")) {
        recu_safe_copy(out, out_size, ru
            ? "Этот элемент нельзя восстановить как файл. Обычно это папка, пустая запись или запись без данных."
            : "This item cannot be recovered as a file. It is usually a directory, an empty entry or an entry without payload.");
    } else if (error_contains_ci(raw, "invalid first cluster") ||
               error_contains_ci(raw, "invalid recovery arguments") ||
               error_contains_ci(raw, "outside candidate bounds")) {
        recu_safe_copy(out, out_size, ru
            ? "У найденного файла некорректные границы или первый кластер. Скорее всего запись повреждена, и восстановление в текущем виде небезопасно."
            : "The found file has invalid boundaries or first cluster. The entry is likely damaged, so recovery is not safe in the current form.");
    } else {
        recu_safe_copy(out, out_size, ru
            ? "Операция не удалась. Ниже техническая причина."
            : "The operation failed. Technical details are below.");
    }
}

static void localize_error_message(const char *raw, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    char brief[1024];
    local_error_brief(raw, brief, sizeof(brief));
    const char *details_label = g.lang == GUI_LANG_RU ? "Технически" : "Technical details";
    if (!raw || !*raw || strcmp(brief, raw) == 0) {
        recu_safe_copy(out, out_size, brief);
        return;
    }
    snprintf(out, out_size, "%s\n\n%s:\n%s", brief, details_label, raw);
}

static void msgbox_error_utf8(const char *raw, const char *title, UINT flags) {
    char msg[RECU_MAX_PREVIEW + 8192];
    localize_error_message(raw, msg, sizeof(msg));
    msgbox_utf8(msg, title, flags);
}

static const char *local_stage(const char *stage) {
    if (strcmp(stage, "FAT quick scan") == 0) return tr(UI_STAGE_FAT);
    if (strcmp(stage, "exFAT quick scan") == 0) return tr(UI_STAGE_EXFAT);
    if (strcmp(stage, "deep scan") == 0) return tr(UI_STAGE_DEEP);
    if (strcmp(stage, "deep scan read") == 0) return g.lang == GUI_LANG_RU ? "чтение с носителя" : "reading device";
    if (strcmp(stage, "deep scan skip") == 0) return g.lang == GUI_LANG_RU ? "пропуск плохого блока" : "skipping bad block";
    if (strcmp(stage, "recover contiguous") == 0) return tr(UI_STAGE_RECOVER_CONTIG);
    if (strcmp(stage, "recover chain") == 0) return tr(UI_STAGE_RECOVER_CHAIN);
    if (strcmp(stage, "recover carved") == 0) return tr(UI_STAGE_RECOVER_CARVED);
    if (strcmp(stage, "create image") == 0) return tr(UI_STAGE_IMAGE);
    return stage;
}

static void format_eta(char *out, size_t out_size, double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    unsigned long long s = (unsigned long long)(seconds + 0.5);
    unsigned long long m = s / 60ull;
    unsigned long long h = m / 60ull;
    if (h) snprintf(out, out_size, "~%lluh %02llum", h, m % 60ull);
    else if (m) snprintf(out, out_size, "~%llum", m);
    else snprintf(out, out_size, "~%llus", s);
}

static void format_progress_status(char *out, size_t out_size, const char *stage, uint64_t done, uint64_t total) {
    static char speed_stage[64];
    static uint64_t speed_total;
    static uint64_t speed_start_done;
    static ULONGLONG speed_start_tick;
    const char *shown_stage = local_stage(stage);
    if (!total) {
        snprintf(out, out_size, "%s", shown_stage);
        return;
    }
    ULONGLONG now = GetTickCount64();
    if (strcmp(speed_stage, stage) != 0 || speed_total != total || done == 0 || done < speed_start_done) {
        recu_safe_copy(speed_stage, sizeof(speed_stage), stage);
        speed_total = total;
        speed_start_done = done;
        speed_start_tick = now;
    }
    if (done > total) done = total;
    double pct = ((double)done * 100.0) / (double)total;
    const double mb = 1024.0 * 1024.0;
    const double gb = mb * 1024.0;
    if (total >= (uint64_t)gb) {
        if (done < (uint64_t)mb) {
            snprintf(out, out_size, "%s: %.1f%% (%llu KB / %.2f GB)",
                     shown_stage, pct, (unsigned long long)(done / 1024ull), (double)total / gb);
        } else if (done < (uint64_t)gb) {
            snprintf(out, out_size, "%s: %.1f%% (%.0f MB / %.2f GB)",
                     shown_stage, pct, (double)done / mb, (double)total / gb);
        } else {
            snprintf(out, out_size, "%s: %.1f%% (%.2f / %.2f GB)",
                     shown_stage, pct, (double)done / gb, (double)total / gb);
        }
    } else if (total >= (uint64_t)mb) {
        snprintf(out, out_size, "%s: %.1f%% (%.1f / %.1f MB)",
                 shown_stage, pct, (double)done / mb, (double)total / mb);
    } else {
        snprintf(out, out_size, "%s: %.1f%% (%llu / %llu B)",
                 shown_stage, pct, (unsigned long long)done, (unsigned long long)total);
    }
    if (done > speed_start_done && done < total && now > speed_start_tick + 1000) {
        double elapsed = (double)(now - speed_start_tick) / 1000.0;
        double bps = (double)(done - speed_start_done) / elapsed;
        if (bps > 1.0) {
            char eta[32];
            format_eta(eta, sizeof(eta), (double)(total - done) / bps);
            char extra[96];
            snprintf(extra, sizeof(extra), " | %.1f MB/s | %s", bps / mb, eta);
            recu_safe_append(out, out_size, extra);
        }
    }
}

static int gui_progress(void *user, const char *stage, uint64_t done, uint64_t total) {
    (void)user;
    if (InterlockedCompareExchange(&g.cancel_requested, 0, 0)) {
        set_status(tr(UI_SCAN_CANCELLED));
        return 0;
    }
    char msg[256];
    format_progress_status(msg, sizeof(msg), stage, done, total);
    set_status(msg);
    set_progress_value(done, total);
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 1;
}

static int worker_progress(void *user, const char *stage, uint64_t done, uint64_t total) {
    ScanJob *job = (ScanJob *)user;
    if (!job) return 1;
    if (InterlockedCompareExchange(job->cancel_requested, 0, 0)) {
        job->cancelled = 1;
        return 0;
    }
    if (job->deep && total && done >= total &&
        (strcmp(stage, "FAT quick scan") == 0 || strcmp(stage, "exFAT quick scan") == 0)) {
        return 1;
    }
    ULONGLONG now = GetTickCount64();
    int stage_changed = strcmp(job->last_stage, stage) != 0;
    if (stage_changed) {
        recu_safe_copy(job->last_stage, sizeof(job->last_stage), stage);
    }
    int force_stage = stage_changed && (done == 0 || done >= total);
    if (!force_stage && done != 0 && done < total && now - job->last_progress_tick < 120) {
        return 1;
    }
    job->last_progress_tick = now;
    ProgressMessage *pm = (ProgressMessage *)calloc(1, sizeof(*pm));
    if (!pm) return 1;
    recu_safe_copy(pm->stage, sizeof(pm->stage), stage);
    pm->done = done;
    pm->total = total;
    PostMessageW(job->hwnd, WM_RECU_PROGRESS, 0, (LPARAM)pm);
    return 1;
}

static DWORD WINAPI scan_thread_proc(LPVOID param) {
    ScanJob *job = (ScanJob *)param;
    recu_error_clear(&job->err);
    if (!recu_source_open(&job->src, job->path, &job->err)) {
        job->ok = 0;
        PostMessageW(job->hwnd, WM_RECU_SCAN_DONE, 0, (LPARAM)job);
        return 0;
    }
    RecuScanOptions opt;
    memset(&opt, 0, sizeof(opt));
    opt.quick_scan = 1;
    opt.deep_scan = job->deep;
    opt.max_carved_files = job->deep ? 1200 : 0;
    opt.max_carve_bytes = job->deep ? (64ull * 1024ull * 1024ull) : 0;
    opt.progress = worker_progress;
    opt.progress_user = job;
    job->ok = recu_scan_source(&job->src, &job->report, &opt, &job->err);
    if (!job->ok) {
        recu_source_close(&job->src);
    }
    PostMessageW(job->hwnd, WM_RECU_SCAN_DONE, 0, (LPARAM)job);
    return 0;
}

static void format_size(uint64_t bytes, char *out, size_t out_size) {
    const char *units_en[] = {"B", "KB", "MB", "GB", "TB"};
    const char *units_ru[] = {"Б", "КБ", "МБ", "ГБ", "ТБ"};
    const char **units = g.lang == GUI_LANG_RU ? units_ru : units_en;
    double value = (double)bytes;
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        unit++;
    }
    if (unit == 0) snprintf(out, out_size, "%llu B", (unsigned long long)bytes);
    else snprintf(out, out_size, "%.2f %s", value, units[unit]);
}

static const char *local_kind(const RecuCandidate *c) {
    return c->kind == RECU_KIND_CARVED ? tr(UI_KIND_CARVED) : tr(UI_KIND_DELETED);
}

static const char *local_format(RecuFileFormat fmt) {
    if (fmt == RECU_FMT_UNKNOWN) return tr(UI_FMT_UNKNOWN);
    if (fmt == RECU_FMT_TEXT) return tr(UI_FMT_TEXT);
    return recu_format_name(fmt);
}

static const char *local_validation(RecuValidationStatus status) {
    switch (status) {
        case RECU_VALIDATION_VALID: return tr(UI_VAL_VALID);
        case RECU_VALIDATION_PARTIAL: return tr(UI_VAL_PARTIAL);
        case RECU_VALIDATION_DAMAGED: return tr(UI_VAL_DAMAGED);
        case RECU_VALIDATION_UNSUPPORTED: return tr(UI_VAL_UNSUPPORTED);
        default: return tr(UI_VAL_UNKNOWN);
    }
}

static RecuValidationStatus ensure_validation(RecuCandidate *c) {
    if (!c) return RECU_VALIDATION_UNKNOWN;
    if (!c->validation_checked && g.source_open) {
        RecuError err;
        recu_error_clear(&err);
        c->validation = recu_validate_candidate(&g.src, &g.report.volume, c, &err);
        c->validation_checked = 1;
        recu_candidate_apply_validation_confidence(c);
    }
    return c->validation_checked ? c->validation : RECU_VALIDATION_UNKNOWN;
}

static const char *local_note(const RecuCandidate *c) {
    static char note[320];
    note[0] = 0;
    if (c->validation_checked) {
        const char *prefix = "";
        if (c->validation == RECU_VALIDATION_VALID) {
            prefix = g.lang == GUI_LANG_RU ? "валиден; " : "valid; ";
        } else if (c->validation == RECU_VALIDATION_PARTIAL) {
            prefix = g.lang == GUI_LANG_RU ? "частично/обрезан; " : "partial/truncated; ";
        } else if (c->validation == RECU_VALIDATION_DAMAGED) {
            prefix = g.lang == GUI_LANG_RU ? "похоже на мусор/поврежден; " : "suspicious/damaged; ";
        } else if (c->validation == RECU_VALIDATION_UNSUPPORTED && !c->recoverable) {
            prefix = g.lang == GUI_LANG_RU ? "не файл для восстановления; " : "not recoverable; ";
        }
        recu_safe_copy(note, sizeof(note), prefix);
    }
    const char *base = NULL;
    if (c->is_directory) {
        base = g.lang == GUI_LANG_RU
            ? "удаленная папка; как файл восстанавливать нечего"
            : "deleted directory entry; not a recoverable file";
    } else if (c->size == 0) {
        base = g.lang == GUI_LANG_RU
            ? "нулевой размер; восстанавливать нечего"
            : "zero-byte file entry; no payload to recover";
    } else if (c->kind == RECU_KIND_CARVED) {
        base = tr(UI_NOTE_CARVED);
    } else if (c->fs_type == RECU_FS_EXFAT) {
        if (c->no_fat_chain) {
            base = c->likely_overwritten ? tr(UI_NOTE_EXFAT_REUSED_CONTIG) : tr(UI_NOTE_EXFAT_FREE_CONTIG);
        } else {
            base = c->likely_overwritten ? tr(UI_NOTE_EXFAT_REUSED_CHAIN) : tr(UI_NOTE_EXFAT_FREE_CHAIN);
        }
    } else if (!base) {
        base = c->likely_overwritten ? tr(UI_NOTE_FAT_REUSED) : tr(UI_NOTE_FAT_FREE);
    }
    recu_safe_append(note, sizeof(note), base);
    if (c->duplicate_of) {
        char detail[96];
        snprintf(detail, sizeof(detail), g.lang == GUI_LANG_RU ? "; дубликат #%u, тот же диапазон байтов" : "; duplicate of #%u, same byte range",
                 c->duplicate_of);
        recu_safe_append(note, sizeof(note), detail);
    } else if (c->same_offset_as) {
        char detail[112];
        snprintf(detail, sizeof(detail), g.lang == GUI_LANG_RU ? "; тот же offset, что #%u (%llu байт пересечения)" : "; same offset as #%u (%llu bytes overlap)",
                 c->same_offset_as, (unsigned long long)c->overlap_bytes);
        recu_safe_append(note, sizeof(note), detail);
    } else if (c->overlaps_with) {
        char detail[112];
        snprintf(detail, sizeof(detail), g.lang == GUI_LANG_RU ? "; пересекается с #%u (%llu байт)" : "; overlaps #%u (%llu bytes)",
                 c->overlaps_with, (unsigned long long)c->overlap_bytes);
        recu_safe_append(note, sizeof(note), detail);
    }
    return note;
}

static const char *local_confidence_reason_token(const char *token) {
    int ru = g.lang == GUI_LANG_RU;
    if (strcmp(token, "source=fat-deleted-entry") == 0) return ru ? "удаленная FAT-запись" : "FAT deleted entry";
    if (strcmp(token, "source=exfat-deleted-entry") == 0) return ru ? "удаленная exFAT-группа записей" : "exFAT deleted entry group";
    if (strcmp(token, "source=signature-scan") == 0) return ru ? "найдено по сигнатуре" : "found by signature";
    if (strcmp(token, "name=lfn-ok") == 0) return ru ? "длинное имя прошло checksum" : "long name checksum is valid";
    if (strcmp(token, "name=short-first-char-lost") == 0) return ru ? "короткое имя, первая буква потеряна" : "short name, first letter is lost";
    if (strcmp(token, "name=entry-group-ok") == 0) return ru ? "имя собрано из exFAT-записей" : "name rebuilt from exFAT entries";
    if (strcmp(token, "name=generated") == 0) return ru ? "имя сгенерировано" : "generated name";
    if (strcmp(token, "size=nonzero") == 0) return ru ? "размер ненулевой" : "non-zero size";
    if (strcmp(token, "size=zero") == 0) return ru ? "нулевой размер" : "zero size";
    if (strcmp(token, "cluster=valid") == 0) return ru ? "первый кластер валиден" : "first cluster is valid";
    if (strcmp(token, "cluster=invalid") == 0) return ru ? "первый кластер некорректен" : "first cluster is invalid";
    if (strcmp(token, "bounds=inside-volume") == 0) return ru ? "диапазон внутри носителя" : "range is inside the volume";
    if (strcmp(token, "bounds=outside-volume") == 0) return ru ? "диапазон выходит за носитель" : "range is outside the volume";
    if (strcmp(token, "allocation=free") == 0) return ru ? "кластеры выглядят свободными" : "clusters look free";
    if (strcmp(token, "allocation=reused") == 0) return ru ? "кластеры выглядят занятыми/перезаписанными" : "clusters look allocated/reused";
    if (strcmp(token, "method=fat-contiguous-assumed") == 0) return ru ? "FAT-цепочка потеряна, копируем подряд" : "FAT chain is gone, copying contiguous data";
    if (strcmp(token, "method=exfat-contiguous") == 0) return ru ? "exFAT NoFatChain, поток подряд" : "exFAT NoFatChain contiguous stream";
    if (strcmp(token, "method=exfat-fat-chain") == 0) return ru ? "exFAT-поток по FAT-цепочке" : "exFAT stream uses FAT chain";
    if (strcmp(token, "method=carving") == 0) return ru ? "восстановление по сигнатуре" : "file carving";
    if (strcmp(token, "validation=valid") == 0) return ru ? "валидатор подтвердил структуру" : "validator confirmed structure";
    if (strcmp(token, "validation=partial") == 0) return ru ? "валидатор видит обрыв/частичный файл" : "validator sees a partial/truncated file";
    if (strcmp(token, "validation=damaged") == 0) return ru ? "валидатор считает файл поврежденным" : "validator says damaged";
    if (strcmp(token, "validation=unknown") == 0) return ru ? "валидатор не смог подтвердить" : "validator could not confirm";
    if (strcmp(token, "validation=unsupported") == 0) return ru ? "для формата нет валидатора" : "no validator for this format";
    if (strcmp(token, "eof=found") == 0) return ru ? "найден конец файла/контейнера" : "file/container end was found";
    if (strcmp(token, "structure=complete") == 0) return ru ? "структура контейнера цельная" : "container structure is complete";
    if (strcmp(token, "structure=truncated") == 0) return ru ? "структура выглядит обрезанной" : "structure looks truncated";
    if (strcmp(token, "metadata=jpeg-exif") == 0) return ru ? "EXIF-метаданные прочитаны" : "JPEG EXIF metadata was read";
    if (strcmp(token, "overlap=duplicate") == 0) return ru ? "дубликат уже найденного диапазона" : "duplicate of an already found range";
    if (strcmp(token, "overlap=same-offset") == 0) return ru ? "тот же offset, что у другого кандидата" : "same offset as another candidate";
    if (strcmp(token, "overlap=range") == 0) return ru ? "диапазон данных пересекается с другим кандидатом" : "data range overlaps another candidate";
    if (strcmp(token, "item=directory") == 0) return ru ? "это папка, не файл" : "directory, not a file";
    return token;
}

static char *trim_token(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        *--end = 0;
    }
    return s;
}

static void append_local_confidence_reasons(char *dst, size_t dst_size, const RecuCandidate *c) {
    if (!c || !c->confidence_reasons[0]) return;
    char copy[sizeof(c->confidence_reasons)];
    recu_safe_copy(copy, sizeof(copy), c->confidence_reasons);
    recu_safe_append(dst, dst_size, g.lang == GUI_LANG_RU ? "Почему такой шанс: " : "Confidence reasons: ");
    char *p = copy;
    int first = 1;
    while (p && *p) {
        char *next = strchr(p, ';');
        if (next) {
            *next = 0;
            next++;
        }
        char *token = trim_token(p);
        if (*token) {
            if (!first) recu_safe_append(dst, dst_size, "; ");
            recu_safe_append(dst, dst_size, local_confidence_reason_token(token));
            first = 0;
        }
        p = next;
    }
    recu_safe_append(dst, dst_size, "\r\n");
}

static int candidate_is_document(const RecuCandidate *c) {
    return c->format == RECU_FMT_PDF || c->format == RECU_FMT_DOCX || c->format == RECU_FMT_XLSX ||
           c->format == RECU_FMT_PPTX || c->format == RECU_FMT_ODT || c->format == RECU_FMT_ODS ||
           c->format == RECU_FMT_ODP || c->format == RECU_FMT_EPUB || c->format == RECU_FMT_TEXT ||
           c->format == RECU_FMT_SQLITE;
}

static int candidate_is_photo(const RecuCandidate *c) {
    return c->format == RECU_FMT_JPG || c->format == RECU_FMT_PNG || c->format == RECU_FMT_BMP ||
           c->format == RECU_FMT_GIF || c->format == RECU_FMT_WEBP || c->format == RECU_FMT_TIFF ||
           c->format == RECU_FMT_HEIC || c->format == RECU_FMT_CR2 || c->format == RECU_FMT_CR3 ||
           c->format == RECU_FMT_DNG || c->format == RECU_FMT_NEF || c->format == RECU_FMT_ARW ||
           c->format == RECU_FMT_ORF || c->format == RECU_FMT_RW2 || c->format == RECU_FMT_RAF ||
           c->format == RECU_FMT_PSD;
}

static int candidate_is_video(const RecuCandidate *c) {
    return c->format == RECU_FMT_MP4 || c->format == RECU_FMT_MOV || c->format == RECU_FMT_3GP ||
           c->format == RECU_FMT_3G2 || c->format == RECU_FMT_MKV || c->format == RECU_FMT_WEBM ||
           c->format == RECU_FMT_TS || c->format == RECU_FMT_M2TS || c->format == RECU_FMT_AVI;
}

static int candidate_is_archive(const RecuCandidate *c) {
    return c->format == RECU_FMT_ZIP || c->format == RECU_FMT_RAR || c->format == RECU_FMT_7Z ||
           c->format == RECU_FMT_APK || c->format == RECU_FMT_JAR;
}

static int candidate_no_name(const RecuCandidate *c) {
    return c->kind == RECU_KIND_CARVED || strncmp(c->name, "carved_", 7) == 0;
}

static int selected_category(void) {
    int sel = (int)SendMessageW(g.category_combo, CB_GETCURSEL, 0, 0);
    return sel < 0 ? 0 : sel;
}

static int selected_validation_filter(void) {
    int sel = (int)SendMessageW(g.validation_combo, CB_GETCURSEL, 0, 0);
    return sel < 0 ? 2 : sel;
}

static int candidate_is_reliable(const RecuCandidate *c) {
    if (!c->recoverable) return 0;
    if (c->duplicate_of || c->same_offset_as || c->overlaps_with) return 0;
    if (c->kind == RECU_KIND_CARVED) {
        return c->validation_checked && c->validation == RECU_VALIDATION_VALID && c->confidence >= 70;
    }
    return c->confidence >= 70 && !c->likely_overwritten;
}

static int category_matches(const RecuCandidate *c) {
    switch (selected_category()) {
        case 1: return candidate_is_reliable(c);
        case 2: return candidate_is_document(c);
        case 3: return candidate_is_photo(c);
        case 4: return candidate_is_video(c);
        case 5: return candidate_is_archive(c);
        case 6: return candidate_no_name(c);
        case 7: return c->confidence < 70 || c->likely_overwritten;
        default: return 1;
    }
}

static int validation_filter_matches(RecuCandidate *c) {
    RecuValidationStatus val = c->validation_checked ? c->validation : RECU_VALIDATION_UNKNOWN;
    switch (selected_validation_filter()) {
        case 1:
            if (!c->recoverable) return 0;
            val = ensure_validation(c);
            return val == RECU_VALIDATION_VALID;
        case 2:
            if (!c->recoverable) return 0;
            if (c->kind == RECU_KIND_CARVED || c->validation_checked) {
                val = ensure_validation(c);
                return val != RECU_VALIDATION_DAMAGED;
            }
            return 1;
        default:
            return 1;
    }
}

static int read_contiguous_candidate_bytes(const RecuCandidate *c, uint8_t **buf, size_t *size, RecuError *err) {
    *buf = NULL;
    *size = 0;
    if (!c || c->size == 0 || c->size > 64ull * 1024ull * 1024ull) return 0;
    if (c->offset > g.src.size || c->size > g.src.size - c->offset) return 0;
    uint8_t *data = (uint8_t *)malloc((size_t)c->size);
    if (!data) {
        recu_error_set(err, "out of memory while loading image preview");
        return 0;
    }
    if (!recu_source_read(&g.src, c->offset, data, (size_t)c->size, err)) {
        free(data);
        return 0;
    }
    *buf = data;
    *size = (size_t)c->size;
    return 1;
}

static HBITMAP create_scaled_hbitmap(GpBitmap *bitmap, int box_w, int box_h) {
    if (!bitmap || box_w <= 0 || box_h <= 0) return NULL;
    UINT src_w = 0, src_h = 0;
    if (GdipGetImageWidth((GpImage *)bitmap, &src_w) != Ok || GdipGetImageHeight((GpImage *)bitmap, &src_h) != Ok) {
        return NULL;
    }
    if (src_w == 0 || src_h == 0) return NULL;
    int pad = 10;
    int max_w = box_w - pad * 2;
    int max_h = box_h - pad * 2;
    if (max_w <= 0 || max_h <= 0) return NULL;
    double scale_x = (double)max_w / (double)src_w;
    double scale_y = (double)max_h / (double)src_h;
    double scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale > 1.0) scale = 1.0;
    int dst_w = (int)(src_w * scale);
    int dst_h = (int)(src_h * scale);
    if (dst_w < 1) dst_w = 1;
    if (dst_h < 1) dst_h = 1;
    int dst_x = (box_w - dst_w) / 2;
    int dst_y = (box_h - dst_h) / 2;

    HDC screen = GetDC(g.image_preview);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP hbmp = CreateCompatibleBitmap(screen, box_w, box_h);
    HGDIOBJ old = SelectObject(mem, hbmp);
    HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
    RECT rc = {0, 0, box_w, box_h};
    FillRect(mem, &rc, brush);
    DeleteObject(brush);
    GpGraphics *graphics = NULL;
    if (GdipCreateFromHDC(mem, &graphics) == Ok) {
        GdipSetInterpolationMode(graphics, InterpolationModeHighQualityBicubic);
        GdipDrawImageRectI(graphics, (GpImage *)bitmap, dst_x, dst_y, dst_w, dst_h);
        GdipDeleteGraphics(graphics);
    }
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(g.image_preview, screen);
    return hbmp;
}

static int rotate_flip_for_orientation(uint16_t orientation, RotateFlipType *out) {
    if (!out) return 0;
    switch (orientation) {
        case 2:
            *out = RotateNoneFlipX;
            return 1;
        case 3:
            *out = Rotate180FlipNone;
            return 1;
        case 4:
            *out = Rotate180FlipX;
            return 1;
        case 5:
            *out = Rotate90FlipX;
            return 1;
        case 6:
            *out = Rotate90FlipNone;
            return 1;
        case 7:
            *out = Rotate270FlipX;
            return 1;
        case 8:
            *out = Rotate270FlipNone;
            return 1;
        default:
            return 0;
    }
}

static HBITMAP create_blank_preview_bitmap(void) {
    if (!g.image_preview) return NULL;
    RECT rc;
    GetClientRect(g.image_preview, &rc);
    int box_w = rc.right - rc.left;
    int box_h = rc.bottom - rc.top;
    if (box_w <= 0) box_w = 1;
    if (box_h <= 0) box_h = 1;

    HDC screen = GetDC(g.image_preview);
    if (!screen) return NULL;
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP hbmp = mem ? CreateCompatibleBitmap(screen, box_w, box_h) : NULL;
    if (!mem || !hbmp) {
        if (mem) DeleteDC(mem);
        ReleaseDC(g.image_preview, screen);
        return NULL;
    }

    HGDIOBJ old = SelectObject(mem, hbmp);
    HBRUSH brush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    RECT fill = {0, 0, box_w, box_h};
    FillRect(mem, &fill, brush);
    DeleteObject(brush);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(g.image_preview, screen);
    return hbmp;
}

static void set_image_preview_bitmap(HBITMAP bitmap) {
    if (!g.image_preview) {
        if (bitmap) DeleteObject(bitmap);
        return;
    }

    HBITMAP old = (HBITMAP)SendMessageW(g.image_preview, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)bitmap);
    if (old && old != g.preview_bitmap && old != bitmap) {
        DeleteObject(old);
    }
    if (g.preview_bitmap && g.preview_bitmap != bitmap) {
        DeleteObject(g.preview_bitmap);
    }
    g.preview_bitmap = bitmap;
    RedrawWindow(g.image_preview, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

static void clear_image_preview(void) {
    set_image_preview_bitmap(NULL);
    HBITMAP blank = create_blank_preview_bitmap();
    set_image_preview_bitmap(blank);
}

static void show_image_preview(const RecuCandidate *c) {
    clear_image_preview();
    if (!g.source_open || !c || !candidate_is_photo(c)) return;
    RecuError err;
    recu_error_clear(&err);
    uint8_t *data = NULL;
    size_t size = 0;
    if (!read_contiguous_candidate_bytes(c, &data, &size, &err)) return;

    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hmem) {
        free(data);
        return;
    }
    void *dst = GlobalLock(hmem);
    if (!dst) {
        GlobalFree(hmem);
        free(data);
        return;
    }
    memcpy(dst, data, size);
    GlobalUnlock(hmem);
    free(data);

    IStream *stream = NULL;
    if (CreateStreamOnHGlobal(hmem, TRUE, &stream) != S_OK) {
        GlobalFree(hmem);
        return;
    }
    GpBitmap *bitmap = NULL;
    if (GdipCreateBitmapFromStream(stream, &bitmap) == Ok) {
        RotateFlipType rotate_flip;
        if (rotate_flip_for_orientation(c->photo_orientation, &rotate_flip)) {
            GdipImageRotateFlip((GpImage *)bitmap, rotate_flip);
        }
        RECT rc;
        GetClientRect(g.image_preview, &rc);
        HBITMAP scaled = create_scaled_hbitmap(bitmap, rc.right - rc.left, rc.bottom - rc.top);
        if (scaled) set_image_preview_bitmap(scaled);
        GdipDisposeImage((GpImage *)bitmap);
    }
    stream->lpVtbl->Release(stream);
}

static int text_contains_ci(const char *hay, const char *needle) {
    if (!needle || !*needle) return 1;
    if (!hay) return 0;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return 1;
    }
    return 0;
}

static void add_column(HWND list, int index, int width, const char *title) {
    wchar_t wtitle[64];
    utf8_to_wide_buf(title, wtitle, (int)(sizeof(wtitle) / sizeof(wtitle[0])));
    LVCOLUMNW col;
    memset(&col, 0, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = wtitle;
    col.cx = width;
    col.iSubItem = index;
    SendMessageW(list, LVM_INSERTCOLUMNW, (WPARAM)index, (LPARAM)&col);
}

static void init_list_columns(HWND list) {
    while (ListView_DeleteColumn(list, 0)) {
    }
    add_column(list, 0, 50, tr(UI_COL_ID));
    add_column(list, 1, 90, tr(UI_COL_KIND));
    add_column(list, 2, 70, tr(UI_COL_CONF));
    add_column(list, 3, 95, tr(UI_COL_SIZE));
    add_column(list, 4, 85, tr(UI_COL_FORMAT));
    add_column(list, 5, 90, tr(UI_COL_VALIDATION));
    add_column(list, 6, 300, tr(UI_COL_NAME));
    add_column(list, 7, 420, tr(UI_COL_NOTE));
}

static void resize_list_columns(HWND list, int list_w) {
    if (!list || list_w <= 0) return;
    const int id_w = 50;
    const int kind_w = 90;
    const int conf_w = 70;
    const int size_w = 100;
    const int fmt_w = 90;
    const int valid_w = 95;
    const int chrome_w = 28;
    int fixed = id_w + kind_w + conf_w + size_w + fmt_w + valid_w + chrome_w;
    int remaining = list_w - fixed;
    int name_w = 300;
    int note_w = 420;
    if (remaining > name_w + note_w) {
        name_w = remaining * 45 / 100;
        note_w = remaining - name_w;
        if (name_w < 300) name_w = 300;
        if (note_w < 420) note_w = 420;
    }
    ListView_SetColumnWidth(list, 0, id_w);
    ListView_SetColumnWidth(list, 1, kind_w);
    ListView_SetColumnWidth(list, 2, conf_w);
    ListView_SetColumnWidth(list, 3, size_w);
    ListView_SetColumnWidth(list, 4, fmt_w);
    ListView_SetColumnWidth(list, 5, valid_w);
    ListView_SetColumnWidth(list, 6, name_w);
    ListView_SetColumnWidth(list, 7, note_w);
}

static void set_subitem(HWND list, int row, int col, const char *text) {
    wchar_t wtext[RECU_MAX_PATH];
    utf8_to_wide_buf(text, wtext, (int)(sizeof(wtext) / sizeof(wtext[0])));
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = col;
    item.pszText = wtext;
    SendMessageW(list, LVM_SETITEMW, 0, (LPARAM)&item);
}

static void refresh_result_row(int row, RecuCandidate *c) {
    if (row < 0 || !c) return;
    char conf[32];
    char size[64];
    snprintf(conf, sizeof(conf), "%d%%", c->confidence);
    format_size(c->size, size, sizeof(size));
    RecuValidationStatus val = c->validation_checked ? c->validation : RECU_VALIDATION_UNKNOWN;
    set_subitem(g.list, row, 2, conf);
    set_subitem(g.list, row, 3, size);
    set_subitem(g.list, row, 5, local_validation(val));
    set_subitem(g.list, row, 7, local_note(c));
    ListView_RedrawItems(g.list, row, row);
}

static void sync_checked_from_list(void) {
    if (!g.checked || !g.has_report) return;
    int rows = ListView_GetItemCount(g.list);
    for (int row = 0; row < rows; row++) {
        LVITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_PARAM;
        item.iItem = row;
        if (SendMessageW(g.list, LVM_GETITEMW, 0, (LPARAM)&item)) {
            size_t idx = (size_t)item.lParam;
            if (idx < g.checked_count) {
                g.checked[idx] = (uint8_t)ListView_GetCheckState(g.list, row);
            }
        }
    }
}

static int ci_compare_text(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *b) {
        int ca = tolower((unsigned char)*a++);
        int cb = tolower((unsigned char)*b++);
        if (ca != cb) return ca - cb;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int validation_sort_value(const RecuCandidate *c) {
    if (!c || !c->validation_checked) return RECU_VALIDATION_UNKNOWN;
    return c->validation;
}

static int compare_candidates_for_sort(const RecuCandidate *a, const RecuCandidate *b) {
    if (!a || !b) return 0;
    switch (g.sort_col) {
        case 0:
            return (a->id > b->id) - (a->id < b->id);
        case 1:
            return ci_compare_text(local_kind(a), local_kind(b));
        case 2:
            return a->confidence - b->confidence;
        case 3:
            return (a->size > b->size) - (a->size < b->size);
        case 4:
            return ci_compare_text(local_format(a->format), local_format(b->format));
        case 5:
            return validation_sort_value(a) - validation_sort_value(b);
        case 6:
            return ci_compare_text(a->path, b->path);
        case 7:
            return ci_compare_text(local_note(a), local_note(b));
        default:
            return (a->id > b->id) - (a->id < b->id);
    }
}

static int compare_candidate_indices(const void *pa, const void *pb) {
    size_t ia = *(const size_t *)pa;
    size_t ib = *(const size_t *)pb;
    if (ia >= g.report.candidates.count || ib >= g.report.candidates.count) return 0;
    const RecuCandidate *a = &g.report.candidates.items[ia];
    const RecuCandidate *b = &g.report.candidates.items[ib];
    int cmp = compare_candidates_for_sort(a, b);
    if (cmp == 0) cmp = (a->id > b->id) - (a->id < b->id);
    return g.sort_desc ? -cmp : cmp;
}

static int result_matches_current_filters(RecuCandidate *c, const char *filter, int hide_zero) {
    if (!c) return 0;
    if (hide_zero && c->size == 0) return 0;
    if (!category_matches(c)) return 0;
    if (!validation_filter_matches(c)) return 0;
    if (!text_contains_ci(c->name, filter) && !text_contains_ci(c->path, filter) &&
        !text_contains_ci(recu_format_name(c->format), filter) && !text_contains_ci(c->extension, filter)) {
        return 0;
    }
    return 1;
}

static void insert_result_row(int row, size_t i) {
    if (i >= g.report.candidates.count) return;
    RecuCandidate *c = &g.report.candidates.items[i];
    RecuValidationStatus val = c->validation_checked ? c->validation : RECU_VALIDATION_UNKNOWN;
    char id[32], conf[32], size[64];
    snprintf(id, sizeof(id), "%u", c->id);
    snprintf(conf, sizeof(conf), "%d%%", c->confidence);
    format_size(c->size, size, sizeof(size));

    wchar_t wid[32];
    utf8_to_wide_buf(id, wid, (int)(sizeof(wid) / sizeof(wid[0])));
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = row;
    item.iSubItem = 0;
    item.pszText = wid;
    item.lParam = (LPARAM)i;
    SendMessageW(g.list, LVM_INSERTITEMW, 0, (LPARAM)&item);
    set_subitem(g.list, row, 1, local_kind(c));
    set_subitem(g.list, row, 2, conf);
    set_subitem(g.list, row, 3, size);
    set_subitem(g.list, row, 4, local_format(c->format));
    set_subitem(g.list, row, 5, local_validation(val));
    set_subitem(g.list, row, 6, c->path);
    set_subitem(g.list, row, 7, local_note(c));
    if (g.checked && i < g.checked_count) {
        ListView_SetCheckState(g.list, row, g.checked[i] ? TRUE : FALSE);
    }
}

static void populate_results(void) {
    if (g.suppress_check_sync_once) {
        g.suppress_check_sync_once = 0;
    } else {
        sync_checked_from_list();
    }
    ListView_DeleteAllItems(g.list);
    SetWindowTextW(g.preview, L"");
    clear_image_preview();
    if (!g.has_report) {
        update_output_buttons();
        return;
    }
    char filter[256];
    get_window_text_utf8(g.filter, filter, sizeof(filter));
    int hide_zero = g.hide_zero && SendMessageW(g.hide_zero, BM_GETCHECK, 0, 0) == BST_CHECKED;
    int row = 0;
    size_t *order = g.report.candidates.count ? (size_t *)malloc(g.report.candidates.count * sizeof(size_t)) : NULL;
    size_t order_count = 0;
    if (order) {
        for (size_t i = 0; i < g.report.candidates.count; i++) {
            RecuCandidate *c = &g.report.candidates.items[i];
            if (result_matches_current_filters(c, filter, hide_zero)) order[order_count++] = i;
        }
        if (g.sort_col >= 0 && order_count > 1) qsort(order, order_count, sizeof(size_t), compare_candidate_indices);
        for (size_t oi = 0; oi < order_count; oi++) {
            insert_result_row(row++, order[oi]);
        }
        free(order);
    } else {
        for (size_t i = 0; i < g.report.candidates.count; i++) {
            RecuCandidate *c = &g.report.candidates.items[i];
            if (!result_matches_current_filters(c, filter, hide_zero)) continue;
            insert_result_row(row++, i);
        }
    }
    char msg[256];
    snprintf(msg, sizeof(msg), tr(UI_VISIBLE_STATUS),
             g.report.candidates.count, row, recu_fs_name(g.report.volume.fs_type), g.report.volume.cluster_size);
    set_status(msg);
    update_output_buttons();
}

static int selected_list_row(void) {
    return ListView_GetNextItem(g.list, -1, LVNI_SELECTED);
}

static int selected_candidate_index(void) {
    int row = selected_list_row();
    if (row < 0) return -1;
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if (!SendMessageW(g.list, LVM_GETITEMW, 0, (LPARAM)&item)) return -1;
    return (int)item.lParam;
}

static const char *preview_body_start(const char *raw) {
    const char *body = strstr(raw, "\r\n\r\n");
    return body ? body + 4 : "";
}

static void append_local_preview_body(char *dst, size_t dst_size, const char *body) {
    if (!body || !*body) return;
    if (strncmp(body, "Photo metadata:\r\n", 17) == 0) {
        const char *after_meta = strstr(body, "\r\n\r\n");
        if (!after_meta) return;
        body = after_meta + 4;
        if (!*body) return;
    }
    if (strncmp(body, "Hex preview:\r\n", 14) == 0) {
        recu_safe_append(dst, dst_size, tr(UI_PREVIEW_HEX));
        recu_safe_append(dst, dst_size, ":\r\n");
        recu_safe_append(dst, dst_size, body + 14);
    } else if (strncmp(body, "Text preview:\r\n", 15) == 0) {
        recu_safe_append(dst, dst_size, tr(UI_PREVIEW_TEXT));
        recu_safe_append(dst, dst_size, ":\r\n");
        recu_safe_append(dst, dst_size, body + 15);
    } else if (strncmp(body, "(empty file)", 12) == 0) {
        recu_safe_append(dst, dst_size, tr(UI_PREVIEW_EMPTY));
    } else {
        recu_safe_append(dst, dst_size, body);
    }
}

static void append_local_photo_metadata(char *dst, size_t dst_size, const RecuCandidate *c) {
    if (!c || !c->photo_metadata_present) return;
    recu_safe_append(dst, dst_size, g.lang == GUI_LANG_RU ? "Метаданные фото:\r\n" : "Photo metadata:\r\n");
    if (c->photo_datetime[0]) {
        recu_safe_append(dst, dst_size, g.lang == GUI_LANG_RU ? "  Дата съемки: " : "  Date taken: ");
        recu_safe_append(dst, dst_size, c->photo_datetime);
        recu_safe_append(dst, dst_size, "\r\n");
    }
    if (c->photo_make[0] || c->photo_model[0]) {
        recu_safe_append(dst, dst_size, g.lang == GUI_LANG_RU ? "  Камера: " : "  Camera: ");
        recu_safe_append(dst, dst_size, c->photo_make);
        if (c->photo_make[0] && c->photo_model[0]) recu_safe_append(dst, dst_size, " ");
        recu_safe_append(dst, dst_size, c->photo_model);
        recu_safe_append(dst, dst_size, "\r\n");
    }
    if (c->photo_width || c->photo_height) {
        char line[96];
        snprintf(line, sizeof(line), g.lang == GUI_LANG_RU ? "  Размер: %u x %u\r\n" : "  Image size: %u x %u\r\n",
                 c->photo_width, c->photo_height);
        recu_safe_append(dst, dst_size, line);
    }
    if (c->photo_orientation) {
        char line[64];
        snprintf(line, sizeof(line), g.lang == GUI_LANG_RU ? "  Ориентация: %u\r\n" : "  Orientation: %u\r\n",
                 c->photo_orientation);
        recu_safe_append(dst, dst_size, line);
    }
    if (c->photo_gps[0]) {
        recu_safe_append(dst, dst_size, "  GPS: ");
        recu_safe_append(dst, dst_size, c->photo_gps);
        recu_safe_append(dst, dst_size, "\r\n");
    }
    if (c->photo_software[0]) {
        recu_safe_append(dst, dst_size, g.lang == GUI_LANG_RU ? "  ПО: " : "  Software: ");
        recu_safe_append(dst, dst_size, c->photo_software);
        recu_safe_append(dst, dst_size, "\r\n");
    }
    recu_safe_append(dst, dst_size, "\r\n");
}

static void build_local_preview(const RecuCandidate *c, const char *raw, char *out, size_t out_size) {
    snprintf(out, out_size,
             "%s: %s\r\n%s: %s\r\n%s: %s\r\n%s: %s\r\n%s: %llu %s\r\n%s: %llu\r\n%s: %u\r\n%s: %d%%\r\n",
             tr(UI_PREVIEW_NAME), c->name,
             tr(UI_PREVIEW_KIND), local_kind(c),
             tr(UI_PREVIEW_FS), recu_fs_name(c->fs_type),
             tr(UI_PREVIEW_FORMAT), local_format(c->format),
             tr(UI_PREVIEW_SIZE), (unsigned long long)c->size, tr(UI_PREVIEW_BYTES),
             tr(UI_PREVIEW_OFFSET), (unsigned long long)c->offset,
             tr(UI_PREVIEW_FIRST_CLUSTER), c->first_cluster,
             tr(UI_PREVIEW_CONFIDENCE), c->confidence);
    append_local_confidence_reasons(out, out_size, c);
    recu_safe_append(out, out_size, tr(UI_PREVIEW_NOTE));
    recu_safe_append(out, out_size, ": ");
    recu_safe_append(out, out_size, local_note(c));
    recu_safe_append(out, out_size, "\r\n\r\n");
    append_local_photo_metadata(out, out_size, c);
    append_local_preview_body(out, out_size, preview_body_start(raw));
}

static void append_recovery_status_preview(char *out, size_t out_size, size_t idx) {
    if (!g.recovered_paths || idx >= g.checked_count || !g.recovered_paths[idx][0]) return;
    recu_safe_append(out, out_size, "\r\n\r\n");
    recu_safe_append(out, out_size, g.lang == GUI_LANG_RU ? "Восстановленный файл:\r\n" : "Recovered file:\r\n");
    recu_safe_append(out, out_size, g.recovered_paths[idx]);
    recu_safe_append(out, out_size, "\r\n");
    if (g.post_checked && g.post_size_ok && g.post_validation && g.post_written_size) {
        PostCheckResult post;
        memset(&post, 0, sizeof(post));
        post.checked = g.post_checked[idx];
        post.size_ok = g.post_size_ok[idx];
        post.validation = g.post_validation[idx];
        post.written_size = g.post_written_size[idx];
        append_post_check_line(out, out_size, &post);
    }
}

static void update_preview(void) {
    int row = selected_list_row();
    int idx = selected_candidate_index();
    if (idx < 0 || !g.has_report || !g.source_open) {
        SetWindowTextW(g.preview, L"");
        clear_image_preview();
        update_output_buttons();
        return;
    }
    if ((size_t)idx >= g.report.candidates.count) {
        SetWindowTextW(g.preview, L"");
        clear_image_preview();
        update_output_buttons();
        return;
    }
    char *text = (char *)malloc(RECU_MAX_PREVIEW + 8192);
    if (!text) return;
    RecuError err;
    recu_error_clear(&err);
    RecuCandidate *c = &g.report.candidates.items[idx];
    ensure_validation(c);
    refresh_result_row(row, c);
    show_image_preview(c);
    if (recu_preview_candidate(&g.src, &g.report.volume, c, text, RECU_MAX_PREVIEW + 8192, &err)) {
        char *localized = (char *)malloc(RECU_MAX_PREVIEW + 8192);
        if (localized) {
            build_local_preview(c, text, localized, RECU_MAX_PREVIEW + 8192);
            append_recovery_status_preview(localized, RECU_MAX_PREVIEW + 8192, (size_t)idx);
            set_window_text_utf8(g.preview, localized);
            update_output_buttons();
            free(localized);
            free(text);
            return;
        }
    } else {
        char friendly[RECU_MAX_ERR + 1024];
        localize_error_message(err.message, friendly, sizeof(friendly));
        snprintf(text, RECU_MAX_PREVIEW + 8192, tr(UI_PREVIEW_FAILED), friendly);
    }
    set_window_text_utf8(g.preview, text);
    update_output_buttons();
    free(text);
}

static void browse_source(void) {
    wchar_t path[RECU_MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = g.lang == GUI_LANG_RU
        ? L"Образы (*.img;*.dd;*.bin)\0*.img;*.dd;*.bin\0Все файлы\0*.*\0"
        : L"Images (*.img;*.dd;*.bin)\0*.img;*.dd;*.bin\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = (DWORD)(sizeof(path) / sizeof(path[0]));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(g.source, path);
    }
}

static int choose_folder(char *out, size_t out_size) {
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = g.hwnd;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    wchar_t title[128];
    utf8_to_wide_buf(tr(UI_CHOOSE_OUTPUT_FOLDER), title, (int)(sizeof(title) / sizeof(title[0])));
    bi.lpszTitle = title;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return 0;
    wchar_t wout[RECU_MAX_PATH];
    BOOL ok = SHGetPathFromIDListW(pidl, wout);
    CoTaskMemFree(pidl);
    if (!ok) return 0;
    wide_to_utf8_buf(wout, out, (int)out_size);
    return 1;
}

static void show_last_recovery_output(void) {
    const char *selected_path = selected_recovered_path();
    const char *path = selected_path ? selected_path : g.last_recovery_path;
    int is_file = selected_path ? 1 : g.last_recovery_is_file;
    if (!path || !*path) return;
    wchar_t wpath[RECU_MAX_PATH];
    utf8_to_wide_buf(path, wpath, (int)(sizeof(wpath) / sizeof(wpath[0])));
    HINSTANCE result;
    if (is_file) {
        wchar_t args[RECU_MAX_PATH + 32];
        swprintf(args, sizeof(args) / sizeof(args[0]), L"/select,\"%ls\"", wpath);
        result = ShellExecuteW(g.hwnd, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
    } else {
        result = ShellExecuteW(g.hwnd, L"open", wpath, NULL, NULL, SW_SHOWNORMAL);
    }
    if ((INT_PTR)result <= 32) {
        msgbox_utf8(path, g.lang == GUI_LANG_RU ? "Не удалось открыть проводник" : "Explorer failed", MB_ICONERROR);
    }
}

static const char *selected_recovered_path(void) {
    int idx = selected_candidate_index();
    if (idx >= 0 && g.recovered_paths && (size_t)idx < g.checked_count && g.recovered_paths[idx][0]) {
        return g.recovered_paths[idx];
    }
    return NULL;
}

static void update_output_buttons(void) {
    const char *selected_path = selected_recovered_path();
    int enabled = !g.scan_running;
    EnableWindow(GetDlgItem(g.hwnd, IDC_SHOW_OUTPUT), enabled && (g.last_recovery_path[0] != 0 || selected_path != NULL));
    EnableWindow(GetDlgItem(g.hwnd, IDC_OPEN_OUTPUT), enabled && (selected_path != NULL || (g.last_recovery_is_file && g.last_recovery_path[0] != 0)));
}

static void open_selected_recovery_output(void) {
    const char *path = selected_recovered_path();
    if (!path && g.last_recovery_is_file) path = g.last_recovery_path;
    if (!path || !*path) return;
    wchar_t wpath[RECU_MAX_PATH];
    utf8_to_wide_buf(path, wpath, (int)(sizeof(wpath) / sizeof(wpath[0])));
    HINSTANCE result = ShellExecuteW(g.hwnd, L"open", wpath, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        msgbox_utf8(path, g.lang == GUI_LANG_RU ? "Не удалось открыть файл" : "Open failed", MB_ICONERROR);
    }
}

static int save_file_dialog_suggested(const char *filter, const char *def_ext, const char *suggested_name, char *out, size_t out_size) {
    wchar_t wout[RECU_MAX_PATH] = L"";
    wchar_t wdef[32];
    wchar_t wfilter[512];
    if (suggested_name && *suggested_name) {
        utf8_to_wide_buf(suggested_name, wout, (int)(sizeof(wout) / sizeof(wout[0])));
    }
    utf8_to_wide_buf(def_ext, wdef, (int)(sizeof(wdef) / sizeof(wdef[0])));
    utf8_multistring_to_wide_buf(filter, wfilter, (int)(sizeof(wfilter) / sizeof(wfilter[0])));
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = g.lang == GUI_LANG_RU
        ? L"Отчеты и образы (*.csv;*.img;*.dd)\0*.csv;*.img;*.dd\0Все файлы\0*.*\0"
        : L"CSV report or disk image\0*.csv;*.img;*.dd\0All files\0*.*\0";
    ofn.lpstrFilter = wfilter;
    ofn.lpstrFile = wout;
    ofn.nMaxFile = (DWORD)(sizeof(wout) / sizeof(wout[0]));
    ofn.lpstrDefExt = wdef;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return 0;
    wide_to_utf8_buf(wout, out, (int)out_size);
    return out[0] != 0;
}

static int save_file_dialog(const char *filter, const char *def_ext, char *out, size_t out_size) {
    return save_file_dialog_suggested(filter, def_ext, NULL, out, out_size);
}

static const char *path_extension(const char *path) {
    const char *slash = strrchr(path, '\\');
    const char *slash2 = strrchr(path, '/');
    if (!slash || (slash2 && slash2 > slash)) slash = slash2;
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    return dot ? dot + 1 : NULL;
}

static int ext_eq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int has_image_extension(const char *path) {
    const char *ext = path_extension(path);
    return ext_eq_ci(ext, "img") || ext_eq_ci(ext, "dd");
}

static void force_extension(char *path, size_t path_size, const char *ext) {
    const char *slash = strrchr(path, '\\');
    const char *slash2 = strrchr(path, '/');
    if (!slash || (slash2 && slash2 > slash)) slash = slash2;
    char *base = slash ? (char *)slash + 1 : path;
    char *dot = strrchr(base, '.');
    if (dot) *dot = 0;
    recu_safe_append(path, path_size, ".");
    recu_safe_append(path, path_size, ext);
}

static int drive_letter_from_path(const char *path) {
    if (!path || !*path) return 0;
    if (strncmp(path, "\\\\.\\", 4) == 0 && path[4] && path[5] == ':') {
        return toupper((unsigned char)path[4]);
    }
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':') {
        return toupper((unsigned char)path[0]);
    }
    return 0;
}

static int recovery_target_is_source_drive(const char *folder) {
    if (!g.source_open || !g.src.is_raw_device) return 0;
    int src_drive = drive_letter_from_path(g.src.path);
    int out_drive = drive_letter_from_path(folder);
    return src_drive && out_drive && src_drive == out_drive;
}

static int output_path_is_source_drive(const char *source_path, const char *output_path) {
    int src_drive = drive_letter_from_path(source_path);
    int out_drive = drive_letter_from_path(output_path);
    return src_drive && out_drive && src_drive == out_drive;
}

static FILE *gui_fopen_write_utf8(const char *path) {
    wchar_t wpath[RECU_MAX_PATH];
    utf8_to_wide_buf(path, wpath, (int)(sizeof(wpath) / sizeof(wpath[0])));
    return _wfopen(wpath, L"wb");
}

static int file_exists_utf8(const char *path) {
    wchar_t wpath[RECU_MAX_PATH];
    utf8_to_wide_buf(path, wpath, (int)(sizeof(wpath) / sizeof(wpath[0])));
    DWORD attrs = GetFileAttributesW(wpath);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static uint64_t file_size_utf8(const char *path) {
    wchar_t wpath[RECU_MAX_PATH];
    utf8_to_wide_buf(path, wpath, (int)(sizeof(wpath) / sizeof(wpath[0])));
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &data)) return 0;
    return ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
}

static const char *post_validation_label(RecuValidationStatus validation, int checked) {
    if (!checked) return g.lang == GUI_LANG_RU ? "не проверено" : "not checked";
    return local_validation(validation);
}

static void post_check_recovered_file(const RecuCandidate *source_candidate, const char *path, PostCheckResult *out) {
    memset(out, 0, sizeof(*out));
    out->validation = RECU_VALIDATION_UNKNOWN;
    if (!source_candidate || !path || !*path) {
        recu_safe_copy(out->message, sizeof(out->message), "no output path");
        return;
    }
    out->written_size = file_size_utf8(path);
    out->size_ok = out->written_size == source_candidate->size;

    RecuSource restored;
    RecuError err;
    recu_error_clear(&err);
    if (!recu_source_open(&restored, path, &err)) {
        recu_safe_copy(out->message, sizeof(out->message), "post-check open failed: ");
        recu_safe_append(out->message, sizeof(out->message), err.message);
        return;
    }
    RecuCandidate c = *source_candidate;
    c.offset = 0;
    c.size = out->written_size;
    c.first_cluster = 0;
    c.cluster_count = 0;
    c.fs_type = RECU_FS_UNKNOWN;
    c.recoverable = 1;
    c.validation_checked = 0;
    out->validation = recu_validate_candidate(&restored, NULL, &c, &err);
    out->checked = 1;
    recu_source_close(&restored);

    snprintf(out->message, sizeof(out->message), "size=%s; validation=%s",
             out->size_ok ? "ok" : "mismatch", recu_validation_name(out->validation));
}

static void remember_recovered_candidate(size_t idx, const char *path, const PostCheckResult *check) {
    if (g.recovered_paths && idx < g.checked_count) {
        recu_safe_copy(g.recovered_paths[idx], RECU_MAX_PATH, path ? path : "");
    }
    if (check && idx < g.checked_count) {
        if (g.post_checked) g.post_checked[idx] = (uint8_t)check->checked;
        if (g.post_size_ok) g.post_size_ok[idx] = (uint8_t)check->size_ok;
        if (g.post_validation) g.post_validation[idx] = check->validation;
        if (g.post_written_size) g.post_written_size[idx] = check->written_size;
    }
}

static void append_post_check_line(char *dst, size_t dst_size, const PostCheckResult *check) {
    if (!check) return;
    char written[64];
    format_size(check->written_size, written, sizeof(written));
    char line[256];
    snprintf(line, sizeof(line),
             g.lang == GUI_LANG_RU
                 ? "Пост-проверка: размер %s (%s), валидатор: %s\r\n"
                 : "Post-check: size %s (%s), validator: %s\r\n",
             check->size_ok ? (g.lang == GUI_LANG_RU ? "совпал" : "ok") : (g.lang == GUI_LANG_RU ? "не совпал" : "mismatch"),
             written,
             post_validation_label(check->validation, check->checked));
    recu_safe_append(dst, dst_size, line);
}

static void count_post_check_result(const PostCheckResult *check, int *valid, int *partial, int *damaged, int *mismatch, int *unknown) {
    if (!check) {
        if (unknown) (*unknown)++;
        return;
    }
    if (!check->size_ok) {
        if (mismatch) (*mismatch)++;
    } else if (!check->checked) {
        if (unknown) (*unknown)++;
    } else if (check->validation == RECU_VALIDATION_VALID) {
        if (valid) (*valid)++;
    } else if (check->validation == RECU_VALIDATION_PARTIAL) {
        if (partial) (*partial)++;
    } else if (check->validation == RECU_VALIDATION_DAMAGED) {
        if (damaged) (*damaged)++;
    } else if (unknown) {
        (*unknown)++;
    }
}

static void current_timestamp(char *out, size_t out_size, int filename_safe) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char tmp[64];
    snprintf(tmp, sizeof(tmp),
             filename_safe ? "%04d-%02d-%02d_%02d-%02d-%02d" : "%04d-%02d-%02d %02d:%02d:%02d",
             (int)st.wYear, (int)st.wMonth, (int)st.wDay, (int)st.wHour, (int)st.wMinute, (int)st.wSecond);
    recu_safe_copy(out, out_size, tmp);
}

static void source_label(const char *source, char *out, size_t out_size) {
    int drive = drive_letter_from_path(source);
    if (drive) {
        snprintf(out, out_size, "%c_drive", drive);
        return;
    }
    const char *ext = path_extension(source);
    if (ext_eq_ci(ext, "img") || ext_eq_ci(ext, "dd")) {
        recu_safe_copy(out, out_size, "image");
        return;
    }
    recu_safe_copy(out, out_size, "unknown");
}

static void build_auto_artifact_name(const char *source, const char *kind, RecuReportFormat format, char *name, size_t name_size, int suffix) {
    char label[64], ts[32], ext[12];
    source_label(source, label, sizeof(label));
    current_timestamp(ts, sizeof(ts), 1);
    recu_safe_copy(ext, sizeof(ext), recu_report_format_extension(format));
    if (suffix > 0) snprintf(name, name_size, "RecuClassic_%s_%s_%s_%03d.%s", label, kind, ts, suffix, ext);
    else snprintf(name, name_size, "RecuClassic_%s_%s_%s.%s", label, kind, ts, ext);
}

static void build_auto_artifact_path(const char *folder, const char *source, const char *kind, RecuReportFormat format, char *out, size_t out_size) {
    char name[192];
    build_auto_artifact_name(source, kind, format, name, sizeof(name), 0);
    recu_path_join(out, out_size, folder, name);
    for (int i = 1; i < 1000 && file_exists_utf8(out); i++) {
        build_auto_artifact_name(source, kind, format, name, sizeof(name), i);
        recu_path_join(out, out_size, folder, name);
    }
}

static void log_csv_escape(FILE *f, const char *s) {
    fputc('"', f);
    for (; s && *s; s++) {
        if (*s == '"') fputc('"', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

static void log_json_escape(FILE *f, const char *s) {
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

static const char *candidate_risk_label(const RecuCandidate *c) {
    if (!c) return "unknown";
    if (c->duplicate_of) return "duplicate";
    if (c->same_offset_as) return "same-offset";
    if (c->overlaps_with) return "overlap";
    if (c->validation_checked && c->validation == RECU_VALIDATION_DAMAGED) return "damaged-or-garbage";
    if (c->validation_checked && c->validation == RECU_VALIDATION_PARTIAL) return "partial-or-truncated";
    if (c->likely_overwritten) return "possibly-overwritten";
    if (c->confidence < 50) return "low-confidence";
    if (c->validation_checked && c->validation == RECU_VALIDATION_VALID) return "valid";
    return "unknown";
}

typedef struct RecoveryLog {
    FILE *f;
    RecuReportFormat format;
    char path[RECU_MAX_PATH];
    char source[RECU_MAX_PATH];
    char output_folder[RECU_MAX_PATH];
    char started_at[32];
    int selected;
    int row_count;
    int recovered;
    int failed;
    int skipped;
} RecoveryLog;

static int open_recovery_log(const char *folder, const char *source, RecuReportFormat format, int selected, RecoveryLog *log, RecuError *err) {
    memset(log, 0, sizeof(*log));
    log->format = format;
    log->selected = selected;
    recu_safe_copy(log->source, sizeof(log->source), source ? source : "");
    recu_safe_copy(log->output_folder, sizeof(log->output_folder), folder ? folder : "");
    current_timestamp(log->started_at, sizeof(log->started_at), 0);
    build_auto_artifact_path(folder, source, "recovery-log", format, log->path, sizeof(log->path));
    log->f = gui_fopen_write_utf8(log->path);
    if (!log->f) {
        recu_error_set(err, "cannot create '%s'", log->path);
        return 0;
    }
    if (format == RECU_REPORT_CSV) {
        fputs("\xEF\xBB\xBF", log->f);
        fprintf(log->f, "product,Recu Classic\nsource,");
        log_csv_escape(log->f, log->source);
        fprintf(log->f, "\noutput_folder,");
        log_csv_escape(log->f, log->output_folder);
        fprintf(log->f, "\nstarted_at,%s\nselected_count,%d\nformat,CSV\n\n", log->started_at, selected);
        fprintf(log->f, "operation_index,candidate_id,status,output_path,error_message,expected_size,written_size,post_size_ok,post_validation,format,validation,risk,confidence,confidence_reasons,duplicate_of,same_offset_as,overlaps_with,overlap_bytes,source_offset,first_cluster,candidate_name,candidate_path,note\n");
    } else if (format == RECU_REPORT_JSON) {
        fprintf(log->f, "{\n  \"product\": \"Recu Classic\",\n  \"source\": ");
        log_json_escape(log->f, log->source);
        fprintf(log->f, ",\n  \"output_folder\": ");
        log_json_escape(log->f, log->output_folder);
        fprintf(log->f, ",\n  \"started_at\": ");
        log_json_escape(log->f, log->started_at);
        fprintf(log->f, ",\n  \"selected_count\": %d,\n  \"items\": [\n", selected);
    } else {
        fprintf(log->f, "Recu Classic recovery log\n");
        fprintf(log->f, "Source: %s\n", log->source);
        fprintf(log->f, "Output folder: %s\n", log->output_folder);
        fprintf(log->f, "Started: %s\n", log->started_at);
        fprintf(log->f, "Selected: %d\n\n", selected);
    }
    return 1;
}

static void write_recovery_log_row(RecoveryLog *log, const RecuCandidate *c, const char *status, const char *output, const char *error_text, uint64_t written_size, const PostCheckResult *post) {
    if (!log || !log->f || !c) return;
    log->row_count++;
    if (status && strcmp(status, "recovered") == 0) log->recovered++;
    else if (status && strcmp(status, "skipped") == 0) log->skipped++;
    else log->failed++;

    if (log->format == RECU_REPORT_CSV) {
        fprintf(log->f, "%d,%u,", log->row_count, c->id);
        log_csv_escape(log->f, status ? status : "");
        fputc(',', log->f);
        log_csv_escape(log->f, output ? output : "");
        fputc(',', log->f);
        log_csv_escape(log->f, error_text ? error_text : "");
        fprintf(log->f, ",%llu,%llu,%s,%s,%s,%s,%s,%d,",
                (unsigned long long)c->size,
                (unsigned long long)written_size,
                post ? (post->size_ok ? "yes" : "no") : "",
                post ? post_validation_label(post->validation, post->checked) : "",
                recu_format_name(c->format),
                c->validation_checked ? recu_validation_name(c->validation) : "unchecked",
                candidate_risk_label(c),
                c->confidence);
        log_csv_escape(log->f, c->confidence_reasons);
        fprintf(log->f, ",%u,%u,%u,%llu,%llu,%u,",
                c->duplicate_of,
                c->same_offset_as,
                c->overlaps_with,
                (unsigned long long)c->overlap_bytes,
                (unsigned long long)c->offset,
                c->first_cluster);
        log_csv_escape(log->f, c->name);
        fputc(',', log->f);
        log_csv_escape(log->f, c->path);
        fputc(',', log->f);
        log_csv_escape(log->f, c->note);
        fputc('\n', log->f);
    } else if (log->format == RECU_REPORT_JSON) {
        if (log->row_count > 1) fputs(",\n", log->f);
        fprintf(log->f, "    {\"operation_index\":%d,\"candidate_id\":%u,\"status\":", log->row_count, c->id);
        log_json_escape(log->f, status ? status : "");
        fprintf(log->f, ",\"output_path\":");
        log_json_escape(log->f, output ? output : "");
        fprintf(log->f, ",\"error_message\":");
        log_json_escape(log->f, error_text ? error_text : "");
        fprintf(log->f, ",\"expected_size\":%llu,\"written_size\":%llu,\"post_size_ok\":",
                (unsigned long long)c->size, (unsigned long long)written_size);
        if (post) fputs(post->size_ok ? "true" : "false", log->f);
        else fputs("null", log->f);
        fprintf(log->f, ",\"post_validation\":");
        log_json_escape(log->f, post ? post_validation_label(post->validation, post->checked) : "");
        fprintf(log->f, ",\"format\":");
        log_json_escape(log->f, recu_format_name(c->format));
        fprintf(log->f, ",\"validation\":");
        log_json_escape(log->f, c->validation_checked ? recu_validation_name(c->validation) : "unchecked");
        fprintf(log->f, ",\"risk\":");
        log_json_escape(log->f, candidate_risk_label(c));
        fprintf(log->f, ",\"confidence\":%d,\"confidence_reasons\":", c->confidence);
        log_json_escape(log->f, c->confidence_reasons);
        fprintf(log->f, ",\"duplicate_of\":%u,\"same_offset_as\":%u,\"overlaps_with\":%u,\"overlap_bytes\":%llu,\"source_offset\":%llu,\"first_cluster\":%u,\"candidate_name\":",
                c->duplicate_of, c->same_offset_as, c->overlaps_with, (unsigned long long)c->overlap_bytes,
                (unsigned long long)c->offset, c->first_cluster);
        log_json_escape(log->f, c->name);
        fprintf(log->f, ",\"candidate_path\":");
        log_json_escape(log->f, c->path);
        fprintf(log->f, ",\"note\":");
        log_json_escape(log->f, c->note);
        fputc('}', log->f);
    } else {
        fprintf(log->f, "[%d] #%u %s %s expected=%llu written=%llu validation=%s risk=%s confidence=%d%%\n",
                log->row_count, c->id, status ? status : "", recu_format_name(c->format),
                (unsigned long long)c->size, (unsigned long long)written_size,
                c->validation_checked ? recu_validation_name(c->validation) : "unchecked",
                candidate_risk_label(c), c->confidence);
        if (post) fprintf(log->f, "    post-check: size=%s validation=%s\n",
                          post->size_ok ? "ok" : "mismatch",
                          post_validation_label(post->validation, post->checked));
        fprintf(log->f, "    output: %s\n", output ? output : "");
        if (error_text && *error_text) fprintf(log->f, "    error: %s\n", error_text);
        fprintf(log->f, "    source: offset=%llu first_cluster=%u\n", (unsigned long long)c->offset, c->first_cluster);
        fprintf(log->f, "    confidence reasons: %s\n", c->confidence_reasons);
        if (c->duplicate_of) fprintf(log->f, "    duplicate of: #%u\n", c->duplicate_of);
        else if (c->same_offset_as) fprintf(log->f, "    same offset as: #%u overlap=%llu bytes\n", c->same_offset_as, (unsigned long long)c->overlap_bytes);
        else if (c->overlaps_with) fprintf(log->f, "    overlaps with: #%u overlap=%llu bytes\n", c->overlaps_with, (unsigned long long)c->overlap_bytes);
        fprintf(log->f, "    candidate: %s\n    path: %s\n    note: %s\n\n", c->name, c->path, c->note);
    }
}

static void close_recovery_log(RecoveryLog *log) {
    if (!log || !log->f) return;
    char finished_at[32];
    current_timestamp(finished_at, sizeof(finished_at), 0);
    if (log->format == RECU_REPORT_CSV) {
        fprintf(log->f, "\nsummary\nfinished_at,%s\nrecovered_count,%d\nfailed_count,%d\nskipped_count,%d\n",
                finished_at, log->recovered, log->failed, log->skipped);
    } else if (log->format == RECU_REPORT_JSON) {
        fprintf(log->f, "\n  ],\n  \"summary\": {\"finished_at\": ");
        log_json_escape(log->f, finished_at);
        fprintf(log->f, ", \"recovered_count\": %d, \"failed_count\": %d, \"skipped_count\": %d}\n}\n",
                log->recovered, log->failed, log->skipped);
    } else {
        fprintf(log->f, "Finished: %s\nRecovered: %d\nFailed: %d\nSkipped: %d\n",
                finished_at, log->recovered, log->failed, log->skipped);
    }
    fclose(log->f);
    log->f = NULL;
}

static const char *report_filter_for_format(RecuReportFormat format) {
    switch (format) {
        case RECU_REPORT_JSON:
            return "JSON (*.json)\0*.json\0All files\0*.*\0";
        case RECU_REPORT_LOG:
            return "Text log (*.log)\0*.log\0All files\0*.*\0";
        case RECU_REPORT_CSV:
        default:
            return "CSV (*.csv)\0*.csv\0All files\0*.*\0";
    }
}

static void fill_scan_report_info(RecuScanReportInfo *info, char *created_at, size_t created_at_size, char *scan_mode, size_t scan_mode_size) {
    memset(info, 0, sizeof(*info));
    current_timestamp(created_at, created_at_size, 0);
    int deep = SendMessageW(g.deep, BM_GETCHECK, 0, 0) == BST_CHECKED;
    int safe = SendMessageW(GetDlgItem(g.hwnd, IDC_SAFE_MODE), BM_GETCHECK, 0, 0) == BST_CHECKED;
    snprintf(scan_mode, scan_mode_size, "%s%s", deep ? "quick+deep" : "quick", safe ? "+safe-image" : "");
    info->product_name = "Recu Classic";
    info->source_path = g.src.path;
    info->scan_mode = scan_mode;
    info->created_at = created_at;
    info->visible_count = g.list ? (size_t)ListView_GetItemCount(g.list) : (g.has_report ? g.report.candidates.count : 0);
}

static int write_scan_report_to_path(const char *path, RecuReportFormat format, RecuError *err) {
    char created_at[32], scan_mode[48];
    RecuScanReportInfo info;
    fill_scan_report_info(&info, created_at, sizeof(created_at), scan_mode, sizeof(scan_mode));
    return recu_write_scan_report(path, &g.report.volume, &g.report.candidates, &info, format, err);
}

static int write_auto_scan_report(const char *folder, RecuReportFormat format, char *path, size_t path_size, RecuError *err) {
    build_auto_artifact_path(folder, g.src.path, "scan-report", format, path, path_size);
    return write_scan_report_to_path(path, format, err);
}

#ifdef _WIN32
static int free_space_for_output_path(const char *path, uint64_t *free_bytes) {
    if (free_bytes) *free_bytes = 0;
    wchar_t wpath[RECU_MAX_PATH];
    utf8_to_wide_buf(path, wpath, (int)(sizeof(wpath) / sizeof(wpath[0])));
    wchar_t dir[RECU_MAX_PATH];
    wcsncpy(dir, wpath, sizeof(dir) / sizeof(dir[0]));
    dir[(sizeof(dir) / sizeof(dir[0])) - 1] = 0;
    wchar_t *slash = wcsrchr(dir, L'\\');
    wchar_t *slash2 = wcsrchr(dir, L'/');
    if (!slash || (slash2 && slash2 > slash)) slash = slash2;
    if (slash) {
        if (slash == dir || (slash == dir + 2 && dir[1] == L':')) slash[1] = 0;
        else *slash = 0;
    } else {
        wcscpy(dir, L".");
    }
    ULARGE_INTEGER avail;
    if (!GetDiskFreeSpaceExW(dir, &avail, NULL, NULL)) return 0;
    if (free_bytes) *free_bytes = (uint64_t)avail.QuadPart;
    return 1;
}
#endif

static void set_scan_running(int running) {
    g.scan_running = running;
    EnableWindow(GetDlgItem(g.hwnd, IDC_SCAN), !running);
    EnableWindow(GetDlgItem(g.hwnd, IDC_CANCEL), running);
    EnableWindow(GetDlgItem(g.hwnd, IDC_BROWSE), !running);
    EnableWindow(g.drives, !running);
    EnableWindow(g.deep, !running);
    EnableWindow(GetDlgItem(g.hwnd, IDC_SAFE_MODE), !running);
    EnableWindow(g.lang_combo, !running);
    EnableWindow(g.category_combo, !running);
    EnableWindow(g.validation_combo, !running);
    EnableWindow(g.preserve_paths, !running);
    EnableWindow(g.hide_zero, !running);
    EnableWindow(g.recovery_log, !running);
    EnableWindow(g.report_format_combo, !running);
    update_log_format_enabled();
    update_output_buttons();
    if (g.progress) {
        SendMessageW(g.progress, PBM_SETSTATE, running ? PBST_NORMAL : PBST_NORMAL, 0);
        if (running) SendMessageW(g.progress, PBM_SETPOS, 0, 0);
    }
}

static void do_scan(void) {
    if (g.scan_running) return;
    char path[RECU_MAX_PATH];
    get_window_text_utf8(g.source, path, sizeof(path));
    if (!path[0]) {
        msgbox_utf8(tr(UI_NO_SOURCE), "recu", MB_ICONINFORMATION);
        return;
    }
    int safe_mode = SendMessageW(GetDlgItem(g.hwnd, IDC_SAFE_MODE), BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (safe_mode) {
        RecuError safe_err;
        RecuSource safe_src;
        recu_error_clear(&safe_err);
        if (!recu_source_open(&safe_src, path, &safe_err)) {
            msgbox_error_utf8(safe_err.message, tr(UI_OPEN_FAILED), MB_ICONERROR);
            return;
        }
        if (safe_src.is_raw_device) {
            char image_path[RECU_MAX_PATH];
            const char *image_filter = g.lang == GUI_LANG_RU
                ? "Образы дисков (*.img;*.dd)\0*.img;*.dd\0Все файлы\0*.*\0"
                : "Disk images (*.img;*.dd)\0*.img;*.dd\0All files\0*.*\0";
            if (!save_file_dialog(image_filter, "img", image_path, sizeof(image_path))) {
                recu_source_close(&safe_src);
                return;
            }
            if (!has_image_extension(image_path)) force_extension(image_path, sizeof(image_path), "img");
            if (output_path_is_source_drive(safe_src.path, image_path)) {
                recu_source_close(&safe_src);
                msgbox_utf8(tr(UI_SAME_DRIVE_BLOCKED), tr(UI_IMAGE_FAILED), MB_ICONERROR);
                return;
            }
            uint64_t free_bytes = 0;
            if (free_space_for_output_path(image_path, &free_bytes) && free_bytes < safe_src.size) {
                char need[64], have[64], msg[512];
                format_size(safe_src.size, need, sizeof(need));
                format_size(free_bytes, have, sizeof(have));
                if (g.lang == GUI_LANG_RU) {
                    snprintf(msg, sizeof(msg), "Недостаточно свободного места для безопасного режима.\nНужно: %s\nСвободно: %s", need, have);
                } else {
                    snprintf(msg, sizeof(msg), "Not enough free space for safe mode.\nRequired: %s\nAvailable: %s", need, have);
                }
                recu_source_close(&safe_src);
                msgbox_utf8(msg, tr(UI_IMAGE_FAILED), MB_ICONERROR);
                return;
            }
            set_status(tr(UI_SAFE_CREATING_IMAGE));
            InterlockedExchange(&g.cancel_requested, 0);
            int image_ok = recu_create_image(&safe_src, image_path, gui_progress, NULL, &safe_err);
            recu_source_close(&safe_src);
            if (!image_ok) {
                msgbox_error_utf8(safe_err.message, tr(UI_IMAGE_FAILED), MB_ICONERROR);
                return;
            }
            recu_safe_copy(path, sizeof(path), image_path);
            set_window_text_utf8(g.source, path);
            set_status(tr(UI_SAFE_SCANNING_IMAGE));
        } else {
            recu_source_close(&safe_src);
            set_status(tr(UI_SAFE_SCANNING_IMAGE));
        }
    }
    if (g.has_report) {
        recu_scan_report_free(&g.report);
        g.has_report = 0;
    }
    free(g.checked);
    g.checked = NULL;
    g.checked_count = 0;
    clear_recovery_state();
    ListView_DeleteAllItems(g.list);
    SetWindowTextW(g.preview, L"");
    clear_image_preview();
    if (g.source_open) {
        recu_source_close(&g.src);
        g.source_open = 0;
    }
    ScanJob *job = (ScanJob *)calloc(1, sizeof(*job));
    if (!job) {
        msgbox_error_utf8("out of memory", tr(UI_SCAN_FAILED), MB_ICONERROR);
        return;
    }
    job->hwnd = g.hwnd;
    recu_safe_copy(job->path, sizeof(job->path), path);
    job->deep = SendMessageW(g.deep, BM_GETCHECK, 0, 0) == BST_CHECKED;
    job->cancel_requested = &g.cancel_requested;
    InterlockedExchange(&g.cancel_requested, 0);
    set_scan_running(1);
    set_status(tr(UI_SCANNING));
    set_progress_value(0, 100);
    g.scan_thread = CreateThread(NULL, 0, scan_thread_proc, job, 0, NULL);
    if (!g.scan_thread) {
        set_scan_running(0);
        free(job);
        msgbox_error_utf8("cannot start scan thread", tr(UI_SCAN_FAILED), MB_ICONERROR);
        return;
    }
}

static void do_cancel_scan(void) {
    if (!g.scan_running) return;
    InterlockedExchange(&g.cancel_requested, 1);
    set_status(tr(UI_SCAN_CANCELLED));
    SendMessageW(g.progress, PBM_SETSTATE, PBST_PAUSED, 0);
}

static void finish_scan_job(ScanJob *job) {
    if (!job) return;
    if (g.scan_thread) {
        CloseHandle(g.scan_thread);
        g.scan_thread = NULL;
    }
    set_scan_running(0);
    if (job->ok) {
        g.src = job->src;
        g.source_open = 1;
        g.report = job->report;
        g.has_report = 1;
        g.checked_count = g.report.candidates.count;
        g.checked = g.checked_count ? (uint8_t *)calloc(g.checked_count, 1) : NULL;
        if (!allocate_recovery_state(g.checked_count)) {
            msgbox_error_utf8("out of memory while tracking recovered files", tr(UI_SCAN_FAILED), MB_ICONWARNING);
        }
        populate_results();
        int visible = ListView_GetItemCount(g.list);
        char msg[256];
        snprintf(msg, sizeof(msg), tr(UI_SCAN_COMPLETE_STATUS),
                 g.report.candidates.count, visible, recu_fs_name(g.report.volume.fs_type), g.report.volume.cluster_size);
        set_status(msg);
        SendMessageW(g.progress, PBM_SETPOS, 0, 0);
    } else {
        recu_scan_report_free(&job->report);
        if (job->cancelled || InterlockedCompareExchange(&g.cancel_requested, 0, 0)) {
            SendMessageW(g.progress, PBM_SETSTATE, PBST_PAUSED, 0);
            set_status(tr(UI_SCAN_CANCELLED));
        } else {
            SendMessageW(g.progress, PBM_SETSTATE, PBST_ERROR, 0);
            msgbox_error_utf8(job->err.message, tr(UI_SCAN_FAILED), MB_ICONERROR);
            set_status(gui_error_is_device_lost(job->err.message) ? device_lost_status() : tr(UI_READY));
        }
    }
    free(job);
    InterlockedExchange(&g.cancel_requested, 0);
}

static int checked_count_total(void) {
    sync_checked_from_list();
    int total = 0;
    for (size_t i = 0; i < g.checked_count; i++) {
        if (g.checked[i]) total++;
    }
    return total;
}

static void select_valid_candidates(void) {
    if (!g.has_report || !g.checked) return;

    memset(g.checked, 0, g.checked_count);
    int rows = ListView_GetItemCount(g.list);
    for (int row = 0; row < rows; row++) {
        LVITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_PARAM;
        item.iItem = row;
        if (!SendMessageW(g.list, LVM_GETITEMW, 0, (LPARAM)&item)) continue;
        size_t i = (size_t)item.lParam;
        if (i >= g.checked_count || i >= g.report.candidates.count) continue;

        RecuCandidate *c = &g.report.candidates.items[i];
        RecuValidationStatus val = ensure_validation(c);
        g.checked[i] = (uint8_t)(c->recoverable && val == RECU_VALIDATION_VALID &&
                                 !c->duplicate_of && !c->same_offset_as && !c->overlaps_with);
    }
    g.suppress_check_sync_once = 1;
    populate_results();
}

static void clear_checks(void) {
    if (!g.checked) return;
    memset(g.checked, 0, g.checked_count);
    g.suppress_check_sync_once = 1;
    populate_results();
}

static int recover_one(size_t idx, const RecuRecoverOptions *opt, char *written, size_t written_size, RecuError *err) {
    char local_written[RECU_MAX_PATH];
    if (!written) {
        written = local_written;
        written_size = sizeof(local_written);
    }
    return recu_recover_candidate(&g.src, &g.report.volume, &g.report.candidates.items[idx], opt, written, written_size, err);
}

static void do_recover_selected(void) {
    int idx = selected_candidate_index();
    int checked_total = checked_count_total();
    if ((checked_total == 0 && idx < 0) || !g.has_report || !g.source_open) {
        msgbox_utf8(checked_total == 0 ? tr(UI_NO_CHECKED) : tr(UI_SELECT_CANDIDATE), "recu", MB_ICONINFORMATION);
        return;
    }
    char folder[RECU_MAX_PATH];
    if (!choose_folder(folder, sizeof(folder))) return;
    if (recovery_target_is_source_drive(folder)) {
        msgbox_utf8(tr(UI_SAME_DRIVE_BLOCKED), tr(UI_RECOVERY_FAILED), MB_ICONERROR);
        return;
    }
    RecuRecoverOptions opt;
    memset(&opt, 0, sizeof(opt));
    opt.output_dir = folder;
    opt.preserve_paths = SendMessageW(g.preserve_paths, BM_GETCHECK, 0, 0) == BST_CHECKED;
    opt.write_report = 1;
    opt.progress = gui_progress;

    int selected_total = checked_total > 0 ? checked_total : 1;
    int write_log = SendMessageW(g.recovery_log, BM_GETCHECK, 0, 0) == BST_CHECKED;
    RecoveryLog log;
    memset(&log, 0, sizeof(log));
    if (write_log) {
        RecuError log_err;
        recu_error_clear(&log_err);
        RecuReportFormat log_format = selected_report_format(g.log_format_combo);
        if (!open_recovery_log(folder, g.src.path, log_format, selected_total, &log, &log_err)) {
            char friendly[RECU_MAX_ERR + 1024];
            char msg[RECU_MAX_ERR + 1400];
            localize_error_message(log_err.message, friendly, sizeof(friendly));
            snprintf(msg, sizeof(msg), "%s\n%s\n\n%s", tr(UI_LOG_CREATE_FAILED), friendly, tr(UI_LOG_CONTINUE_QUESTION));
            if (msgbox_utf8_ret(msg, tr(UI_RECOVERY_FAILED), MB_ICONERROR | MB_YESNO | MB_DEFBUTTON2) != IDYES) {
                return;
            }
            log.path[0] = 0;
        }
    }

    char last_error[RECU_MAX_ERR + 1024];
    last_error[0] = 0;
    int recovered = 0;
    int failed = 0;
    int skipped = 0;
    int post_valid = 0;
    int post_partial = 0;
    int post_damaged = 0;
    int post_mismatch = 0;
    int post_unknown = 0;
    if (checked_total > 0) {
        for (size_t i = 0; i < g.checked_count; i++) {
            if (!g.checked[i]) continue;
            RecuCandidate *c = &g.report.candidates.items[i];
            ensure_validation(c);
            char written[RECU_MAX_PATH] = "";
            RecuError row_err;
            recu_error_clear(&row_err);
            if (c->recoverable && recover_one(i, &opt, written, sizeof(written), &row_err)) {
                PostCheckResult post;
                post_check_recovered_file(c, written, &post);
                remember_recovered_candidate(i, written, &post);
                count_post_check_result(&post, &post_valid, &post_partial, &post_damaged, &post_mismatch, &post_unknown);
                recovered++;
                write_recovery_log_row(&log, c, "recovered", written, "", post.written_size, &post);
            } else {
                if (c->recoverable) failed++;
                else skipped++;
                const char *reason = c->recoverable ? row_err.message : "candidate is not recoverable";
                char friendly[RECU_MAX_ERR + 1024];
                localize_error_message(reason, friendly, sizeof(friendly));
                recu_safe_copy(last_error, sizeof(last_error), friendly);
                write_recovery_log_row(&log, c, c->recoverable ? "failed" : "skipped", "", friendly, 0, NULL);
            }
        }
    } else {
        char written[RECU_MAX_PATH] = "";
        PostCheckResult single_post;
        memset(&single_post, 0, sizeof(single_post));
        RecuCandidate *c = &g.report.candidates.items[idx];
        ensure_validation(c);
        RecuError row_err;
        recu_error_clear(&row_err);
        if (c->recoverable && recover_one((size_t)idx, &opt, written, sizeof(written), &row_err)) {
            post_check_recovered_file(c, written, &single_post);
            remember_recovered_candidate((size_t)idx, written, &single_post);
            count_post_check_result(&single_post, &post_valid, &post_partial, &post_damaged, &post_mismatch, &post_unknown);
            recovered++;
            write_recovery_log_row(&log, c, "recovered", written, "", single_post.written_size, &single_post);
        } else {
            if (c->recoverable) failed++;
            else skipped++;
            const char *reason = c->recoverable ? row_err.message : "candidate is not recoverable";
            char friendly[RECU_MAX_ERR + 1024];
            localize_error_message(reason, friendly, sizeof(friendly));
            recu_safe_copy(last_error, sizeof(last_error), friendly);
            write_recovery_log_row(&log, c, c->recoverable ? "failed" : "skipped", "", friendly, 0, NULL);
        }
        if (recovered == 1) {
            set_last_recovery_output(written, 1);
            char report_path[RECU_MAX_PATH];
            RecuError report_err;
            recu_error_clear(&report_err);
            RecuReportFormat report_format = selected_report_format(g.report_format_combo);
            int report_ok = write_auto_scan_report(folder, report_format, report_path, sizeof(report_path), &report_err);
            close_recovery_log(&log);
            char summary[RECU_MAX_PATH + 320];
            char post_line[256] = "";
            append_post_check_line(post_line, sizeof(post_line), &single_post);
            char msg[RECU_MAX_PATH * 3 + 512];
            snprintf(summary, sizeof(summary), tr(UI_RECOVERED_PREFIX), written);
            if (post_line[0]) {
                recu_safe_append(summary, sizeof(summary), "\n\n");
                recu_safe_append(summary, sizeof(summary), post_line);
            }
            snprintf(msg, sizeof(msg), "%s\n\n%s%s%s%s%s%s",
                     summary,
                     log.path[0] ? (g.lang == GUI_LANG_RU ? "Лог восстановления:\n" : "Recovery log:\n") : "",
                     log.path[0] ? log.path : "",
                     log.path[0] ? "\n\n" : "",
                     report_ok ? (g.lang == GUI_LANG_RU ? "Отчет сканирования:\n" : "Scan report:\n") : tr(UI_REPORT_WARNING),
                     report_ok ? report_path : "",
                     report_ok ? "" : "");
            msgbox_utf8(msg, tr(UI_RECOVERY_COMPLETE_TITLE), MB_ICONINFORMATION);
            if (!report_ok) msgbox_error_utf8(report_err.message, tr(UI_REPORT_FAILED), MB_ICONWARNING);
            set_status(tr(UI_RECOVERY_COMPLETE_STATUS));
            return;
        }
    }
    char report_path[RECU_MAX_PATH];
    RecuError report_err;
    recu_error_clear(&report_err);
    RecuReportFormat report_format = selected_report_format(g.report_format_combo);
    int report_ok = write_auto_scan_report(folder, report_format, report_path, sizeof(report_path), &report_err);
    close_recovery_log(&log);
    if (recovered == 0 && failed > 0) {
        msgbox_utf8(last_error[0] ? last_error : tr(UI_RECOVERY_FAILED), tr(UI_RECOVERY_FAILED), MB_ICONERROR);
        return;
    }
    if (recovered > 0) set_last_recovery_output(folder, 0);
    char summary[RECU_MAX_PATH + 320];
    char msg[RECU_MAX_PATH * 3 + 320];
    snprintf(summary, sizeof(summary), tr(UI_RECOVERED_MANY), recovered, folder);
    if (recovered > 0) {
        char post_summary[256];
        snprintf(post_summary, sizeof(post_summary),
                 g.lang == GUI_LANG_RU
                     ? "\nПост-проверка: валидно %d, частично %d, повреждено %d, размер не совпал %d, неизвестно %d"
                     : "\nPost-check: valid %d, partial %d, damaged %d, size mismatch %d, unknown %d",
                 post_valid, post_partial, post_damaged, post_mismatch, post_unknown);
        recu_safe_append(summary, sizeof(summary), post_summary);
    }
    snprintf(msg, sizeof(msg),
             "%s\n%s%d\n%s%d\n\n%s%s%s%s%s%s",
             summary,
             g.lang == GUI_LANG_RU ? "Ошибок: " : "Failed: ", failed,
             g.lang == GUI_LANG_RU ? "Пропущено: " : "Skipped: ", skipped,
             log.path[0] ? (g.lang == GUI_LANG_RU ? "Лог восстановления:\n" : "Recovery log:\n") : "",
             log.path[0] ? log.path : "",
             log.path[0] ? "\n\n" : "",
             report_ok ? (g.lang == GUI_LANG_RU ? "Отчет сканирования:\n" : "Scan report:\n") : tr(UI_REPORT_WARNING),
             report_ok ? report_path : "",
             report_ok ? "" : "");
    msgbox_utf8(msg, tr(UI_RECOVERY_COMPLETE_TITLE), MB_ICONINFORMATION);
    if (!report_ok) msgbox_error_utf8(report_err.message, tr(UI_REPORT_FAILED), MB_ICONWARNING);
    set_status(tr(UI_RECOVERY_COMPLETE_STATUS));
}

static void do_create_image(void) {
    char source_path[RECU_MAX_PATH];
    get_window_text_utf8(g.source, source_path, sizeof(source_path));
    if (!source_path[0]) {
        msgbox_utf8(tr(UI_NO_SOURCE), "recu", MB_ICONINFORMATION);
        return;
    }
    char out[RECU_MAX_PATH];
    const char *image_filter = g.lang == GUI_LANG_RU
        ? "Образы дисков (*.img;*.dd)\0*.img;*.dd\0Все файлы\0*.*\0"
        : "Disk images (*.img;*.dd)\0*.img;*.dd\0All files\0*.*\0";
    if (!save_file_dialog(image_filter, "img", out, sizeof(out))) return;
    if (!has_image_extension(out)) force_extension(out, sizeof(out), "img");
    RecuError err;
    RecuSource src;
    recu_error_clear(&err);
    if (!recu_source_open(&src, source_path, &err)) {
        msgbox_error_utf8(err.message, tr(UI_OPEN_FAILED), MB_ICONERROR);
        if (gui_error_is_device_lost(err.message)) set_status(device_lost_status());
        return;
    }
    if (src.is_raw_device && output_path_is_source_drive(src.path, out)) {
        recu_source_close(&src);
        msgbox_utf8(tr(UI_SAME_DRIVE_BLOCKED), tr(UI_IMAGE_FAILED), MB_ICONERROR);
        return;
    }
#ifdef _WIN32
    uint64_t free_bytes = 0;
    if (free_space_for_output_path(out, &free_bytes) && free_bytes < src.size) {
        char need[64], have[64], msg[512];
        format_size(src.size, need, sizeof(need));
        format_size(free_bytes, have, sizeof(have));
        if (g.lang == GUI_LANG_RU) {
            snprintf(msg, sizeof(msg), "Недостаточно свободного места для образа.\nНужно: %s\nСвободно: %s\n\nОбраз не будет создан.", need, have);
        } else {
            snprintf(msg, sizeof(msg), "Not enough free space for the disk image.\nRequired: %s\nAvailable: %s\n\nImage creation was not started.", need, have);
        }
        recu_source_close(&src);
        msgbox_utf8(msg, tr(UI_IMAGE_FAILED), MB_ICONERROR);
        return;
    }
#endif
    InterlockedExchange(&g.cancel_requested, 0);
    set_scan_running(1);
    int ok = recu_create_image(&src, out, gui_progress, NULL, &err);
    set_scan_running(0);
    recu_source_close(&src);
    if (!ok) {
        if (error_contains_ci(err.message, "operation cancelled")) {
            set_status(tr(UI_SCAN_CANCELLED));
        } else {
            set_status(gui_error_is_device_lost(err.message) ? device_lost_status() : tr(UI_READY));
            msgbox_error_utf8(err.message, tr(UI_IMAGE_FAILED), MB_ICONERROR);
        }
        InterlockedExchange(&g.cancel_requested, 0);
        return;
    }
    InterlockedExchange(&g.cancel_requested, 0);
    msgbox_utf8(tr(UI_IMAGE_CREATED), "recu", MB_ICONINFORMATION);
    set_status(tr(UI_IMAGE_CREATED_STATUS));
}

static void do_save_report(void) {
    if (!g.has_report) {
        msgbox_utf8(tr(UI_SCAN_FIRST_REPORT), "recu", MB_ICONINFORMATION);
        return;
    }
    char out[RECU_MAX_PATH];
    RecuReportFormat format = selected_report_format(g.report_format_combo);
    char suggested[192];
    build_auto_artifact_name(g.src.path, "scan-report", format, suggested, sizeof(suggested), 0);
    const char *ext = recu_report_format_extension(format);
    if (!save_file_dialog_suggested(report_filter_for_format(format), ext, suggested, out, sizeof(out))) return;
    if (!ext_eq_ci(path_extension(out), ext)) force_extension(out, sizeof(out), ext);
    RecuError err;
    recu_error_clear(&err);
    if (!write_scan_report_to_path(out, format, &err)) {
        msgbox_error_utf8(err.message, tr(UI_REPORT_FAILED), MB_ICONERROR);
        return;
    }
    set_status(tr(UI_REPORT_SAVED_STATUS));
}

static int path_eq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int find_drive_path_in_list(const RecuDriveInfo *drives, int count, const char *path) {
    if (!drives || !path || !*path) return -1;
    for (int i = 0; i < count; i++) {
        if (path_eq_ci(drives[i].volume_path, path)) return i;
    }
    return -1;
}

static int drive_info_equal(const RecuDriveInfo *a, const RecuDriveInfo *b) {
    return a && b &&
           path_eq_ci(a->volume_path, b->volume_path) &&
           strcmp(a->display, b->display) == 0 &&
           a->size == b->size &&
           a->drive_type == b->drive_type;
}

static int drive_lists_equal(const RecuDriveInfo *a, int a_count, const RecuDriveInfo *b, int b_count) {
    if (a_count != b_count) return 0;
    for (int i = 0; i < a_count; i++) {
        if (!drive_info_equal(&a[i], &b[i])) return 0;
    }
    return 1;
}

static void refill_drive_combo(void) {
    SendMessageW(g.drives, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g.drive_count; i++) {
        wchar_t wdisplay[160];
        utf8_to_wide_buf(g.drive_info[i].display, wdisplay, (int)(sizeof(wdisplay) / sizeof(wdisplay[0])));
        int idx = (int)SendMessageW(g.drives, CB_ADDSTRING, 0, (LPARAM)wdisplay);
        SendMessageW(g.drives, CB_SETITEMDATA, idx, i);
    }
}

static int refresh_drive_combo(int force, int select_new_if_source_empty) {
    if (!g.drives || g.scan_running) return 0;

    RecuDriveInfo old_drives[64];
    int old_count = g.drive_count;
    if (old_count > 64) old_count = 64;
    if (old_count > 0) memcpy(old_drives, g.drive_info, sizeof(old_drives[0]) * old_count);

    RecuDriveInfo next_drives[64];
    int next_count = recu_list_windows_drives(next_drives, 64);
    if (!force && drive_lists_equal(g.drive_info, g.drive_count, next_drives, next_count)) {
        return 0;
    }

    char source_before[RECU_MAX_PATH];
    char selected_path[16];
    source_before[0] = 0;
    selected_path[0] = 0;
    get_window_text_utf8(g.source, source_before, sizeof(source_before));

    int old_sel = (int)SendMessageW(g.drives, CB_GETCURSEL, 0, 0);
    if (old_sel >= 0) {
        int old_di = (int)SendMessageW(g.drives, CB_GETITEMDATA, old_sel, 0);
        if (old_di >= 0 && old_di < old_count) {
            recu_safe_copy(selected_path, sizeof(selected_path), old_drives[old_di].volume_path);
        }
    }

    int source_empty = source_before[0] == 0;
    int source_was_drive = find_drive_path_in_list(old_drives, old_count, source_before) >= 0 ||
                           find_drive_path_in_list(next_drives, next_count, source_before) >= 0;

    int new_drive_index = -1;
    if (select_new_if_source_empty && source_empty) {
        for (int i = 0; i < next_count; i++) {
            if (find_drive_path_in_list(old_drives, old_count, next_drives[i].volume_path) < 0 &&
                next_drives[i].drive_type == DRIVE_REMOVABLE) {
                new_drive_index = i;
                break;
            }
        }
        if (new_drive_index < 0) {
            for (int i = 0; i < next_count; i++) {
                if (find_drive_path_in_list(old_drives, old_count, next_drives[i].volume_path) < 0) {
                    new_drive_index = i;
                    break;
                }
            }
        }
    }

    memcpy(g.drive_info, next_drives, sizeof(next_drives[0]) * next_count);
    g.drive_count = next_count;
    refill_drive_combo();

    int select_index = -1;
    if (selected_path[0]) select_index = find_drive_path_in_list(g.drive_info, g.drive_count, selected_path);
    if (select_index < 0 && source_was_drive) select_index = find_drive_path_in_list(g.drive_info, g.drive_count, source_before);
    if (select_index < 0) select_index = new_drive_index;

    if (select_index >= 0) {
        SendMessageW(g.drives, CB_SETCURSEL, select_index, 0);
        if (source_empty || source_was_drive) {
            set_window_text_utf8(g.source, g.drive_info[select_index].volume_path);
        }
    } else {
        SendMessageW(g.drives, CB_SETCURSEL, (WPARAM)-1, 0);
        if (source_was_drive) {
            set_window_text_utf8(g.source, "");
        }
    }
    return 1;
}

static void populate_language_combo(void) {
    SendMessageW(g.lang_combo, CB_RESETCONTENT, 0, 0);
    wchar_t en[32], ru[32];
    utf8_to_wide_buf("English", en, (int)(sizeof(en) / sizeof(en[0])));
    utf8_to_wide_buf("Русский", ru, (int)(sizeof(ru) / sizeof(ru[0])));
    SendMessageW(g.lang_combo, CB_ADDSTRING, 0, (LPARAM)en);
    SendMessageW(g.lang_combo, CB_ADDSTRING, 0, (LPARAM)ru);
    SendMessageW(g.lang_combo, CB_SETCURSEL, g.lang == GUI_LANG_RU ? 1 : 0, 0);
}

static void populate_category_combo(void) {
    int current = selected_category();
    SendMessageW(g.category_combo, CB_RESETCONTENT, 0, 0);
    UiText cats[] = {UI_CAT_ALL, UI_CAT_RELIABLE, UI_CAT_DOCUMENTS, UI_CAT_PHOTOS, UI_CAT_VIDEO, UI_CAT_ARCHIVES, UI_CAT_NO_NAME, UI_CAT_LOW_CHANCE};
    for (int i = 0; i < (int)(sizeof(cats) / sizeof(cats[0])); i++) {
        wchar_t w[80];
        utf8_to_wide_buf(tr(cats[i]), w, (int)(sizeof(w) / sizeof(w[0])));
        SendMessageW(g.category_combo, CB_ADDSTRING, 0, (LPARAM)w);
    }
    if (current < 0 || current > 7) current = 0;
    SendMessageW(g.category_combo, CB_SETCURSEL, current, 0);
}

static void populate_validation_combo(void) {
    int current = selected_validation_filter();
    SendMessageW(g.validation_combo, CB_RESETCONTENT, 0, 0);
    UiText vals[] = {UI_VAL_FILTER_ALL, UI_VAL_FILTER_VALID, UI_VAL_FILTER_HIDE_BAD};
    for (int i = 0; i < (int)(sizeof(vals) / sizeof(vals[0])); i++) {
        wchar_t w[96];
        utf8_to_wide_buf(tr(vals[i]), w, (int)(sizeof(w) / sizeof(w[0])));
        SendMessageW(g.validation_combo, CB_ADDSTRING, 0, (LPARAM)w);
    }
    if (current < 0 || current > 2) current = 0;
    SendMessageW(g.validation_combo, CB_SETCURSEL, current, 0);
}

static void populate_report_format_combo(HWND combo, RecuReportFormat selected) {
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    RecuReportFormat formats[] = {RECU_REPORT_CSV, RECU_REPORT_JSON, RECU_REPORT_LOG};
    for (int i = 0; i < (int)(sizeof(formats) / sizeof(formats[0])); i++) {
        wchar_t w[16];
        utf8_to_wide_buf(recu_report_format_name(formats[i]), w, (int)(sizeof(w) / sizeof(w[0])));
        int idx = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)w);
        SendMessageW(combo, CB_SETITEMDATA, idx, formats[i]);
        if (formats[i] == selected) SendMessageW(combo, CB_SETCURSEL, idx, 0);
    }
    if (SendMessageW(combo, CB_GETCURSEL, 0, 0) < 0) SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

static RecuReportFormat selected_report_format(HWND combo) {
    if (!combo) return RECU_REPORT_CSV;
    int sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (sel < 0) return RECU_REPORT_CSV;
    LRESULT data = SendMessageW(combo, CB_GETITEMDATA, sel, 0);
    if (data == RECU_REPORT_JSON || data == RECU_REPORT_LOG || data == RECU_REPORT_CSV) return (RecuReportFormat)data;
    return RECU_REPORT_CSV;
}

static void update_log_format_enabled(void) {
    int enabled = g.recovery_log && SendMessageW(g.recovery_log, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(g.log_format_combo, enabled && !g.scan_running);
    EnableWindow(g.log_format_label, enabled && !g.scan_running);
}

static void update_safe_tooltip(void) {
    if (!g.tooltip || !g.hwnd) return;
    HWND safe = GetDlgItem(g.hwnd, IDC_SAFE_MODE);
    if (!safe) return;
    utf8_to_wide_buf(tr(UI_SAFE_TOOLTIP), g.safe_tooltip, (int)(sizeof(g.safe_tooltip) / sizeof(g.safe_tooltip[0])));
    TOOLINFOW ti;
    ZeroMemory(&ti, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = g.hwnd;
    ti.uId = (UINT_PTR)safe;
    ti.lpszText = g.safe_tooltip;
    if (!g.safe_tooltip_added) {
        SendMessageW(g.tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        g.safe_tooltip_added = 1;
    } else {
        SendMessageW(g.tooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    }
}

static void update_log_tooltip(void) {
    if (!g.tooltip || !g.hwnd || !g.recovery_log) return;
    utf8_to_wide_buf(tr(UI_RECOVERY_LOG_TOOLTIP), g.log_tooltip, (int)(sizeof(g.log_tooltip) / sizeof(g.log_tooltip[0])));
    TOOLINFOW ti;
    ZeroMemory(&ti, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = g.hwnd;
    ti.uId = (UINT_PTR)g.recovery_log;
    ti.lpszText = g.log_tooltip;
    if (!g.log_tooltip_added) {
        SendMessageW(g.tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        g.log_tooltip_added = 1;
    } else {
        SendMessageW(g.tooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    }
}

static void update_preserve_tooltip(void) {
    if (!g.tooltip || !g.hwnd || !g.preserve_paths) return;
    utf8_to_wide_buf(tr(UI_PRESERVE_PATHS_TOOLTIP), g.preserve_tooltip, (int)(sizeof(g.preserve_tooltip) / sizeof(g.preserve_tooltip[0])));
    TOOLINFOW ti;
    ZeroMemory(&ti, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = g.hwnd;
    ti.uId = (UINT_PTR)g.preserve_paths;
    ti.lpszText = g.preserve_tooltip;
    if (!g.preserve_tooltip_added) {
        SendMessageW(g.tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        g.preserve_tooltip_added = 1;
    } else {
        SendMessageW(g.tooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    }
}

static void apply_language(int refresh_results) {
    set_window_text_utf8(g.hwnd, tr(UI_TITLE));
    set_window_text_utf8(GetDlgItem(g.hwnd, IDC_BROWSE), tr(UI_BROWSE));
    set_window_text_utf8(GetDlgItem(g.hwnd, IDC_SCAN), tr(UI_SCAN));
    set_window_text_utf8(g.deep, tr(UI_DEEP_SCAN));
    set_window_text_utf8(GetDlgItem(g.hwnd, IDC_SAFE_MODE), tr(UI_SAFE_MODE));
    update_safe_tooltip();
    set_window_text_utf8(GetDlgItem(g.hwnd, IDC_IMAGE), tr(UI_CREATE_IMAGE));
    set_window_text_utf8(GetDlgItem(g.hwnd, IDC_RECOVER), tr(UI_RECOVER_CHECKED));
    set_window_text_utf8(GetDlgItem(g.hwnd, IDC_REPORT), tr(UI_SAVE_REPORT));
    set_window_text_utf8(g.report_format_label, tr(UI_REPORT_FORMAT));
    set_window_text_utf8(g.recovery_log, tr(UI_RECOVERY_LOG));
    set_window_text_utf8(g.preserve_paths, tr(UI_PRESERVE_PATHS));
    set_window_text_utf8(g.log_format_label, tr(UI_LOG_FORMAT));
    update_log_tooltip();
    update_preserve_tooltip();
    set_window_text_utf8(GetDlgItem(g.hwnd, IDC_SHOW_OUTPUT), tr(UI_SHOW_OUTPUT));
    set_window_text_utf8(GetDlgItem(g.hwnd, IDC_OPEN_OUTPUT), tr(UI_OPEN_OUTPUT));
    set_window_text_utf8(g.hide_zero, tr(UI_HIDE_ZERO));
    set_window_text_utf8(GetDlgItem(g.hwnd, IDC_SELECT_HIGH), tr(UI_SELECT_HIGH));
    set_window_text_utf8(GetDlgItem(g.hwnd, IDC_CLEAR_CHECKS), tr(UI_CLEAR_CHECKS));
    set_window_text_utf8(GetDlgItem(g.hwnd, IDC_CANCEL), tr(UI_CANCEL));
    wchar_t cue[128];
    utf8_to_wide_buf(tr(UI_FILTER_CUE), cue, (int)(sizeof(cue) / sizeof(cue[0])));
    SendMessageW(g.filter, EM_SETCUEBANNER, FALSE, (LPARAM)cue);
    populate_category_combo();
    populate_validation_combo();
    populate_report_format_combo(g.report_format_combo, selected_report_format(g.report_format_combo));
    populate_report_format_combo(g.log_format_combo, selected_report_format(g.log_format_combo));
    update_log_format_enabled();
    init_list_columns(g.list);
    if (refresh_results && g.has_report) {
        populate_results();
        update_preview();
    } else {
        set_status(tr(UI_READY));
    }
    layout(g.hwnd);
}

static void layout(HWND hwnd) {
    if (!g.source || !g.list) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int pad = 10;
    int top = pad;
    int gap = 8;
    int lang_w = 100;
    int image_w_top = 130;
    int safe_w = g.lang == GUI_LANG_RU ? 160 : 120;
    int deep_w = g.lang == GUI_LANG_RU ? 140 : 112;
    int cancel_w = 82;
    int scan_w = g.lang == GUI_LANG_RU ? 118 : 90;
    int drives_w = 220;
    int browse_w = 44;
    int x = w - pad - lang_w;
    MoveWindow(g.lang_combo, x, top, lang_w, 180, TRUE);
    x -= gap + image_w_top;
    MoveWindow(GetDlgItem(hwnd, IDC_IMAGE), x, top, image_w_top, 24, TRUE);
    x -= gap + safe_w;
    MoveWindow(GetDlgItem(hwnd, IDC_SAFE_MODE), x, top, safe_w, 24, TRUE);
    x -= gap + deep_w;
    MoveWindow(g.deep, x, top, deep_w, 24, TRUE);
    x -= gap + cancel_w;
    MoveWindow(GetDlgItem(hwnd, IDC_CANCEL), x, top, cancel_w, 24, TRUE);
    x -= gap + scan_w;
    MoveWindow(GetDlgItem(hwnd, IDC_SCAN), x, top, scan_w, 24, TRUE);
    x -= gap + drives_w;
    MoveWindow(g.drives, x, top, drives_w, 280, TRUE);
    x -= gap + browse_w;
    MoveWindow(GetDlgItem(hwnd, IDC_BROWSE), x, top, browse_w, 24, TRUE);
    int source_w = x - pad - gap;
    if (source_w < 250) source_w = 250;
    MoveWindow(g.source, pad, top, source_w, 24, TRUE);

    top += 34;
    int report_w = 130;
    int recover_w = 200;
    int clear_w = 95;
    int select_w = 150;
    int category_w = 135;
    int validation_w = 155;
    int right = w - pad;
    right -= report_w;
    MoveWindow(GetDlgItem(hwnd, IDC_REPORT), right, top, report_w, 24, TRUE);
    right -= gap + recover_w;
    MoveWindow(GetDlgItem(hwnd, IDC_RECOVER), right, top, recover_w, 24, TRUE);
    right -= gap + clear_w;
    MoveWindow(GetDlgItem(hwnd, IDC_CLEAR_CHECKS), right, top, clear_w, 24, TRUE);
    right -= gap + select_w;
    MoveWindow(GetDlgItem(hwnd, IDC_SELECT_HIGH), right, top, select_w, 24, TRUE);

    int left = pad;
    int filters_right = right - gap;
    int filter_w = filters_right - left - gap * 2 - category_w - validation_w;
    if (filter_w < 160) filter_w = 160;
    MoveWindow(g.filter, left, top, filter_w, 24, TRUE);
    left += filter_w + gap;
    MoveWindow(g.category_combo, left, top, category_w, 180, TRUE);
    left += category_w + gap;
    MoveWindow(g.validation_combo, left, top, validation_w, 180, TRUE);

    top += 34;
    int options_top = top;
    int report_label_w = g.lang == GUI_LANG_RU ? 58 : 58;
    int fmt_w = 76;
    int log_label_w = g.lang == GUI_LANG_RU ? 42 : 92;
    int recovery_log_w = g.lang == GUI_LANG_RU ? 230 : 150;
    int preserve_w = g.lang == GUI_LANG_RU ? 190 : 110;
    int hide_zero_w = g.lang == GUI_LANG_RU ? 132 : 88;
    left = pad;
    MoveWindow(g.preserve_paths, left, options_top, preserve_w, 22, TRUE);
    left += preserve_w + gap + 8;
    MoveWindow(g.hide_zero, left, options_top, hide_zero_w, 22, TRUE);
    left += hide_zero_w + gap + 8;
    MoveWindow(g.recovery_log, left, options_top, recovery_log_w, 22, TRUE);
    left += recovery_log_w + gap + 10;
    MoveWindow(g.log_format_label, left, options_top + 3, log_label_w, 18, TRUE);
    left += log_label_w + 4;
    MoveWindow(g.log_format_combo, left, options_top, fmt_w, 120, TRUE);
    left += fmt_w + gap + 20;
    MoveWindow(g.report_format_label, left, options_top + 3, report_label_w, 18, TRUE);
    left += report_label_w + 4;
    MoveWindow(g.report_format_combo, left, options_top, fmt_w, 120, TRUE);

    top += 30;
    int status_h = 24;
    int available_h = h - top - status_h - gap;
    if (available_h < 300) available_h = 300;
    int bottom_h = available_h / 3;
    if (bottom_h < 120) bottom_h = 120;
    int list_h = available_h - bottom_h - gap;
    if (list_h < 170) {
        list_h = 170;
        bottom_h = available_h - list_h - gap;
        if (bottom_h < 100) bottom_h = 100;
    }
    int list_w = w - pad * 2;
    if (list_w < 400) list_w = 400;
    MoveWindow(g.list, pad, top, list_w, list_h, TRUE);
    resize_list_columns(g.list, list_w);

    top += list_h + 8;
    int bottom_w = w - pad * 2;
    if (bottom_w < 400) bottom_w = 400;
    int image_w = bottom_w / 4;
    if (image_w < 220) image_w = 220;
    if (image_w > 330) image_w = 330;
    int preview_w = bottom_w - image_w - gap;
    if (preview_w < 240) {
        preview_w = 240;
        image_w = bottom_w - preview_w - gap;
        if (image_w < 160) image_w = 160;
    }
    MoveWindow(g.image_preview, pad, top, image_w, bottom_h, TRUE);
    MoveWindow(g.preview, pad + image_w + gap, top, preview_w, bottom_h, TRUE);

    int progress_w = w / 5;
    if (progress_w < 160) progress_w = 160;
    if (progress_w > 260) progress_w = 260;
    int show_w = 95;
    int open_w = 95;
    int status_y = h - 23;
    int status_w = w - pad * 2 - progress_w - show_w - open_w - gap * 3;
    if (status_w < 180) status_w = 180;
    MoveWindow(g.status, pad, status_y, status_w, 20, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_OPEN_OUTPUT), w - pad - progress_w - show_w - open_w - gap * 2, h - 24, open_w, 20, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_SHOW_OUTPUT), w - pad - progress_w - show_w - gap, h - 24, show_w, 20, TRUE);
    MoveWindow(g.progress, w - pad - progress_w, h - 23, progress_w, 18, TRUE);
}

static void row_colors_for_candidate(const RecuCandidate *c, COLORREF *text, COLORREF *back) {
    if (!c || !text || !back) return;
    *text = RGB(0, 0, 0);
    *back = RGB(255, 255, 255);
    if (c->duplicate_of || c->same_offset_as || c->overlaps_with || c->size == 0 || !c->recoverable) {
        *text = RGB(95, 95, 95);
        *back = RGB(242, 242, 242);
        return;
    }
    if (!c->validation_checked) return;
    switch (c->validation) {
        case RECU_VALIDATION_VALID:
            *back = RGB(232, 248, 235);
            break;
        case RECU_VALIDATION_PARTIAL:
            *back = RGB(255, 249, 218);
            break;
        case RECU_VALIDATION_DAMAGED:
            *text = RGB(120, 0, 0);
            *back = RGB(255, 236, 236);
            break;
        default:
            break;
    }
}

static LRESULT handle_list_custom_draw(NMLVCUSTOMDRAW *draw) {
    if (!draw) return CDRF_DODEFAULT;
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        LVITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_PARAM;
        item.iItem = (int)draw->nmcd.dwItemSpec;
        if (SendMessageW(g.list, LVM_GETITEMW, 0, (LPARAM)&item)) {
            size_t idx = (size_t)item.lParam;
            if (idx < g.report.candidates.count) {
                COLORREF text = RGB(0, 0, 0);
                COLORREF back = RGB(255, 255, 255);
                row_colors_for_candidate(&g.report.candidates.items[idx], &text, &back);
                draw->clrText = text;
                draw->clrTextBk = back;
            }
        }
    }
    return CDRF_DODEFAULT;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.hwnd = hwnd;
            HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            g.source = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                      0, 0, 0, 0, hwnd, (HMENU)IDC_SOURCE, g.inst, NULL);
            CreateWindowW(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_BROWSE, g.inst, NULL);
            g.drives = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                     0, 0, 0, 0, hwnd, (HMENU)IDC_DRIVES, g.inst, NULL);
            CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_SCAN, g.inst, NULL);
            CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_CANCEL, g.inst, NULL);
            g.deep = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                   0, 0, 0, 0, hwnd, (HMENU)IDC_DEEP, g.inst, NULL);
            CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                          0, 0, 0, 0, hwnd, (HMENU)IDC_SAFE_MODE, g.inst, NULL);
            CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_IMAGE, g.inst, NULL);
            g.lang_combo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                         0, 0, 0, 0, hwnd, (HMENU)IDC_LANG, g.inst, NULL);
            g.filter = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                      0, 0, 0, 0, hwnd, (HMENU)IDC_FILTER, g.inst, NULL);
            g.category_combo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                             0, 0, 0, 0, hwnd, (HMENU)IDC_CATEGORY, g.inst, NULL);
            g.validation_combo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                               0, 0, 0, 0, hwnd, (HMENU)IDC_VALIDATION, g.inst, NULL);
            CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_SELECT_HIGH, g.inst, NULL);
            CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_CLEAR_CHECKS, g.inst, NULL);
            CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_RECOVER, g.inst, NULL);
            CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_REPORT, g.inst, NULL);
            CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_SHOW_OUTPUT, g.inst, NULL);
            CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_OPEN_OUTPUT, g.inst, NULL);
            g.preserve_paths = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                             0, 0, 0, 0, hwnd, (HMENU)IDC_PRESERVE_PATHS, g.inst, NULL);
            SendMessageW(g.preserve_paths, BM_SETCHECK, BST_CHECKED, 0);
            g.hide_zero = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        0, 0, 0, 0, hwnd, (HMENU)IDC_HIDE_ZERO, g.inst, NULL);
            SendMessageW(g.hide_zero, BM_SETCHECK, BST_CHECKED, 0);
            g.recovery_log = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                           0, 0, 0, 0, hwnd, (HMENU)IDC_RECOVERY_LOG, g.inst, NULL);
            SendMessageW(g.recovery_log, BM_SETCHECK, BST_CHECKED, 0);
            g.report_format_label = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                                  0, 0, 0, 0, hwnd, (HMENU)IDC_REPORT_FORMAT_LABEL, g.inst, NULL);
            g.report_format_combo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                                  0, 0, 0, 0, hwnd, (HMENU)IDC_REPORT_FORMAT, g.inst, NULL);
            g.log_format_label = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                               0, 0, 0, 0, hwnd, (HMENU)IDC_LOG_FORMAT_LABEL, g.inst, NULL);
            g.log_format_combo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                               0, 0, 0, 0, hwnd, (HMENU)IDC_LOG_FORMAT, g.inst, NULL);
            g.list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                    0, 0, 0, 0, hwnd, (HMENU)IDC_LIST, g.inst, NULL);
            ListView_SetExtendedListViewStyle(g.list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);
            init_list_columns(g.list);
            g.preview = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                       WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
                                       0, 0, 0, 0, hwnd, (HMENU)IDC_PREVIEW, g.inst, NULL);
            g.image_preview = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
                                             WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
                                             0, 0, 0, 0, hwnd, (HMENU)IDC_IMAGE_PREVIEW, g.inst, NULL);
            g.status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_STATUS, g.inst, NULL);
            g.progress = CreateWindowExW(0, PROGRESS_CLASSW, L"",
                                         WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                         0, 0, 0, 0, hwnd, (HMENU)IDC_PROGRESS, g.inst, NULL);
            SendMessageW(g.progress, PBM_SETRANGE, 0, MAKELPARAM(0, 10000));
            g.tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                                        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                        hwnd, NULL, g.inst, NULL);
            if (g.tooltip) SendMessageW(g.tooltip, TTM_SETMAXTIPWIDTH, 0, 420);
            HWND controls[] = {g.source, GetDlgItem(hwnd, IDC_BROWSE), g.drives, GetDlgItem(hwnd, IDC_SCAN), g.deep,
                               GetDlgItem(hwnd, IDC_SAFE_MODE),
                               GetDlgItem(hwnd, IDC_CANCEL), GetDlgItem(hwnd, IDC_IMAGE), g.lang_combo, g.filter, g.category_combo, g.validation_combo,
                               GetDlgItem(hwnd, IDC_SELECT_HIGH), GetDlgItem(hwnd, IDC_CLEAR_CHECKS),
                               GetDlgItem(hwnd, IDC_RECOVER), GetDlgItem(hwnd, IDC_REPORT), GetDlgItem(hwnd, IDC_SHOW_OUTPUT), GetDlgItem(hwnd, IDC_OPEN_OUTPUT),
                               g.preserve_paths, g.hide_zero, g.recovery_log, g.report_format_label, g.report_format_combo, g.log_format_label, g.log_format_combo,
                               g.list, g.preview, g.image_preview, g.status, g.progress};
            for (int i = 0; i < (int)(sizeof(controls) / sizeof(controls[0])); i++) {
                SendMessageW(controls[i], WM_SETFONT, (WPARAM)font, TRUE);
            }
            populate_language_combo();
            refresh_drive_combo(1, 0);
            apply_language(0);
            load_control_settings();
            SetTimer(hwnd, DRIVE_REFRESH_TIMER_ID, DRIVE_REFRESH_INTERVAL_MS, NULL);
            EnableWindow(GetDlgItem(hwnd, IDC_CANCEL), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_SHOW_OUTPUT), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_OPEN_OUTPUT), FALSE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO *mmi = (MINMAXINFO *)lp;
            mmi->ptMinTrackSize.x = RECU_DEFAULT_WINDOW_W;
            mmi->ptMinTrackSize.y = RECU_DEFAULT_WINDOW_H;
            return 0;
        }
        case WM_SIZE:
            layout(hwnd);
            return 0;
        case WM_DEVICECHANGE:
            if (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE || wp == DBT_DEVNODES_CHANGED) {
                refresh_drive_combo(0, 1);
                return TRUE;
            }
            break;
        case WM_TIMER:
            if (wp == DRIVE_REFRESH_TIMER_ID) {
                refresh_drive_combo(0, 1);
                return 0;
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_BROWSE:
                    browse_source();
                    return 0;
                case IDC_DRIVES:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        int sel = (int)SendMessageW(g.drives, CB_GETCURSEL, 0, 0);
                        if (sel >= 0) {
                            int di = (int)SendMessageW(g.drives, CB_GETITEMDATA, sel, 0);
                            if (di >= 0 && di < g.drive_count) set_window_text_utf8(g.source, g.drive_info[di].volume_path);
                        }
                    }
                    return 0;
                case IDC_LANG:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        int sel = (int)SendMessageW(g.lang_combo, CB_GETCURSEL, 0, 0);
                        g.lang = sel == 1 ? GUI_LANG_RU : GUI_LANG_EN;
                        apply_language(1);
                    }
                    return 0;
                case IDC_SCAN:
                    do_scan();
                    return 0;
                case IDC_CANCEL:
                    do_cancel_scan();
                    return 0;
                case IDC_FILTER:
                    if (HIWORD(wp) == EN_CHANGE) populate_results();
                    return 0;
                case IDC_CATEGORY:
                    if (HIWORD(wp) == CBN_SELCHANGE) populate_results();
                    return 0;
                case IDC_VALIDATION:
                    if (HIWORD(wp) == CBN_SELCHANGE) populate_results();
                    return 0;
                case IDC_HIDE_ZERO:
                    populate_results();
                    return 0;
                case IDC_RECOVERY_LOG:
                    update_log_format_enabled();
                    return 0;
                case IDC_SELECT_HIGH:
                    select_valid_candidates();
                    return 0;
                case IDC_CLEAR_CHECKS:
                    clear_checks();
                    return 0;
                case IDC_RECOVER:
                    do_recover_selected();
                    return 0;
                case IDC_IMAGE:
                    do_create_image();
                    return 0;
                case IDC_REPORT:
                    do_save_report();
                    return 0;
                case IDC_SHOW_OUTPUT:
                    show_last_recovery_output();
                    return 0;
                case IDC_OPEN_OUTPUT:
                    open_selected_recovery_output();
                    return 0;
            }
            break;
        case WM_RECU_PROGRESS: {
            ProgressMessage *pm = (ProgressMessage *)lp;
            if (pm) {
                char msg[256];
                format_progress_status(msg, sizeof(msg), pm->stage, pm->done, pm->total);
                set_status(msg);
                set_progress_value(pm->done, pm->total);
                free(pm);
            }
            return 0;
        }
        case WM_RECU_SCAN_DONE:
            finish_scan_job((ScanJob *)lp);
            return 0;
        case WM_NOTIFY:
            if (((LPNMHDR)lp)->idFrom == IDC_LIST) {
                LPNMHDR hdr = (LPNMHDR)lp;
                if (hdr->code == NM_CUSTOMDRAW) {
                    return handle_list_custom_draw((NMLVCUSTOMDRAW *)lp);
                }
                if (hdr->code == LVN_COLUMNCLICK) {
                    NMLISTVIEW *lv = (NMLISTVIEW *)lp;
                    if (g.sort_col == lv->iSubItem) {
                        g.sort_desc = !g.sort_desc;
                    } else {
                        g.sort_col = lv->iSubItem;
                        g.sort_desc = 0;
                    }
                    populate_results();
                    return 0;
                }
                if (hdr->code == NM_DBLCLK) {
                    if (selected_recovered_path()) {
                        open_selected_recovery_output();
                    } else {
                        update_preview();
                    }
                    return 0;
                }
                if (hdr->code == LVN_ITEMCHANGED) update_preview();
                if (hdr->code == NM_CLICK) {
                    LPNMITEMACTIVATE act = (LPNMITEMACTIVATE)lp;
                    if (act->iItem >= 0) {
                        ListView_SetItemState(g.list, act->iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                        update_preview();
                    }
                }
            }
            break;
        case WM_CLOSE:
            save_settings();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            save_settings();
            KillTimer(hwnd, DRIVE_REFRESH_TIMER_ID);
            if (g.scan_running) {
                InterlockedExchange(&g.cancel_requested, 1);
                if (g.scan_thread) WaitForSingleObject(g.scan_thread, 2000);
            }
            clear_image_preview();
            if (g.has_report) recu_scan_report_free(&g.report);
            if (g.source_open) recu_source_close(&g.src);
            free(g.checked);
            clear_recovery_state();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev;
    (void)cmd;
    memset(&g, 0, sizeof(g));
    g.inst = inst;
    g.sort_col = -1;
    LANGID ui_lang = GetUserDefaultUILanguage();
    g.lang = PRIMARYLANGID(ui_lang) == LANG_RUSSIAN ? GUI_LANG_RU : GUI_LANG_EN;
    load_startup_settings();
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    CoInitialize(NULL);
    GdiplusStartupInput gdip;
    memset(&gdip, 0, sizeof(gdip));
    gdip.GdiplusVersion = 1;
    GdiplusStartup(&g.gdiplus_token, &gdip, NULL);

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"RecuClassicWin32";
    RegisterClassW(&wc);

    wchar_t title[128];
    utf8_to_wide_buf(tr(UI_TITLE), title, (int)(sizeof(title) / sizeof(title[0])));
    int win_x = CW_USEDEFAULT;
    int win_y = CW_USEDEFAULT;
    int win_w = RECU_DEFAULT_WINDOW_W;
    int win_h = RECU_DEFAULT_WINDOW_H;
    if (g.has_saved_window_rect) {
        win_x = g.saved_window_rect.left;
        win_y = g.saved_window_rect.top;
        win_w = g.saved_window_rect.right - g.saved_window_rect.left;
        win_h = g.saved_window_rect.bottom - g.saved_window_rect.top;
    }
    HWND hwnd = CreateWindowW(L"RecuClassicWin32", title,
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              win_x, win_y, win_w, win_h,
                              NULL, NULL, inst, NULL);
    ShowWindow(hwnd, g.saved_window_maximized ? SW_MAXIMIZE : show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g.gdiplus_token) GdiplusShutdown(g.gdiplus_token);
    CoUninitialize();
    return (int)msg.wParam;
}
