/* Zhaoxu OS - ZhaoxuBootMgr (UEFI first stage).
 *
 * Reads \EFI\BOOT\zhaoxu.cfg from the boot volume, then loads and starts the
 * second stage, \EFI\BOOT\OSLDRX64.efi, which owns the kernel hand-off. The
 * kernel itself never lives in BOOTX64.EFI (rule 25).
 */
#include "uefi.h"

#define CFG_MAX   (64 * 1024)
#define STAGE_MAX (4 * 1024 * 1024)

static EFI_SYSTEM_TABLE *g_st;
static EFI_BOOT_SERVICES *g_bs;

static void serial_char(char c)
{
    for (UINT32 spin = 0; spin < 100000; spin++) {
        UINT8 lsr;
        __asm__ volatile("inb %1, %0" : "=a"(lsr) : "Nd"((UINT16)0x3FD));
        if (lsr & 0x20)
            break;
    }
    __asm__ volatile("outb %0, %1" ::"a"(c), "Nd"((UINT16)0x3F8));
}

static void print(const char *text)
{
    while (*text)
        serial_char(*text++);
}

static void print_con(const char *text)
{
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *out;
    CHAR16 pair[2];
    if (!g_st || !(out = g_st->ConOut) || !out->OutputString)
        return;
    while (*text) {
        pair[0] = (CHAR16)(UINT8)*text++;
        pair[1] = 0;
        out->OutputString(out, pair);
    }
}

static __attribute__((noreturn)) void halt(void)
{
    __asm__ volatile("cli");
    for (;;)
        __asm__ volatile("hlt");
}

static EFI_STATUS open_boot_volume(EFI_HANDLE image_handle,
                                   EFI_FILE_PROTOCOL **root)
{
    static const EFI_GUID loaded_guid = ZHAOXU_LOADED_IMAGE_GUID;
    static const EFI_GUID fs_guid = ZHAOXU_SIMPLE_FS_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *image = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_STATUS status;

    status = g_bs->HandleProtocol(image_handle, &loaded_guid, (void **)&image);
    if (EFI_ERROR(status) || !image || !image->DeviceHandle)
        return 0x8000000000000002ULL;
    status = g_bs->HandleProtocol(image->DeviceHandle, &fs_guid, (void **)&fs);
    if (EFI_ERROR(status) || !fs)
        return 0x8000000000000002ULL;
    return fs->OpenVolume(fs, root);
}

static UINTN read_whole_file(EFI_FILE_PROTOCOL *root, const CHAR16 *path,
                             UINTN capacity, void **buffer)
{
    EFI_FILE_PROTOCOL *file = 0;
    void *data = 0;
    UINTN size = capacity;
    EFI_STATUS status;

    status = root->Open(root, &file, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || !file)
        return 0;
    status = g_bs->AllocatePool(EFI_LOADER_DATA, capacity, &data);
    if (EFI_ERROR(status) || !data) {
        file->Close(file);
        return 0;
    }
    status = file->Read(file, &size, data);
    file->Close(file);
    if (EFI_ERROR(status) || !size) {
        g_bs->FreePool(data);
        return 0;
    }
    *buffer = data;
    return size;
}

EFI_STATUS EFIAPI efi_start(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st)
{
    static const CHAR16 cfg_path[] = L"\\EFI\\BOOT\\zhaoxu.cfg";
    static const CHAR16 stage_path[] = L"\\EFI\\BOOT\\OSLDRX64.efi";
    EFI_FILE_PROTOCOL *root = 0;
    void *cfg = 0;
    void *stage = 0;
    UINTN cfg_size;
    UINTN stage_size;
    EFI_HANDLE stage_handle = 0;
    UINTN exit_size = 0;
    CHAR16 *exit_data = 0;
    EFI_STATUS status;

    g_st = st;
    g_bs = st->BootServices;

    print("ZhaoxuBootMgr (UEFI x86_64)\r\n");
    print_con("ZhaoxuBootMgr (UEFI x86_64)\r\n");

    if (EFI_ERROR(open_boot_volume(image_handle, &root)) || !root) {
        print("BootMgr: cannot open the boot volume\r\n");
        halt();
    }

    cfg_size = read_whole_file(root, cfg_path, CFG_MAX, &cfg);
    if (cfg_size) {
        print("BootMgr: zhaoxu.cfg loaded\r\n");
        g_bs->FreePool(cfg);
    } else {
        print("BootMgr: zhaoxu.cfg missing, using defaults\r\n");
    }

    stage_size = read_whole_file(root, stage_path, STAGE_MAX, &stage);
    if (!stage_size) {
        print("BootMgr: cannot read OSLDRX64.efi\r\n");
        halt();
    }
    print("BootMgr: starting OSLDRX64.efi\r\n");

    status = g_bs->LoadImage(0, image_handle, 0, stage, stage_size,
                             &stage_handle);
    if (EFI_ERROR(status)) {
        print("BootMgr: LoadImage failed\r\n");
        halt();
    }
    status = g_bs->StartImage(stage_handle, &exit_size, &exit_data);
    print("BootMgr: osloader returned\r\n");
    if (exit_size && exit_data)
        g_bs->FreePool(exit_data);
    (void)status;
    halt();
}
