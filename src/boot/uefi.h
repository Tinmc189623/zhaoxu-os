#ifndef ZHAOXU_BOOT_UEFI_H
#define ZHAOXU_BOOT_UEFI_H

/* Minimal UEFI 2.x definitions for the ZhaoxuBootloader C loader.
 * No firmware headers are used; the ABI is Microsoft x64 (ms_abi). */

typedef unsigned char      UINT8;
typedef unsigned short     UINT16;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
typedef signed long long   INT64;
typedef UINT64             UINTN;
typedef UINTN              EFI_STATUS;
typedef void              *EFI_HANDLE;
typedef UINT64             EFI_PHYSICAL_ADDRESS;
typedef UINT64             EFI_VIRTUAL_ADDRESS;
typedef UINT16             CHAR16;

#define EFIAPI __attribute__((ms_abi))

#define EFI_SUCCESS           0ULL
#define EFI_BUFFER_TOO_SMALL  0x8000000000000005ULL
#define EFI_ERROR(s)          ((INT64)(s) < 0)

/* Allocation types and memory types (UEFI 2.x enum order) */
#define EFI_ALLOCATE_ANY_PAGES   0
#define EFI_ALLOCATE_MAX_ADDRESS 1
#define EFI_ALLOCATE_ADDRESS     2
#define EFI_LOADER_DATA          2

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 Crc32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    UINT32 Type;
    UINT32 Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS  VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct {
    EFI_GUID VendorGuid;
    void    *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINT8);
    EFI_STATUS (EFIAPI *OutputString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *,
                                      CHAR16 *);
    void *TestString;
    void *QueryMode;
    void *SetMode;
    void *SetAttribute;
    void *ClearScreen;
    void *SetCursorPosition;
    void *EnableCursor;
    void *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct EFI_DEVICE_PATH_PROTOCOL EFI_DEVICE_PATH_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_IMAGE_LOAD)(
    UINT8 BootPolicy, EFI_HANDLE ParentImageHandle,
    EFI_DEVICE_PATH_PROTOCOL *DevicePath, void *SourceBuffer,
    UINTN SourceSize, EFI_HANDLE *ImageHandle);
typedef EFI_STATUS (EFIAPI *EFI_IMAGE_START)(
    EFI_HANDLE ImageHandle, UINTN *ExitDataSize, CHAR16 **ExitData);

typedef struct {
    EFI_TABLE_HEADER Hdr;                                   /* 0 */
    void *RaiseTPL;                                         /* 24 */
    void *RestoreTPL;                                       /* 32 */
    EFI_STATUS (EFIAPI *AllocatePages)(UINT32 Type, UINT32 MemoryType,
                                       UINTN Pages,
                                       EFI_PHYSICAL_ADDRESS *Memory); /* 40 */
    EFI_STATUS (EFIAPI *FreePages)(EFI_PHYSICAL_ADDRESS Memory,
                                   UINTN Pages);            /* 48 */
    EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN *MemoryMapSize,
                                      EFI_MEMORY_DESCRIPTOR *MemoryMap,
                                      UINTN *MapKey, UINTN *DescriptorSize,
                                      UINT32 *DescriptorVersion); /* 56 */
    EFI_STATUS (EFIAPI *AllocatePool)(UINT32 PoolType, UINTN Size,
                                      void **Buffer);       /* 64 */
    EFI_STATUS (EFIAPI *FreePool)(void *Buffer);            /* 72 */
    void *CreateEvent;                                      /* 80 */
    void *SetTimer;                                         /* 88 */
    void *WaitForEvent;                                     /* 96 */
    void *SignalEvent;                                     /* 104 */
    void *CloseEvent;                                      /* 112 */
    void *CheckEvent;                                      /* 120 */
    void *InstallProtocolInterface;                        /* 128 */
    void *ReinstallProtocolInterface;                      /* 136 */
    void *UninstallProtocolInterface;                      /* 144 */
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE Handle,
                                        const EFI_GUID *Protocol,
                                        void **Interface);   /* 152 */
    void *Reserved;                                        /* 160 */
    void *RegisterProtocolNotify;                          /* 168 */
    void *LocateHandle;                                    /* 176 */
    void *LocateDevicePath;                                /* 184 */
    void *InstallConfigurationTable;                       /* 192 */
    EFI_IMAGE_LOAD LoadImage;                               /* 200 */
    EFI_IMAGE_START StartImage;                             /* 208 */
    void *Exit;                                            /* 216 */
    void *UnloadImage;                                     /* 224 */
    EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE ImageHandle,
                                          UINTN MapKey);    /* 232 */
    void *GetNextMonotonicCount;                           /* 240 */
    void *Stall;                                           /* 248 */
    void *SetWatchdogTimer;                                /* 256 */
    void *ConnectController;                               /* 264 */
    void *DisconnectController;                            /* 272 */
    void *OpenProtocol;                                    /* 280 */
    void *CloseProtocol;                                   /* 288 */
    void *OpenProtocolInformation;                         /* 296 */
    void *ProtocolsPerHandle;                              /* 304 */
    void *LocateHandleBuffer;                              /* 312 */
    EFI_STATUS (EFIAPI *LocateProtocol)(const EFI_GUID *Protocol,
                                        void *Registration,
                                        void **Interface);  /* 320 */
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;                                   /* 0 */
    CHAR16 *FirmwareVendor;                                 /* 24 */
    UINT32 FirmwareRevision;                                /* 32 */
    UINT32 Pad0;                                            /* 36 */
    EFI_HANDLE ConsoleInHandle;                             /* 40 */
    void *ConIn;                                            /* 48 */
    EFI_HANDLE ConsoleOutHandle;                            /* 56 */
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;                /* 64 */
    EFI_HANDLE StandardErrorHandle;                         /* 72 */
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;                /* 80 */
    void *RuntimeServices;                                  /* 88 */
    EFI_BOOT_SERVICES *BootServices;                        /* 96 */
    UINTN NumberOfTableEntries;                            /* 104 */
    EFI_CONFIGURATION_TABLE *ConfigurationTable;           /* 112 */
} EFI_SYSTEM_TABLE;

/* Graphics Output Protocol */
typedef struct {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor = 0,
    PixelBlueGreenRedReserved8BitPerColor = 1,
    PixelBitMask = 2,
    PixelBltOnly = 3,
    PixelFormatMax = 4
} EFI_GRAPHICS_OUTPUT_PIXEL_FORMAT;

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_OUTPUT_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *QueryMode)(
        struct EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32, UINTN *,
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **);
    EFI_STATUS (EFIAPI *SetMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *,
                                 UINT32);
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* GUIDs used by the loader */
#define ZHAOXU_GOP_GUID \
    { 0x9042A9DE, 0x23DC, 0x4A38, { 0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A } }
#define ZHAOXU_ACPI20_GUID \
    { 0x8868E871, 0xE4F1, 0x11D3, { 0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81 } }
#define ZHAOXU_ACPI10_GUID \
    { 0xEB9D2D30, 0x2D88, 0x11D3, { 0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D } }
#define ZHAOXU_LOADED_IMAGE_GUID \
    { 0x5B1B31A1, 0x9562, 0x11D2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

/* Loaded Image Protocol: tells us where the firmware placed this image,
 * so the page tables built below can keep it executable across CR3 switch. */
typedef struct {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    struct EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath;
    void *Reserved;
    UINT32 LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    UINT64 ImageSize;
    UINT32 ImageCodeType;
    UINT32 ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* Simple File System: rule 25 keeps the kernel out of BOOTX64.EFI, so the
 * loader reads \EFI\BOOT\KERNEL.BIN from the boot volume at runtime. */
typedef struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *Open)(struct EFI_FILE_PROTOCOL *This,
                              struct EFI_FILE_PROTOCOL **NewHandle,
                              CHAR16 *FileName, UINT64 OpenMode,
                              UINT64 Attributes);
    EFI_STATUS (EFIAPI *Close)(struct EFI_FILE_PROTOCOL *This);
    EFI_STATUS (EFIAPI *Delete)(struct EFI_FILE_PROTOCOL *This);
    EFI_STATUS (EFIAPI *Read)(struct EFI_FILE_PROTOCOL *This,
                              UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *Write)(struct EFI_FILE_PROTOCOL *This,
                               UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *GetPosition)(struct EFI_FILE_PROTOCOL *This,
                                     UINT64 *Position);
    EFI_STATUS (EFIAPI *SetPosition)(struct EFI_FILE_PROTOCOL *This,
                                     UINT64 Position);
    EFI_STATUS (EFIAPI *GetInfo)(struct EFI_FILE_PROTOCOL *This,
                                 EFI_GUID *InformationType,
                                 UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *SetInfo)(struct EFI_FILE_PROTOCOL *This,
                                 EFI_GUID *InformationType,
                                 UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *Flush)(struct EFI_FILE_PROTOCOL *This);
} EFI_FILE_PROTOCOL;

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(
        struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
        EFI_FILE_PROTOCOL **Root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

#define ZHAOXU_SIMPLE_FS_GUID \
    { 0x964E5B22, 0x6459, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

#define ZHAOXU_BLOCK_IO_GUID \
    { 0x964E5B21, 0x6459, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

typedef UINT64 EFI_LBA;

typedef struct {
    UINT32 MediaId;
    UINT8 RemovableMedia;
    UINT8 MediaPresent;
    UINT8 LogicalPartition;
    UINT8 ReadOnly;
    UINT8 WriteCaching;
    UINT32 BlockSize;
    UINT32 IoAlign;
    EFI_LBA LastBlock;
    EFI_LBA LowestAlignedLba;
    UINT32 LogicalBlocksPerPhysicalBlock;
    UINT32 OptimalTransferLengthGranularity;
} EFI_BLOCK_IO_MEDIA;

typedef struct EFI_BLOCK_IO_PROTOCOL {
    UINT64 Revision;
    EFI_BLOCK_IO_MEDIA *Media;
    EFI_STATUS (EFIAPI *Reset)(struct EFI_BLOCK_IO_PROTOCOL *This,
                               UINT8 ExtendedVerification);
    EFI_STATUS (EFIAPI *ReadBlocks)(struct EFI_BLOCK_IO_PROTOCOL *This,
                                    UINT32 MediaId, EFI_LBA Lba,
                                    UINTN BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *WriteBlocks)(struct EFI_BLOCK_IO_PROTOCOL *This,
                                     UINT32 MediaId, EFI_LBA Lba,
                                     UINTN BufferSize, const void *Buffer);
    EFI_STATUS (EFIAPI *FlushBlocks)(struct EFI_BLOCK_IO_PROTOCOL *This);
} EFI_BLOCK_IO_PROTOCOL;

#define EFI_FILE_MODE_READ 0x0000000000000001ULL

#endif
