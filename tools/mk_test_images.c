#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define FSEEK _fseeki64
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0777)
#define FSEEK fseeko
#endif

static void le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void le64(uint8_t *p, uint64_t v) { le32(p, (uint32_t)v); le32(p + 4, (uint32_t)(v >> 32)); }

static void write_at(FILE *f, uint64_t off, const void *buf, size_t n) {
    FSEEK(f, (long long)off, SEEK_SET);
    fwrite(buf, 1, n, f);
}

static void set_size(FILE *f, uint64_t size) {
    FSEEK(f, (long long)(size - 1), SEEK_SET);
    fputc(0, f);
}

static void fat_short_deleted_entry(uint8_t *e, const char raw11[11], uint8_t attr, uint32_t cluster, uint32_t size) {
    memset(e, 0, 32);
    memcpy(e, raw11, 11);
    e[0] = 0xE5;
    e[11] = attr;
    le16(e + 20, (uint16_t)(cluster >> 16));
    le16(e + 26, (uint16_t)cluster);
    le32(e + 28, size);
}

static uint8_t fat_lfn_checksum(const char raw11[11]) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + (uint8_t)raw11[i]);
    }
    return sum;
}

static void fat_deleted_lfn_entry(uint8_t *e, const uint16_t *units, int count, uint8_t checksum) {
    static const int offsets[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
    memset(e, 0xFF, 32);
    e[0] = 0xE5;
    e[11] = 0x0F;
    e[12] = 0;
    e[13] = checksum;
    le16(e + 26, 0);
    for (int i = 0; i < 13; i++) {
        uint16_t w = 0xFFFF;
        if (i < count) w = units[i];
        else if (i == count) w = 0x0000;
        le16(e + offsets[i], w);
    }
}

static uint64_t fat_cluster_offset(uint32_t first_data_sector, uint32_t spc, uint32_t cluster) {
    return ((uint64_t)first_data_sector + (uint64_t)(cluster - 2) * spc) * 512u;
}

static void make_fat16(const char *path) {
    const uint32_t total = 16384;
    const uint32_t reserved = 1;
    const uint32_t fats = 1;
    const uint32_t fat_size = 64;
    const uint32_t root_entries = 512;
    const uint32_t root_secs = 32;
    const uint32_t first_data = reserved + fats * fat_size + root_secs;
    FILE *f = fopen(path, "wb");
    set_size(f, (uint64_t)total * 512u);
    uint8_t bs[512];
    memset(bs, 0, sizeof(bs));
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;
    memcpy(bs + 3, "RECUF16 ", 8);
    le16(bs + 11, 512);
    bs[13] = 1;
    le16(bs + 14, reserved);
    bs[16] = fats;
    le16(bs + 17, root_entries);
    le16(bs + 19, total);
    bs[21] = 0xF8;
    le16(bs + 22, fat_size);
    le16(bs + 24, 32);
    le16(bs + 26, 64);
    bs[38] = 0x29;
    le32(bs + 39, 0x12345678);
    memcpy(bs + 43, "RECU FAT16 ", 11);
    memcpy(bs + 54, "FAT16   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;
    write_at(f, 0, bs, sizeof(bs));

    uint8_t fat[512];
    memset(fat, 0, sizeof(fat));
    fat[0] = 0xF8; fat[1] = 0xFF; fat[2] = 0xFF; fat[3] = 0xFF;
    write_at(f, reserved * 512u, fat, sizeof(fat));

    const char *payload = "Recovered from a deleted FAT16 directory entry.\r\n";
    const char *lfn_payload = "Recovered with a verified deleted FAT LFN.\r\n";
    const char *bad_lfn_payload = "Recovered after rejecting a stale FAT LFN.\r\n";
    uint8_t root[1024];
    memset(root, 0, sizeof(root));
    static const uint16_t long_name[] = {
        'L','o','n','g',' ','F','i','l','e','.','t','x','t'
    };
    static const uint16_t stale_name[] = {
        'S','t','a','l','e','.','t','x','t'
    };
    const char valid_short[11] = {'L','O','N','G','F','I',' ',' ','T','X','T'};
    const char stale_short[11] = {'B','A','D','N','A','M','E',' ','T','X','T'};
    fat_deleted_lfn_entry(root, long_name, (int)(sizeof(long_name) / sizeof(long_name[0])), fat_lfn_checksum(valid_short));
    fat_short_deleted_entry(root + 32, valid_short, 0x20, 5, (uint32_t)strlen(lfn_payload));
    fat_deleted_lfn_entry(root + 64, stale_name, (int)(sizeof(stale_name) / sizeof(stale_name[0])), fat_lfn_checksum(stale_short));
    fat_short_deleted_entry(root + 96, stale_short, 0x20, 6, (uint32_t)strlen(bad_lfn_payload));
    fat_short_deleted_entry(root + 128, "DELETED TXT", 0x20, 7, (uint32_t)strlen(payload));
    write_at(f, (reserved + fats * fat_size) * 512u, root, sizeof(root));
    write_at(f, fat_cluster_offset(first_data, 1, 5), lfn_payload, strlen(lfn_payload));
    write_at(f, fat_cluster_offset(first_data, 1, 6), bad_lfn_payload, strlen(bad_lfn_payload));
    write_at(f, fat_cluster_offset(first_data, 1, 7), payload, strlen(payload));
    const char *pdf = "%PDF-1.4\n1 0 obj\n<<>>\nendobj\ntrailer\n<<>>\n%%EOF\n";
    write_at(f, fat_cluster_offset(first_data, 1, 30), pdf, strlen(pdf));
    fclose(f);
}

static void make_fat32(const char *path) {
    const uint32_t total = 131072;
    const uint32_t reserved = 32;
    const uint32_t fats = 1;
    const uint32_t fat_size = 1024;
    const uint32_t first_data = reserved + fats * fat_size;
    FILE *f = fopen(path, "wb");
    set_size(f, (uint64_t)total * 512u);
    uint8_t bs[512];
    memset(bs, 0, sizeof(bs));
    bs[0] = 0xEB; bs[1] = 0x58; bs[2] = 0x90;
    memcpy(bs + 3, "RECUF32 ", 8);
    le16(bs + 11, 512);
    bs[13] = 1;
    le16(bs + 14, reserved);
    bs[16] = fats;
    le32(bs + 32, total);
    le32(bs + 36, fat_size);
    le32(bs + 44, 2);
    le16(bs + 48, 1);
    le16(bs + 50, 6);
    bs[66] = 0x29;
    le32(bs + 67, 0x87654321);
    memcpy(bs + 71, "RECU FAT32 ", 11);
    memcpy(bs + 82, "FAT32   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;
    write_at(f, 0, bs, sizeof(bs));

    uint8_t fat[4096];
    memset(fat, 0, sizeof(fat));
    le32(fat + 0, 0x0FFFFFF8);
    le32(fat + 4, 0xFFFFFFFF);
    le32(fat + 8, 0x0FFFFFFF);
    write_at(f, reserved * 512u, fat, sizeof(fat));

    const char *payload = "Recovered from a deleted FAT32 directory entry.\r\n";
    uint8_t root[512];
    memset(root, 0, sizeof(root));
    fat_short_deleted_entry(root, "PICTURE JPG", 0x20, 10, (uint32_t)strlen(payload));
    write_at(f, fat_cluster_offset(first_data, 1, 2), root, sizeof(root));
    write_at(f, fat_cluster_offset(first_data, 1, 10), payload, strlen(payload));
    const uint8_t jpg[] = {0xFF,0xD8,0xFF,0xE0,0,0x10,'J','F','I','F',0,1,1,0,0,1,0,1,0,0,0xFF,0xD9};
    write_at(f, fat_cluster_offset(first_data, 1, 40), jpg, sizeof(jpg));
    fclose(f);
}

static void utf16_name_units(uint8_t *entry, const uint16_t *units, int start, int count) {
    memset(entry, 0, 32);
    entry[0] = 0x41;
    for (int i = 0; i < 15 && start + i < count; i++) {
        le16(entry + 2 + i * 2, units[start + i]);
    }
}

static uint64_t exfat_cluster_offset(uint32_t heap_sector, uint32_t spc, uint32_t cluster) {
    return ((uint64_t)heap_sector + (uint64_t)(cluster - 2) * spc) * 512u;
}

static void make_exfat(const char *path) {
    const uint32_t total = 65536;
    const uint32_t fat_offset = 24;
    const uint32_t fat_length = 256;
    const uint32_t heap = 512;
    const uint32_t clusters = total - heap;
    FILE *f = fopen(path, "wb");
    set_size(f, (uint64_t)total * 512u);
    uint8_t bs[512];
    memset(bs, 0, sizeof(bs));
    bs[0] = 0xEB; bs[1] = 0x76; bs[2] = 0x90;
    memcpy(bs + 3, "EXFAT   ", 8);
    le64(bs + 72, total);
    le32(bs + 80, fat_offset);
    le32(bs + 84, fat_length);
    le32(bs + 88, heap);
    le32(bs + 92, clusters);
    le32(bs + 96, 3);
    le32(bs + 100, 0xABCDEF01);
    le16(bs + 104, 0x0100);
    bs[108] = 9;
    bs[109] = 0;
    bs[110] = 1;
    bs[111] = 0x80;
    bs[510] = 0x55; bs[511] = 0xAA;
    write_at(f, 0, bs, sizeof(bs));

    uint8_t fat[2048];
    memset(fat, 0, sizeof(fat));
    le32(fat + 0, 0xFFFFFFF8);
    le32(fat + 4, 0xFFFFFFFF);
    le32(fat + 2 * 4, 0xFFFFFFFF);
    le32(fat + 3 * 4, 0xFFFFFFFF);
    write_at(f, (uint64_t)fat_offset * 512u, fat, sizeof(fat));

    uint8_t bitmap[512];
    memset(bitmap, 0, sizeof(bitmap));
    bitmap[0] = 0x03; /* clusters 2 and 3 allocated */
    write_at(f, exfat_cluster_offset(heap, 1, 2), bitmap, sizeof(bitmap));

    const char *payload = "Recovered from a deleted exFAT directory entry.\r\n";
    uint32_t payload_len = (uint32_t)strlen(payload);

    uint8_t root[512];
    memset(root, 0, sizeof(root));
    root[0] = 0x81;
    le32(root + 20, 2);
    le64(root + 24, sizeof(bitmap));
    static const uint16_t pptx_name[] = {
        0x041F, 0x0440, 0x0435, 0x0437, 0x0435, 0x043D, 0x0442, 0x0430,
        0x0446, 0x0438, 0x044F, 0x002E, 0x0070, 0x0070, 0x0074, 0x0078
    };
    const int pptx_name_len = (int)(sizeof(pptx_name) / sizeof(pptx_name[0]));
    static const uint16_t zero_mp4_name[] = {
        0x007A, 0x0065, 0x0072, 0x006F, 0x002E, 0x006D, 0x0070, 0x0034
    };
    const int zero_mp4_name_len = (int)(sizeof(zero_mp4_name) / sizeof(zero_mp4_name[0]));

    uint8_t *file = root + 32;
    file[0] = 0x05;
    file[1] = 3;
    le16(file + 4, 0x20);
    uint8_t *stream = root + 64;
    stream[0] = 0x40;
    stream[1] = 0x02;
    stream[3] = (uint8_t)pptx_name_len;
    le64(stream + 8, payload_len);
    le32(stream + 20, 10);
    le64(stream + 24, payload_len);
    utf16_name_units(root + 96, pptx_name, 0, pptx_name_len);
    utf16_name_units(root + 128, pptx_name, 15, pptx_name_len);

    uint8_t *zero_file = root + 160;
    zero_file[0] = 0x05;
    zero_file[1] = 2;
    le16(zero_file + 4, 0x20);
    uint8_t *zero_stream = root + 192;
    zero_stream[0] = 0x40;
    zero_stream[1] = 0x02;
    zero_stream[3] = (uint8_t)zero_mp4_name_len;
    le64(zero_stream + 8, 0);
    le32(zero_stream + 20, 0);
    le64(zero_stream + 24, 0);
    utf16_name_units(root + 224, zero_mp4_name, 0, zero_mp4_name_len);
    write_at(f, exfat_cluster_offset(heap, 1, 3), root, sizeof(root));

    write_at(f, exfat_cluster_offset(heap, 1, 10), payload, strlen(payload));
    fclose(f);
}

int main(void) {
    MKDIR("tests");
    make_fat16("tests/fat16.img");
    make_fat32("tests/fat32.img");
    make_exfat("tests/exfat.img");
    puts("wrote tests/fat16.img tests/fat32.img tests/exfat.img");
    return 0;
}
