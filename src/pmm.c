#include <stdbool.h>
#include "string.h"
#include "limine.h"
#include "serial.h"
#include "pmm.h"

// HHDM request
__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

// memory map request
__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

static const char *pmm_type_to_str(pmm_memmap_type_t type) {
    switch (type) {
        case PMM_MEMMAP_USABLE:                 return "Usable RAM";
        case PMM_MEMMAP_RESERVED:               return "Reserved";
        case PMM_MEMMAP_ACPI_RECLAIMABLE:       return "ACPI Reclaimable";
        case PMM_MEMMAP_ACPI_NVS:               return "ACPI NVS";
        case PMM_MEMMAP_BAD_MEMORY:             return "Bad Memory";
        case PMM_MEMMAP_BOOTLOADER_RECLAIMABLE: return "Bootloader Reclaimable";
        case PMM_MEMMAP_KERNEL_AND_MODULES:     return "Kernel/Modules";
        case PMM_MEMMAP_FRAMEBUFFER:            return "Framebuffer";
        default:                                return "Unknown";
    }
}

static uint8_t *bitmap = NULL;
static uint64_t num_frames = 0;
uint64_t hhdm_offset = 0;

static inline void bitmap_set(uint64_t frame_idx) {
    bitmap[frame_idx >> 3] |= (uint8_t)(1u << (frame_idx & 7));
}

static inline void bitmap_clear(uint64_t frame_idx) {
    bitmap[frame_idx >> 3] &= (uint8_t)~(1u << (frame_idx & 7));
}

static inline bool bitmap_test(uint64_t frame_idx) {
    return (bitmap[frame_idx >> 3] & (1u << (frame_idx & 7))) != 0;
}

static void bitmap_set_range(uint64_t start_frame, uint64_t count) {
    if (count == 0) return;

    uint64_t end_frame = start_frame + count;

    if ((start_frame >> 3) == ((end_frame - 1) >> 3)) {
        uint8_t mask = ((1u << (end_frame - start_frame)) - 1) << (start_frame & 7);
        bitmap[start_frame >> 3] |= mask;
        return;
    }

    uint64_t start_byte = start_frame >> 3;
    uint64_t end_byte   = end_frame >> 3;

    if (start_frame & 7) {
        uint8_t head_mask = (uint8_t)(0xFFu << (start_frame & 7));
        bitmap[start_byte++] |= head_mask;
    }

    if (end_byte > start_byte) {
        memset(&bitmap[start_byte], 0xFF, end_byte - start_byte);
    }

    if (end_frame & 7) {
        uint8_t tail_mask = (uint8_t)((1u << (end_frame & 7)) - 1);
        bitmap[end_byte] |= tail_mask;
    }
}

static void bitmap_clear_range(uint64_t start_frame, uint64_t count) {
    if (count == 0) return;

    uint64_t end_frame = start_frame + count;

    if ((start_frame >> 3) == ((end_frame - 1) >> 3)) {
        uint8_t mask = ((1u << (end_frame - start_frame)) - 1) << (start_frame & 7);
        bitmap[start_frame >> 3] &= (uint8_t)~mask;
        return;
    }

    uint64_t start_byte = start_frame >> 3;
    uint64_t end_byte   = end_frame >> 3;

    if (start_frame & 7) {
        uint8_t head_mask = (uint8_t)(0xFFu << (start_frame & 7));
        bitmap[start_byte++] &= (uint8_t)~head_mask;
    }

    if (end_byte > start_byte) {
        memset(&bitmap[start_byte], 0x00, end_byte - start_byte);
    }

    if (end_frame & 7) {
        uint8_t tail_mask = (uint8_t)((1u << (end_frame & 7)) - 1);
        bitmap[end_byte] &= (uint8_t)~tail_mask;
    }
}

void pmm_init(void) {
    if (hhdm_request.response == NULL || memmap_request.response == NULL) {
        kprintf("[-] Fatal: Limine requests failed\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    hhdm_offset = hhdm_request.response->offset;
    kprintf("[debug 1] hhdm_offset=%p\n", hhdm_offset);

    struct limine_memmap_response *mmap = memmap_request.response;
    kprintf("[+] Total memory map entries: %u\n", mmap->entry_count);

    uint64_t highest_address = 0;

    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry *entry = mmap->entries[i];

        kprintf("Region %u: base=%p, len=%x, type=%s\n",
                i, entry->base, entry->length, pmm_type_to_str(entry->type));

        uint64_t top = entry->base + entry->length;
        if (entry->type == PMM_MEMMAP_USABLE && top > highest_address) {
            highest_address = top;
        }
    }

    num_frames = (highest_address + 4095) >> 12;
    uint64_t bmp_size = (num_frames + 7) >> 3;
    kprintf("[debug 2] highest_addr=%p, num_frames=%u, bmp_size=%u\n",
            highest_address, num_frames, bmp_size);

    uint64_t bmp_region = 0;
    bool found_region = false;
    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry *entry = mmap->entries[i];

        if (entry->type == PMM_MEMMAP_USABLE && entry->length >= bmp_size) {
            bmp_region = i;
            found_region = true;
            break;
        }
    }

    if (!found_region) {
        kprintf("[-] Fatal: No usable RAM region big enough for bitmap!\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    uint64_t bmp_phys = mmap->entries[bmp_region]->base;
    bitmap = (uint8_t *)PHYS_TO_VIRT(bmp_phys);
    kprintf("[debug 3] bmp_region=%u, bmp_phys=%p, bitmap_virt=%p\n",
            bmp_region, bmp_phys, bitmap);

    kprintf("[debug 4] Calling memset on bitmap buffer...\n");
    memset(bitmap, 0xff, bmp_size);
    kprintf("[debug 5] Memset succeeded.\n");

    kprintf("[debug 6] Clearing usable RAM ranges...\n");
    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry *entry = mmap->entries[i];

        if (entry->type == PMM_MEMMAP_USABLE) {
            uint64_t start_frame = entry->base >> 12;
            uint64_t frame_count = entry->length >> 12;
            kprintf("  [clearing] region %u: start_frame=%u, count=%u\n", i, start_frame, frame_count);
            bitmap_clear_range(start_frame, frame_count);
        }
    }
    kprintf("[debug 7] Usable ranges cleared.\n");

    kprintf("[debug 8] Locking down frame 0 and bitmap frames...\n");
    bitmap_set(0);

    uint64_t bmp_start_frame = bmp_phys >> 12;
    uint64_t bmp_frame_count = (bmp_size + 4095) >> 12;
    kprintf("  [protecting bitmap] start_frame=%u, count=%u\n", bmp_start_frame, bmp_frame_count);
    bitmap_set_range(bmp_start_frame, bmp_frame_count);

    kprintf("[+] PMM initialization complete!\n");
}