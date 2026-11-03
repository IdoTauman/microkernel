#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096

// address translation macros using the Limine HHDM offset
extern uint64_t hhdm_offset;
#define PHYS_TO_VIRT(phys) ((void *)((uintptr_t)(phys) + hhdm_offset))
#define VIRT_TO_PHYS(virt) ((uintptr_t)(virt) - hhdm_offset)

typedef enum {
    PMM_MEMMAP_USABLE                 = 0,
    PMM_MEMMAP_RESERVED               = 1,
    PMM_MEMMAP_ACPI_RECLAIMABLE       = 2,
    PMM_MEMMAP_ACPI_NVS               = 3,
    PMM_MEMMAP_BAD_MEMORY             = 4,
    PMM_MEMMAP_BOOTLOADER_RECLAIMABLE = 5,
    PMM_MEMMAP_KERNEL_AND_MODULES     = 6,
    PMM_MEMMAP_FRAMEBUFFER            = 7
} pmm_memmap_type_t;

void pmm_init(void);
void *pmm_alloc_page(void);
void  pmm_free_page(void *ptr);

#endif