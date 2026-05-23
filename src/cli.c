#include "recu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cli_progress(void *user, const char *stage, uint64_t done, uint64_t total) {
    (void)user;
    if (strcmp(stage, "deep scan read") == 0 || strcmp(stage, "deep scan skip") == 0) {
        stage = "deep scan";
    }
    static char last_stage[64];
    static int last_pct = -1;
    static uint64_t last_done = 0;
    if (strcmp(last_stage, stage) != 0) {
        recu_safe_copy(last_stage, sizeof(last_stage), stage);
        last_pct = -1;
        last_done = 0;
    }
    if (total) {
        int pct = (int)((done * 100u) / total);
        uint64_t step = done < 1024ull * 1024ull ? 4ull * 1024ull : 1024ull * 1024ull;
        if (pct == last_pct && done < total && done < last_done + step) return 1;
        last_pct = pct;
        last_done = done;
        if (total >= 1024ull * 1024ull * 1024ull) {
            if (done < 1024ull * 1024ull) {
                fprintf(stderr, "\r%-22s %5.1f%% (%llu KB / %llu MB)",
                        stage, ((double)done * 100.0) / (double)total,
                        (unsigned long long)(done / 1024ull),
                        (unsigned long long)(total / (1024ull * 1024ull)));
            } else {
                fprintf(stderr, "\r%-22s %5.1f%% (%llu MB / %llu MB)",
                        stage, ((double)done * 100.0) / (double)total,
                        (unsigned long long)(done / (1024ull * 1024ull)),
                        (unsigned long long)(total / (1024ull * 1024ull)));
            }
        } else {
            fprintf(stderr, "\r%-22s %3d%%", stage, pct);
        }
        if (done >= total) fprintf(stderr, "\n");
    } else {
        fprintf(stderr, "\r%s", stage);
    }
    return 1;
}

static void usage(void) {
    puts("Recu Classic - FAT16/FAT32/exFAT recovery utility");
    puts("");
    puts("Usage:");
    puts("  recu-cli drives");
    puts("  recu-cli scan <image-or-drive> [--deep] [--report <csv>]");
    puts("  recu-cli preview <image-or-drive> <id> [--deep]");
    puts("  recu-cli recover <image-or-drive> <id|all> <output-dir> [--deep] [--preserve-paths]");
    puts("  recu-cli image <drive-or-image> <output.img>");
    puts("");
    puts("Drive examples:");
    puts("  recu-cli scan E: --deep");
    puts("  recu-cli scan \\\\.\\E:");
    puts("  recu-cli image E: D:\\backup\\flash.dd");
}

static int has_arg(int argc, char **argv, const char *needle) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], needle) == 0) return 1;
    }
    return 0;
}

static const char *arg_value(int argc, char **argv, const char *needle) {
    for (int i = 0; i + 1 < argc; i++) {
        if (strcmp(argv[i], needle) == 0) return argv[i + 1];
    }
    return NULL;
}

static int scan_source(const char *path, int deep, RecuScanReport *report) {
    RecuError err;
    recu_error_clear(&err);
    RecuSource src;
    if (!recu_source_open(&src, path, &err)) {
        fprintf(stderr, "open failed: %s\n", err.message);
        return 0;
    }
    RecuScanOptions opt;
    memset(&opt, 0, sizeof(opt));
    opt.quick_scan = 1;
    opt.deep_scan = deep;
    opt.max_carved_files = 2048;
    opt.max_carve_bytes = 512ull * 1024ull * 1024ull;
    opt.progress = cli_progress;
    int ok = recu_scan_source(&src, report, &opt, &err);
    recu_source_close(&src);
    if (!ok) {
        fprintf(stderr, "scan failed: %s\n", err.message);
        return 0;
    }
    return 1;
}

static void print_candidates(const RecuScanReport *report) {
    printf("Filesystem: %s, cluster size: %u, total: %llu bytes\n",
           recu_fs_name(report->volume.fs_type),
           report->volume.cluster_size,
           (unsigned long long)report->volume.total_bytes);
    printf("%-5s %-8s %-8s %-10s %-8s %-12s %s\n", "ID", "Kind", "Conf", "Size", "Format", "Cluster", "Name");
    for (size_t i = 0; i < report->candidates.count; i++) {
        const RecuCandidate *c = &report->candidates.items[i];
        printf("%-5u %-8s %3d%%     %-10llu %-8s %-12u %s\n",
               c->id,
               c->kind == RECU_KIND_CARVED ? "carved" : "deleted",
               c->confidence,
               (unsigned long long)c->size,
               recu_format_name(c->format),
               c->first_cluster,
               c->path);
    }
}

static const RecuCandidate *find_candidate(const RecuCandidateList *list, uint32_t id) {
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].id == id) return &list->items[i];
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    if (strcmp(argv[1], "drives") == 0) {
#ifdef _WIN32
        RecuDriveInfo drives[64];
        int n = recu_list_windows_drives(drives, 64);
        for (int i = 0; i < n; i++) {
            printf("%s  %s\n", drives[i].volume_path, drives[i].display);
        }
#else
        puts("drive enumeration is implemented for Windows builds");
#endif
        return 0;
    }

    if (strcmp(argv[1], "scan") == 0) {
        if (argc < 3) {
            usage();
            return 1;
        }
        int deep = has_arg(argc, argv, "--deep");
        RecuScanReport report;
        if (!scan_source(argv[2], deep, &report)) return 2;
        print_candidates(&report);
        const char *report_path = arg_value(argc, argv, "--report");
        if (report_path) {
            RecuError err;
            recu_error_clear(&err);
            if (!recu_write_report(report_path, &report.volume, &report.candidates, &err)) {
                fprintf(stderr, "report failed: %s\n", err.message);
            } else {
                fprintf(stderr, "report written: %s\n", report_path);
            }
        }
        recu_scan_report_free(&report);
        return 0;
    }

    if (strcmp(argv[1], "preview") == 0) {
        if (argc < 4) {
            usage();
            return 1;
        }
        int deep = has_arg(argc, argv, "--deep");
        uint32_t id = (uint32_t)strtoul(argv[3], NULL, 10);
        RecuError err;
        RecuSource src;
        RecuScanReport report;
        if (!recu_source_open(&src, argv[2], &err)) {
            fprintf(stderr, "open failed: %s\n", err.message);
            return 2;
        }
        RecuScanOptions opt;
        memset(&opt, 0, sizeof(opt));
        opt.quick_scan = 1;
        opt.deep_scan = deep;
        opt.max_carved_files = 2048;
        opt.max_carve_bytes = 512ull * 1024ull * 1024ull;
        opt.progress = cli_progress;
        if (!recu_scan_source(&src, &report, &opt, &err)) {
            fprintf(stderr, "scan failed: %s\n", err.message);
            recu_source_close(&src);
            return 2;
        }
        const RecuCandidate *c = find_candidate(&report.candidates, id);
        if (!c) {
            fprintf(stderr, "candidate id %u not found\n", id);
            recu_scan_report_free(&report);
            recu_source_close(&src);
            return 3;
        }
        char text[RECU_MAX_PREVIEW + 4096];
        if (recu_preview_candidate(&src, &report.volume, c, text, sizeof(text), &err)) {
            puts(text);
        } else {
            fprintf(stderr, "preview failed: %s\n", err.message);
        }
        recu_scan_report_free(&report);
        recu_source_close(&src);
        return 0;
    }

    if (strcmp(argv[1], "recover") == 0) {
        if (argc < 5) {
            usage();
            return 1;
        }
        int deep = has_arg(argc, argv, "--deep");
        int preserve_paths = has_arg(argc, argv, "--preserve-paths");
        RecuError err;
        RecuSource src;
        if (!recu_source_open(&src, argv[2], &err)) {
            fprintf(stderr, "open failed: %s\n", err.message);
            return 2;
        }
        RecuScanOptions scan_opt;
        memset(&scan_opt, 0, sizeof(scan_opt));
        scan_opt.quick_scan = 1;
        scan_opt.deep_scan = deep;
        scan_opt.max_carved_files = 2048;
        scan_opt.max_carve_bytes = 512ull * 1024ull * 1024ull;
        scan_opt.progress = cli_progress;
        RecuScanReport report;
        if (!recu_scan_source(&src, &report, &scan_opt, &err)) {
            fprintf(stderr, "scan failed: %s\n", err.message);
            recu_source_close(&src);
            return 2;
        }
        RecuRecoverOptions ropt;
        memset(&ropt, 0, sizeof(ropt));
        ropt.output_dir = argv[4];
        ropt.preserve_paths = preserve_paths;
        ropt.write_report = 1;
        ropt.progress = cli_progress;
        int recovered = 0;
        if (strcmp(argv[3], "all") == 0) {
            for (size_t i = 0; i < report.candidates.count; i++) {
                char out[RECU_MAX_PATH];
                if (report.candidates.items[i].recoverable &&
                    recu_recover_candidate(&src, &report.volume, &report.candidates.items[i], &ropt, out, sizeof(out), &err)) {
                    printf("recovered: %s\n", out);
                    recovered++;
                }
            }
        } else {
            uint32_t id = (uint32_t)strtoul(argv[3], NULL, 10);
            const RecuCandidate *c = find_candidate(&report.candidates, id);
            if (!c) {
                fprintf(stderr, "candidate id %u not found\n", id);
            } else {
                char out[RECU_MAX_PATH];
                if (recu_recover_candidate(&src, &report.volume, c, &ropt, out, sizeof(out), &err)) {
                    printf("recovered: %s\n", out);
                    recovered++;
                } else {
                    fprintf(stderr, "recover failed: %s\n", err.message);
                }
            }
        }
        char report_path[RECU_MAX_PATH];
        recu_path_join(report_path, sizeof(report_path), argv[4], "recovery-report.csv");
        recu_write_report(report_path, &report.volume, &report.candidates, &err);
        fprintf(stderr, "recovered %d file(s), report: %s\n", recovered, report_path);
        recu_scan_report_free(&report);
        recu_source_close(&src);
        return recovered > 0 ? 0 : 4;
    }

    if (strcmp(argv[1], "image") == 0) {
        if (argc < 4) {
            usage();
            return 1;
        }
        RecuError err;
        RecuSource src;
        if (!recu_source_open(&src, argv[2], &err)) {
            fprintf(stderr, "open failed: %s\n", err.message);
            return 2;
        }
        int ok = recu_create_image(&src, argv[3], cli_progress, NULL, &err);
        recu_source_close(&src);
        if (!ok) {
            fprintf(stderr, "image failed: %s\n", err.message);
            return 2;
        }
        fprintf(stderr, "image written: %s\n", argv[3]);
        return 0;
    }

    usage();
    return 1;
}
