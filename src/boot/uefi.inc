; Zhaoxu OS UEFI constants
; UEFI 2.x x86_64 ABI offsets and GUIDs used by the FASM boot layer.

ST_FirmwareVendor        = 24
ST_FirmwareRevision      = 32
ST_ConsoleInHandle       = 40
ST_ConIn                 = 48
ST_ConsoleOutHandle      = 56
ST_ConOut                = 64
ST_StandardErrorHandle   = 72
ST_StdErr                = 80
ST_RuntimeServices       = 88
ST_BootServices          = 96
ST_NumberOfTableEntries  = 104
ST_ConfigurationTable    = 112

BS_RaiseTPL              = 24
BS_RestoreTPL            = 32
BS_AllocatePages         = 40
BS_FreePages             = 48
BS_GetMemoryMap          = 56
BS_AllocatePool          = 64
BS_FreePool              = 72
BS_Stall                 = 248
BS_ExitBootServices      = 232
BS_LocateProtocol        = 320

EFI_SUCCESS              = 0
EFI_LOADER_DATA          = 2
EFI_CONVENTIONAL_MEMORY  = 7

GOP_MODE                 = 24
GOP_MODE_INFO            = 8
GOP_MODE_INFO_WIDTH      = 4
GOP_MODE_INFO_HEIGHT     = 8
GOP_MODE_INFO_FORMAT     = 12
GOP_MODE_INFO_PPSL       = 32
GOP_MODE_FB_BASE         = 24
GOP_MODE_FB_SIZE         = 32
