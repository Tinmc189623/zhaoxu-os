#ifndef ZHAOXU_ACPI_H
#define ZHAOXU_ACPI_H

#include <types.h>

struct acpi_info {
    int present;
    u32 rsdp_revision;
    char oem_id[7];
    u32 table_count;
    u32 fadt_revision;
    u16 pm1a_control;
    u16 pm1b_control;
    u32 smi_command;
    u8  slp_typa;
    int slp_typa_valid;
    int reset_supported;
    u8  reset_address_space;
    u8  reset_value;
    u64 reset_address;
};

void acpi_init(u64 rsdp_physical);
const struct acpi_info *acpi_get_info(void);
void acpi_poweroff(void) __attribute__((noreturn));
void acpi_reboot(void) __attribute__((noreturn));

#endif
