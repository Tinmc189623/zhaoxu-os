/*
 * Zhaoxu OS - self-contained disk installer.
 *
 * No extra installation media is required (rule 27): the UEFI loader comes
 * from the initramfs (/boot/limine/BOOTX64.EFI) and the kernel is the
 * running image.  The installer formats the FAT32 ESP itself and delegates
 * the ext4 root volume to the Ring3 /bin/ext4fs service over the fs RPC
 * channel, then writes either an MBR or a GPT layout (rule 15):
 *
 *   MBR: LBA0 stage1, LBA1 stage2, gap, ESP at LBA 2048, ext4 root after.
 *   GPT: LBA0 stage1 + protective MBR, LBA1/2 header+entries, stage2 at
 *        LBA34, gap, ESP at LBA 2048, ext4 root, backup GPT at the end.
 *
 * Limine 12 reads FAT and ISO9660 only, so KERNEL.ELF, KERNEL.BIN,
 * MiSans.ttf and WALL.RGB live on the ESP; the ext4 root carries the
 * installed filesystem and no boot files.
 */
#include <block.h>
#include <boot.h>
#include <fat32.h>
#include <fs_rpc.h>
#include <install.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <timer.h>
#include <types.h>
#include <vfs.h>

#define SECTOR_SIZE 512
#define CHUNK_SECTORS 8
#define CHUNK_BYTES (CHUNK_SECTORS * SECTOR_SIZE)

#define ESP_DEFAULT_SECTORS 131072u    /* 64 MiB, the automatic layout */
#define ESP_MAX_SECTORS 524288u        /* 256 MiB, custom layout */
#define ESP_MIN_SECTORS 70000u         /* FAT32 needs >= 65525 clusters */
#define EXT4_MIN_SECTORS 65536u        /* 32 MiB: kernel + font + wallpaper */
#define DATA_MIN_SECTORS 131072u       /* 64 MiB FAT32 data volume */
#define KERNEL_MAX_BYTES (64u * 1024u * 1024u)
#define LOADER_MAX_BYTES (16u * 1024u * 1024u)
#define LIMINE_BIOS_HDD_MAX (4u * 1024u * 1024u)
#define FONT_MAX_BYTES (32u * 1024u * 1024u)
#define WALLPAPER_MAX_BYTES (32u * 1024u * 1024u)
/* The ext4/FAT writers and the MBR/GPT fields use 32-bit sector counts. */
#define MAX_DISK_SECTORS 0xFFFFFFFFULL
#define GPT_HEADER_LBA 1u
#define GPT_ENTRIES_LBA 2u
#define GPT_ENTRY_COUNT 128u
#define GPT_ENTRY_SIZE 128u
#define GPT_ENTRY_SECTORS 32u
#define GPT_BIOS_START 34u
#define GPT_BACKUP_SECTORS 33u

#define KERNEL_VMA 0xffffffff80000000ULL
#define KERNEL_MAP_SIZE 0x01000000ULL
#define KERNEL_FILE_HEADER 0x1000u

static const u8 g_guid_esp[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};
static const u8 g_guid_bios[16] = {
    0x48, 0x61, 0x68, 0x21, 0x49, 0x64, 0x6E, 0x6F,
    0x74, 0x4E, 0x65, 0x65, 0x64, 0x45, 0x46, 0x49
};
static const u8 g_guid_disk[16] = {
    0x5A, 0x4F, 0x53, 0x2D, 0x44, 0x49, 0x53, 0x4B,
    0x9A, 0x4F, 0x53, 0x31, 0x00, 0x00, 0x00, 0x01
};
static const u8 g_guid_part[16] = {
    0x5A, 0x4F, 0x53, 0x2D, 0x50, 0x41, 0x52, 0x54,
    0x9A, 0x4F, 0x53, 0x31, 0x00, 0x00, 0x00, 0x02
};
static const u8 g_guid_root[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x84, 0x83, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};
/* Microsoft basic data, used for the optional FAT32 ZHAOXUDATA partition. */
static const u8 g_guid_data[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x43, 0x33,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

static u8 g_sector[SECTOR_SIZE];
static u8 g_gpt_entries[GPT_ENTRY_COUNT * GPT_ENTRY_SIZE];
static u8 g_chunk[CHUNK_BYTES];
static u8 g_zero[CHUNK_BYTES];
static char g_error[80];

static int write_blob(int disk, u64 lba, const u8 *data, u64 bytes);

static void set_error(const char *text)
{
    usize i = 0;
    for (; text[i] && i + 1 < sizeof(g_error); i++)
        g_error[i] = text[i];
    g_error[i] = 0;
}

const char *install_last_error(void)
{
    return g_error;
}

static void label_from_sector(const u8 *sector, char label[12])
{
    label[0] = 0;
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return;
    if (sector[0] != 0xEB && sector[0] != 0xE9)
        return;
    u32 bytes_per_sector = (u32)sector[11] | ((u32)sector[12] << 8);
    if (bytes_per_sector != SECTOR_SIZE)
        return;

    u32 offset = 0;
    if (sector[0x36] == 'F' && sector[0x37] == 'A' &&
        sector[0x38] == 'T' && sector[0x39] == '1')
        offset = 0x2B;
    else if (sector[0x52] == 'F' && sector[0x53] == 'A' &&
             sector[0x54] == 'T' && sector[0x55] == '3')
        offset = 0x47;
    else
        return;

    int length = 11;
    while (length > 0 && sector[offset + length - 1] == ' ')
        length--;
    for (int i = 0; i < length; i++)
        label[i] = (char)sector[offset + i];
    label[length] = 0;
}

static int sector_is_blank(const u8 *sector)
{
    for (int i = 0; i < SECTOR_SIZE; i++)
        if (sector[i])
            return 0;
    return 1;
}

static u32 get_le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static void put_le16(u8 *p, u16 value)
{
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
}

static void put_le32(u8 *p, u32 value)
{
    p[0] = (u8)(value & 0xFF);
    p[1] = (u8)((value >> 8) & 0xFF);
    p[2] = (u8)((value >> 16) & 0xFF);
    p[3] = (u8)((value >> 24) & 0xFF);
}

static void put_le64(u8 *p, u64 value)
{
    put_le32(p, (u32)value);
    put_le32(p + 4, (u32)(value >> 32));
}

/* Wrap the in-memory kernel image in a one-segment ELF so the UEFI loader
 * can validate and relocate it.  The image itself starts at KERNEL_VMA. */
static u8 *build_kernel_elf(const u8 *image, u64 size, u64 *file_size)
{
    u64 total = KERNEL_FILE_HEADER + size;
    u8 *file = kmalloc(total);
    if (!file)
        return NULL;
    memset(file, 0, KERNEL_FILE_HEADER);

    file[0] = 0x7F;
    file[1] = 'E';
    file[2] = 'L';
    file[3] = 'F';
    file[4] = 2;
    file[5] = 1;
    file[6] = 1;
    put_le16(file + 16, 2);
    put_le16(file + 18, 62);
    put_le32(file + 20, 1);
    put_le64(file + 24, KERNEL_VMA);
    put_le64(file + 32, 64);
    put_le16(file + 52, 64);
    put_le16(file + 54, 56);
    put_le16(file + 56, 1);

    u8 *header = file + 64;
    put_le32(header + 0, 1);
    put_le32(header + 4, 7);
    put_le64(header + 8, KERNEL_FILE_HEADER);
    put_le64(header + 16, KERNEL_VMA);
    put_le64(header + 24, KERNEL_VMA);
    put_le64(header + 32, size);
    put_le64(header + 40, KERNEL_MAP_SIZE);
    put_le64(header + 48, 0x1000);

    memcpy(file + KERNEL_FILE_HEADER, image, (usize)size);
    *file_size = total;
    return file;
}

/* A previous Zhaoxu install: our MBR (0xEF/0x0C/0x0E first partition at
 * LBA 2048) or any GPT/BIOS disk already carrying Limine's boot sector.
 * The full stage1 bytes are not compared: bootloader builds legitimately
 * differ between releases and an older install must stay a valid target. */
static int sector_is_our_mbr(const u8 *sector)
{
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return 0;
    /* Limine's boot sector: EB 3C 90 followed by "LIMINE". */
    if (sector[0] == 0xEB && sector[1] == 0x3C && sector[2] == 0x90 &&
        memcmp(sector + 3, "LIMINE", 6) == 0)
        return 1;
    u8 type = sector[446 + 4];
    if (type == 0xEF || type == 0x0C || type == 0x0E)
        return get_le32(sector + 446 + 8) == INSTALL_PARTITION_LBA;
    if (type == 0xEE) {
        static const u8 marker[5] = {'Z', 'S', 'L', 'B', 'A'};
        for (usize i = 0; i + 5 <= 446; i++) {
            if (memcmp(sector + i, marker, 5) == 0)
                return 1;
        }
        return 0;
    }
    return 0;
}

int install_scan(struct install_disk_info *out, int capacity)
{
    int count = 0;
    int disks = block_disk_count();
    for (int i = 0; i < disks && count < capacity; i++) {
        struct install_disk_info *info = &out[count++];
        memset(info, 0, sizeof(*info));
        info->disk = i;
        info->port = block_disk_port(i);
        info->sectors = block_disk_sectors(i);
        const char *model = block_disk_model(i);
        usize n = 0;
        while (model[n] && n + 1 < sizeof(info->model)) {
            info->model[n] = model[n];
            n++;
        }
        info->model[n] = 0;

        int blank = 0;
        int installed = 0;
        if (block_disk_read(i, 0, 1, g_sector) == 0) {
            blank = sector_is_blank(g_sector);
            installed = !blank && sector_is_our_mbr(g_sector);
            label_from_sector(g_sector, info->label);
        }
        info->is_esp = strcmp(info->label, "ZHAOXUEFI") == 0;
        info->is_data = strcmp(info->label, "ZHAOXUDATA") == 0 ||
                        strcmp(info->label, "ZHAOXUROOT") == 0;
        info->is_target = !info->is_esp && !info->is_data &&
                          (blank || installed) &&
                          info->sectors >=
                              INSTALL_PARTITION_LBA + ESP_MIN_SECTORS +
                                  EXT4_MIN_SECTORS;
    }
    return count;
}

int install_pick_target(void)
{
    struct install_disk_info disks[8];
    int count = install_scan(disks, 8);
    for (int i = 0; i < count; i++) {
        if (disks[i].is_target)
            return disks[i].disk;
    }
    return -1;
}

static u32 crc32(const u8 *data, usize length)
{
    u32 crc = 0xFFFFFFFFu;
    for (usize i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (u32)(-(int)(crc & 1)));
    }
    return ~crc;
}

/* Build the Limine config written to the installed ESP.  Limine 12 reads
 * FAT and ISO9660 only, so the kernel and its modules live on the ESP:
 * partition 2 on GPT (BIOS boot, ESP, root) and partition 1 on MBR. */
static int build_limine_config(char *buffer, usize capacity, int gpt,
                               int font_present, int wallpaper_present)
{
    int esp_partition = gpt ? 2 : 1;
    char modules[3][96];
    int count = 0;
    ksnprintf(modules[count++], sizeof(modules[0]),
              "    module_path: boot(%d):/KERNEL.BIN\n", esp_partition);
    if (font_present)
        ksnprintf(modules[count++], sizeof(modules[0]),
                  "    module_path: boot(%d):/MiSans.ttf\n", esp_partition);
    if (wallpaper_present)
        ksnprintf(modules[count++], sizeof(modules[0]),
                  "    module_path: boot(%d):/WALL.RGB\n", esp_partition);

    int written = ksnprintf(
        buffer, capacity,
        "timeout: 0\n"
        "serial: yes\n\n"
        "/Zhaoxu OS\n"
        "    protocol: limine\n"
        "    path: boot(%d):/KERNEL.ELF\n"
        "%s%s%s"
        "    resolution: 1920x1080x32\n",
        esp_partition, modules[0],
        count > 1 ? modules[1] : "", count > 2 ? modules[2] : "");
    return written > 0 && (usize)written < capacity ? 0 : -1;
}

/* Install Limine's BIOS boot sector and stage 2.  The MBR stage 2 lives in
 * the post-MBR gap at byte 4096; GPT uses the BIOS boot partition at LBA 34.
 * Partition-table bytes are preserved exactly like `limine bios-install`. */
static int write_limine_bios(int disk, const u8 *image, u64 image_size,
                             int gpt)
{
    if (!image || image_size <= SECTOR_SIZE)
        return -1;
    u64 stage2_bytes = image_size - SECTOR_SIZE;
    u64 stage2_lba = gpt ? GPT_BIOS_START : 8;
    u64 stage2_sectors = (stage2_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (stage2_lba + stage2_sectors > INSTALL_PARTITION_LBA)
        return -1;

    if (block_disk_read(disk, 0, 1, g_sector) != 0)
        return -1;
    memcpy(g_sector, image, 440);
    g_sector[510] = image[510];
    g_sector[511] = image[511];
    put_le64(g_sector + 0x1A4, stage2_lba * SECTOR_SIZE);
    if (block_disk_write(disk, 0, 1, g_sector) != 0)
        return -1;
    return write_blob(disk, stage2_lba, image + SECTOR_SIZE, stage2_bytes);
}

static int write_blob(int disk, u64 lba, const u8 *data, u64 bytes)
{
    u64 offset = 0;
    while (offset < bytes) {
        u64 left = bytes - offset;
        u32 chunk = (u32)((left + SECTOR_SIZE - 1) / SECTOR_SIZE);
        if (chunk > CHUNK_SECTORS)
            chunk = CHUNK_SECTORS;
        u32 padded = chunk * SECTOR_SIZE;
        if (left < padded) {
            memset(g_chunk, 0, padded);
            memcpy(g_chunk, data + offset, (usize)left);
            if (block_disk_write(disk, lba + offset / SECTOR_SIZE, chunk,
                                g_chunk) != 0)
                return -1;
        } else if (block_disk_write(disk, lba + offset / SECTOR_SIZE, chunk,
                                   data + offset) != 0) {
            return -1;
        }
        offset += padded;
    }
    return 0;
}

static int zero_gap(int disk, u64 from, u64 to)
{
    memset(g_zero, 0, sizeof(g_zero));
    for (u64 lba = from; lba < to; lba += CHUNK_SECTORS) {
        u32 chunk = CHUNK_SECTORS;
        if (to - lba < chunk)
            chunk = (u32)(to - lba);
        if (block_disk_write(disk, lba, chunk, g_zero) != 0)
            return -1;
    }
    return 0;
}

static void mbr_entry(u8 *entry, u8 type, u64 start, u32 sectors)
{
    entry[0] = 0x00;
    entry[1] = 0xFE;
    entry[2] = 0xFF;
    entry[3] = 0xFF;
    entry[4] = type;
    entry[5] = 0xFE;
    entry[6] = 0xFF;
    entry[7] = 0xFF;
    put_le32(entry + 8, (u32)start);
    put_le32(entry + 12, sectors);
}

static int write_mbr(int target, u64 total, u32 esp_sectors,
                     u32 root_sectors, u64 data_sectors, int gpt)
{
    memset(g_sector, 0, SECTOR_SIZE);
    u8 *entry = g_sector + 446;
    if (gpt) {
        entry[0] = 0x00;
        entry[1] = 0xFF;
        entry[2] = 0xFF;
        entry[3] = 0xFF;
        entry[4] = 0xEE;
        entry[5] = 0xFF;
        entry[6] = 0xFF;
        entry[7] = 0xFF;
        put_le32(entry + 8, 1);
        u64 size = total > 1 ? total - 1 : 1;
        if (size > 0xFFFFFFFFu)
            size = 0xFFFFFFFFu;
        put_le32(entry + 12, (u32)size);
    } else {
        mbr_entry(entry, 0xEF, INSTALL_PARTITION_LBA, esp_sectors);
        entry[0] = 0x80;
        u64 root_start = (u64)INSTALL_PARTITION_LBA + esp_sectors;
        mbr_entry(entry + 16, 0x83, root_start, root_sectors);
        if (data_sectors) {
            mbr_entry(entry + 32, 0x0C, root_start + root_sectors,
                      data_sectors);
        }
    }
    g_sector[510] = 0x55;
    g_sector[511] = 0xAA;
    return block_disk_write(target, 0, 1, g_sector);
}

static void gpt_entry(u8 *entry, const u8 type[16], const u8 unique[16],
                      u64 first, u64 last)
{
    memset(entry, 0, GPT_ENTRY_SIZE);
    memcpy(entry, type, 16);
    memcpy(entry + 16, unique, 16);
    put_le64(entry + 32, first);
    put_le64(entry + 40, last);
}

static int write_gpt_header(int disk, u64 lba, u64 current, u64 backup,
                            u64 entries_lba, u64 first_usable,
                            u64 last_usable, u32 entries_crc)
{
    memset(g_sector, 0, SECTOR_SIZE);
    memcpy(g_sector, "EFI PART", 8);
    put_le32(g_sector + 8, 0x00010000);
    put_le32(g_sector + 12, 92);
    put_le64(g_sector + 24, current);
    put_le64(g_sector + 32, backup);
    put_le64(g_sector + 40, first_usable);
    put_le64(g_sector + 48, last_usable);
    memcpy(g_sector + 56, g_guid_disk, 16);
    put_le64(g_sector + 72, entries_lba);
    put_le32(g_sector + 80, GPT_ENTRY_COUNT);
    put_le32(g_sector + 84, GPT_ENTRY_SIZE);
    put_le32(g_sector + 88, entries_crc);
    put_le32(g_sector + 16, crc32(g_sector, 92));
    return block_disk_write(disk, lba, 1, g_sector);
}

static int write_gpt(int disk, u64 total, u32 esp_sectors, u32 root_sectors,
                     u64 data_sectors)
{
    u64 esp_start = INSTALL_PARTITION_LBA;
    u64 esp_end = esp_start + esp_sectors - 1;
    u64 root_start = esp_end + 1;
    u64 root_end = root_start + root_sectors - 1;

    memset(g_gpt_entries, 0, sizeof(g_gpt_entries));
    gpt_entry(g_gpt_entries, g_guid_bios, g_guid_part, GPT_BIOS_START,
              INSTALL_PARTITION_LBA - 1);
    gpt_entry(g_gpt_entries + GPT_ENTRY_SIZE, g_guid_esp, g_guid_part,
              esp_start, esp_end);
    gpt_entry(g_gpt_entries + 2 * GPT_ENTRY_SIZE, g_guid_root, g_guid_part,
              root_start, root_end);
    if (data_sectors) {
        gpt_entry(g_gpt_entries + 3 * GPT_ENTRY_SIZE, g_guid_data,
                  g_guid_part, root_end + 1, root_end + data_sectors);
    }

    u32 entries_crc = crc32(g_gpt_entries, sizeof(g_gpt_entries));
    u64 backup_entries = total - GPT_BACKUP_SECTORS;
    u64 last_usable = backup_entries - 1;

    if (write_blob(disk, GPT_ENTRIES_LBA, g_gpt_entries,
                   sizeof(g_gpt_entries)) != 0)
        return -1;
    if (write_blob(disk, backup_entries, g_gpt_entries,
                   sizeof(g_gpt_entries)) != 0)
        return -1;
    /* Both headers carry the same raw LBAs: first usable right after the
     * 128 entries, last usable just before the backup entries. */
    if (write_gpt_header(disk, GPT_HEADER_LBA, GPT_HEADER_LBA, total - 1,
                         GPT_ENTRIES_LBA, GPT_BIOS_START, last_usable,
                         entries_crc) != 0)
        return -1;
    return write_gpt_header(disk, total - 1, total - 1, GPT_HEADER_LBA,
                            backup_entries, GPT_BIOS_START, last_usable,
                            entries_crc);
}

static int read_boot_verify(int disk)
{
    if (block_disk_read(disk, 0, 1, g_sector) != 0) {
        set_error("verification failed: boot sector is unreadable");
        return -1;
    }
    /* Limine's MBR code starts with EB 3C 90 and the signature string
     * "LIMINE" begins at offset 3, immediately after the jump stub. */
    if (g_sector[510] != 0x55 || g_sector[511] != 0xAA ||
        memcmp(g_sector + 3, "LIMINE", 6) != 0) {
        set_error("verification failed: Limine boot sector differs");
        return -1;
    }
    return 0;
}

/* Publish one coarse stage to the installer UI and to the serial log; the
 * tick count makes a slow stage visible without attaching a debugger. */
static void stage_report(void (*progress)(void *context, u32 done, u32 total),
                         void *context, u32 *step, u32 steps,
                         const char *name)
{
    (*step)++;
    if (progress)
        progress(context, *step, steps);
    kprintf("[install] stage %u/%u %s t=%llu\n", *step, steps, name,
            (unsigned long long)timer_ticks());
}

/* Every failure after the ext4 FORMAT must leave the service in a clean
 * state: one SYNC commits the chunks already sent and restores the volume
 * that was mounted before the install (ext4fs does the restore on SYNC).
 * The three heap buffers are freed in exactly one place each. */
int install_run(int target_disk, int gpt, const struct install_layout *layout,
                void (*progress)(void *context, u32 done, u32 total),
                void *context)
{
#if HEADLESS
    /* The installed stage2 and loader are copies of this build, so a
     * headless install produces a headless system: no Aqua Desktop
     * Environment, 8sh only. */
    kprintf("[install] warning: headless build; the installed system will "
            "not start Aqua Desktop Environment\n");
#endif

    if (target_disk < 0) {
        set_error("invalid target disk");
        return -1;
    }

    struct install_disk_info disks[8];
    int count = install_scan(disks, 8);
    int is_target = 0;
    for (int i = 0; i < count; i++) {
        if (disks[i].disk == target_disk && disks[i].is_target)
            is_target = 1;
    }
    if (!is_target) {
        set_error("target is not a blank Zhaoxu target disk");
        return -1;
    }

    u64 total = block_disk_sectors(target_disk);
    if (total == 0 || total > MAX_DISK_SECTORS) {
        set_error("disk size is not supported (max 2 TiB)");
        return -1;
    }
    u64 usable = total - INSTALL_PARTITION_LBA -
                 (gpt ? GPT_BACKUP_SECTORS : 0);
    u64 data_sectors = layout ? layout->data_sectors : 0;
    u32 esp_sectors = layout ? layout->esp_sectors : ESP_DEFAULT_SECTORS;

    if (layout) {
        if (esp_sectors < ESP_MIN_SECTORS || esp_sectors > ESP_MAX_SECTORS) {
            set_error("ESP size is outside 34..256 MiB");
            return -1;
        }
        if (data_sectors && data_sectors < DATA_MIN_SECTORS) {
            set_error("data partition must be at least 64 MiB");
            return -1;
        }
        if (data_sectors > 0xFFFFFFFFULL) {
            set_error("data partition exceeds the FAT32 sector range");
            return -1;
        }
    }
    if (usable < ESP_MIN_SECTORS + EXT4_MIN_SECTORS + data_sectors) {
        set_error("target disk is too small for this layout");
        return -1;
    }
    if (!layout && (u64)esp_sectors > usable - EXT4_MIN_SECTORS)
        esp_sectors = (u32)(usable - EXT4_MIN_SECTORS);
    u64 root_sectors_wide = usable - esp_sectors - data_sectors;
    if (root_sectors_wide > 0xFFFFFFFFULL) {
        set_error("root partition exceeds the ext4 sector range");
        return -1;
    }
    u32 root_sectors = (u32)root_sectors_wide;
    u64 root_lba = INSTALL_PARTITION_LBA + esp_sectors;
    u64 data_lba = root_lba + root_sectors;
    kprintf("[install] esp %u sectors, ext4 root %u sectors @ %llu, "
            "data %llu sectors @ %llu (target %llu sectors, %s)\n",
            esp_sectors, root_sectors, (unsigned long long)root_lba,
            (unsigned long long)data_sectors,
            (unsigned long long)data_lba, (unsigned long long)total,
            gpt ? "GPT" : "MBR");

    const struct boot_info *bi = boot_get_info();
    const u8 *kernel = (const u8 *)(uintptr_t)(bi->boot_image_phys
                                                    ? bi->boot_image_phys
                                                    : bi->kernel_phys_base);
    u64 kernel_bytes = bi->boot_image_size ? bi->boot_image_size
                                           : bi->kernel_size;
    if (!kernel || kernel_bytes == 0 || kernel_bytes > KERNEL_MAX_BYTES) {
        set_error("running kernel image is unavailable or too large");
        return -1;
    }

    u8 *loader = NULL;
    u64 loader_bytes = 0;
    u8 *bios_sys = NULL;
    u64 bios_sys_bytes = 0;
    u8 *bios_hdd = NULL;
    u64 bios_hdd_bytes = 0;
    u8 *kernel_file = NULL;
    u64 kernel_file_size = 0;
    u8 *font_copy = NULL;
    int fs_dirty = 0;
    char limine_config[512];

    if (vfs_read_all("/boot/limine/BOOTX64.EFI", &loader, &loader_bytes) != 0 ||
        !loader || !loader_bytes || loader_bytes > LOADER_MAX_BYTES) {
        set_error("embedded Limine BOOTX64.EFI is missing or too large");
        goto fail;
    }
    if (vfs_read_all("/boot/limine/limine-bios.sys", &bios_sys,
                     &bios_sys_bytes) != 0 ||
        !bios_sys || !bios_sys_bytes || bios_sys_bytes > LOADER_MAX_BYTES) {
        set_error("embedded limine-bios.sys is missing or too large");
        goto fail;
    }
    if (vfs_read_all("/boot/limine/limine-bios-hdd.bin", &bios_hdd,
                     &bios_hdd_bytes) != 0 ||
        !bios_hdd || bios_hdd_bytes <= SECTOR_SIZE ||
        bios_hdd_bytes > LIMINE_BIOS_HDD_MAX) {
        set_error("embedded Limine BIOS image is missing or too large");
        goto fail;
    }
    kernel_file = build_kernel_elf(kernel, kernel_bytes, &kernel_file_size);
    if (!kernel_file || kernel_file_size > KERNEL_MAX_BYTES) {
        set_error("cannot build the kernel ELF image");
        goto fail;
    }

    const struct boot_info *info = boot_get_info();
    const u8 *font = (const u8 *)(uintptr_t)info->font_phys;
    u64 font_size = info->font_size;
    if (font && font_size > FONT_MAX_BYTES) {
        set_error("boot font image is too large");
        goto fail;
    }
    if (!font || font_size < 13) {
        static const char *const font_candidates[] = {
            "/mnt/MiSans-Regular.ttf", "/mnt/MiSansGlobal-Regular.ttf",
            "/mnt/Inter-Regular.otf"
        };
        font = NULL;
        font_size = 0;
        for (u32 i = 0;
             i < sizeof(font_candidates) / sizeof(font_candidates[0]); i++) {
            u64 read = 0;
            if (vfs_read_all(font_candidates[i], &font_copy, &read) == 0 &&
                read >= 13) {
                font = font_copy;
                font_size = read;
                break;
            }
        }
    }

    const u8 *wallpaper = (const u8 *)(uintptr_t)info->wallpaper_phys;
    u64 wallpaper_size = info->wallpaper_size;
    if (wallpaper && wallpaper_size > WALLPAPER_MAX_BYTES) {
        set_error("boot wallpaper image is too large");
        goto fail;
    }
    int font_present = font && font_size >= 13;
    int wallpaper_present = wallpaper &&
                            wallpaper_size >= 1920u * 1080u * 3u;
    if (build_limine_config(limine_config, sizeof(limine_config), gpt,
                            font_present, wallpaper_present) != 0) {
        set_error("cannot build limine.conf");
        goto fail;
    }

    /* Everything Limine must read goes on the ESP: it has no ext4 driver,
     * so the root volume only carries the filesystem, not boot files. */
    struct fat32_boot_file boot_files[4];
    u32 boot_file_count = 0;
    boot_files[boot_file_count].path = "/KERNEL.ELF";
    boot_files[boot_file_count].data = kernel_file;
    boot_files[boot_file_count].size = kernel_file_size;
    boot_file_count++;
    boot_files[boot_file_count].path = "/KERNEL.BIN";
    boot_files[boot_file_count].data = kernel;
    boot_files[boot_file_count].size = kernel_bytes;
    boot_file_count++;
    if (font_present) {
        boot_files[boot_file_count].path = "/MiSans.ttf";
        boot_files[boot_file_count].data = font;
        boot_files[boot_file_count].size = font_size;
        boot_file_count++;
    }
    if (wallpaper_present) {
        boot_files[boot_file_count].path = "/WALL.RGB";
        boot_files[boot_file_count].data = wallpaper;
        boot_files[boot_file_count].size = wallpaper_size;
        boot_file_count++;
    }

    /* Coarse stages: format, MBR/GPT, Limine BIOS, gap, ESP, optional data
     * format, verify and flush.  Report before the long format so the UI can
     * show a real denominator instead of an indeterminate 0/1. */
    u32 steps = data_sectors ? 8 : 7;
    u32 step = 0;
    if (progress)
        progress(context, step, steps);
    kprintf("[install] start t=%llu, kernel %llu bytes, loader %llu, "
            "bios.sys %llu, bios.hdd %llu\n",
            (unsigned long long)timer_ticks(),
            (unsigned long long)kernel_bytes,
            (unsigned long long)loader_bytes,
            (unsigned long long)bios_sys_bytes,
            (unsigned long long)bios_hdd_bytes);

    if (fs_call_format(target_disk, root_lba, root_sectors,
                       "ZHAOXUROOT") != 0) {
        set_error("ext4 service could not format the root volume");
        goto fail;
    }
    /* The service now has the target mounted: every later failure must
     * SYNC (commit + restore the previous mount) in the cleanup path. */
    fs_dirty = 1;
    if (fs_call_sync() != 0) {
        set_error("ext4 service could not sync the root volume");
        /* The cleanup retries the (idempotent) sync once. */
        goto fail;
    }
    fs_dirty = 0;
    stage_report(progress, context, &step, steps, "root formatted");

    if (write_mbr(target_disk, total, esp_sectors, root_sectors,
                  data_sectors, gpt) != 0) {
        set_error("failed to write the MBR");
        goto fail;
    }
    /* An MBR install must not leave an old backup GPT at the end of the
     * disk; some firmware would then prefer the stale GPT over the new MBR. */
    if (!gpt && zero_gap(target_disk, total - GPT_BACKUP_SECTORS, total) != 0) {
        set_error("failed to clear the old GPT backup");
        goto fail;
    }
    stage_report(progress, context, &step, steps, "partition table written");

    if (gpt && write_gpt(target_disk, total, esp_sectors, root_sectors,
                         data_sectors) != 0) {
        set_error("failed to write the GPT");
        goto fail;
    }

    if (write_limine_bios(target_disk, bios_hdd, bios_hdd_bytes, gpt) != 0) {
        set_error("failed to install Limine BIOS stages");
        goto fail;
    }
    stage_report(progress, context, &step, steps, "Limine BIOS installed");

    u64 stage2_sectors = (bios_hdd_bytes - SECTOR_SIZE + SECTOR_SIZE - 1) /
                         SECTOR_SIZE;
    u64 stage2_lba = gpt ? GPT_BIOS_START : 8;
    if (zero_gap(target_disk, stage2_lba + stage2_sectors,
                 INSTALL_PARTITION_LBA) != 0) {
        set_error("failed to clear the partition gap");
        goto fail;
    }
    stage_report(progress, context, &step, steps, "gap cleared");

    int esp_status = fat32_make_esp(target_disk, INSTALL_PARTITION_LBA,
                                    esp_sectors, loader, loader_bytes,
                                    bios_sys, bios_sys_bytes, limine_config,
                                    boot_files, boot_file_count);
    if (esp_status != 0) {
        if (esp_status == -3)
            set_error("could not format the FAT32 ESP");
        else if (esp_status == -5)
            set_error("could not create the ESP directories");
        else if (esp_status == -6)
            set_error("could not write the Limine EFI loader");
        else if (esp_status == -8)
            set_error("could not write limine-bios.sys");
        else if (esp_status == -7 || esp_status == -9)
            set_error("could not write limine.conf");
        else if (esp_status == -11)
            set_error("could not write the kernel to the ESP");
        else
            set_error("failed to build the FAT32 ESP");
        goto fail;
    }
    stage_report(progress, context, &step, steps, "ESP built");

    if (data_sectors) {
        if (fat32_format_volume(target_disk, data_lba, (u32)data_sectors,
                                "ZHAOXUDATA") != 0) {
            set_error("failed to format the FAT32 data partition");
            goto fail;
        }
        stage_report(progress, context, &step, steps, "data formatted");
    }

    if (read_boot_verify(target_disk) != 0)
        goto fail;   /* read_boot_verify already published the reason */
    if (block_disk_read(target_disk, INSTALL_PARTITION_LBA, 1, g_sector) != 0 ||
        g_sector[510] != 0x55 || g_sector[511] != 0xAA ||
        memcmp(g_sector + 82, "FAT32   ", 8) != 0) {
        set_error("verification failed: ESP is not FAT32");
        goto fail;
    }
    u8 super[1024];
    if (block_disk_read(target_disk, root_lba + 2, 2, super) != 0 ||
        super[0x38] != 0x53 || super[0x39] != 0xEF) {
        set_error("verification failed: root partition is not ext4");
        goto fail;
    }
    if (data_sectors) {
        if (block_disk_read(target_disk, data_lba, 1, g_sector) != 0 ||
            memcmp(g_sector + 82, "FAT32   ", 8) != 0 ||
            memcmp(g_sector + 71, "ZHAOXUDATA", 10) != 0) {
            set_error("verification failed: data partition is not FAT32");
            goto fail;
        }
    }
    stage_report(progress, context, &step, steps, "verified");

    if (block_disk_flush(target_disk) != 0) {
        set_error("failed to flush the target disk");
        goto fail;
    }
    stage_report(progress, context, &step, steps, "flushed");

    g_error[0] = 0;
    kfree(font_copy);
    kfree(kernel_file);
    kfree(bios_hdd);
    kfree(bios_sys);
    kfree(loader);
    return 0;

fail:
    if (fs_dirty)
        fs_call_sync();
    kfree(font_copy);
    kfree(kernel_file);
    kfree(bios_hdd);
    kfree(bios_sys);
    kfree(loader);
    return -1;
}
