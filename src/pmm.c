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

    // the entire range fits within a single byte
    if ((start_frame >> 3) == ((end_frame - 1) >> 3)) {
        uint8_t mask = ((1u << (end_frame - start_frame)) - 1) << (start_frame & 7);
        bitmap[start_frame >> 3] |= mask;
        return;
    }

    uint64_t start_byte = start_frame >> 3;
    uint64_t end_byte   = end_frame >> 3;

    // unaligned leading bits
    if (start_frame & 7) {
        uint8_t head_mask = (uint8_t)(0xFFu << (start_frame & 7));
        bitmap[start_byte++] |= head_mask;
    }

    // bulk aligned bytes via memset
    if (end_byte > start_byte) {
        memset(&bitmap[start_byte], 0xFF, end_byte - start_byte);
    }

    // unaligned trailing bits
    if (end_frame & 7) {
        uint8_t tail_mask = (uint8_t)((1u << (end_frame & 7)) - 1);
        bitmap[end_byte] |= tail_mask;
    }
}

static void bitmap_clear_range(uint64_t start_frame, uint64_t count) {
    if (count == 0) return;

    uint64_t end_frame = start_frame + count;

    // the entire range fits within a single byte
    if ((start_frame >> 3) == ((end_frame - 1) >> 3)) {
        uint8_t mask = ((1u << (end_frame - start_frame)) - 1) << (start_frame & 7);
        bitmap[start_frame >> 3] &= (uint8_t)~mask;
        return;
    }

    uint64_t start_byte = start_frame >> 3;
    uint64_t end_byte   = end_frame >> 3;

    // unaligned leading bits
    if (start_frame & 7) {
        uint8_t head_mask = (uint8_t)(0xFFu << (start_frame & 7));
        bitmap[start_byte++] &= (uint8_t)~head_mask;
    }

    // bulk aligned bytes via memset
    if (end_byte > start_byte) {
        memset(&bitmap[start_byte], 0x00, end_byte - start_byte);
    }

    // unaligned trailing bits
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
    uint64_t bmp_size = ((num_frames + 63) >> 6) * sizeof(uint64_t);

    // we may initialize the region to an actual region and not -1 because such a region is gurrenteed to exist
    uint64_t bmp_region = 0;
    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry *entry = mmap->entries[i];

        if (entry->type == PMM_MEMMAP_USABLE && entry->length >= bmp_size) {
            bmp_region = i;
            break;
        }
    }

    uint64_t bmp_phys = mmap->entries[bmp_region]->base;
    bitmap = (uint8_t *)PHYS_TO_VIRT(bmp_phys);

    // fill bitmap with 1s (used)
    memset(bitmap, 0xff, bmp_size);

    // update usable ram
    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry *entry = mmap->entries[i];

        if (entry->type == PMM_MEMMAP_USABLE) {
            uint64_t start_frame = entry->base >> 12;
            uint64_t frame_count = entry->length >> 12;

            bitmap_clear_range(start_frame, frame_count);
        }
    }

    // set null address space as reserved
    bitmap_set(0);

    // set bitmap frames as reserved
    uint64_t bmp_start_frame = bmp_phys >> 12;
    uint64_t bmp_frame_count = (bmp_size + 4095) >> 12;

    bitmap_set_range(bmp_start_frame, bmp_frame_count);
}

void *pmm_alloc_page(void) {
    uint64_t *words = (uint64_t *)bitmap;
    for (uint64_t i = 0; i < (num_frames + 63) >> 6; i++) {
        if (words[i] != (uint64_t)-1) {
            uint64_t bit = (uint64_t)__builtin_ctzll(~words[i]);
            uint64_t frame_idx = (i << 6) + bit;

            if (frame_idx >= num_frames) {
                return NULL; // out of memory
            }

            // set frame as used
            bitmap_set(frame_idx);

            // convert to virtual address and return
            return (void *)PHYS_TO_VIRT(frame_idx << 12);
        }
    }

    return NULL; // out of memory
}

void pmm_free_page(void *ptr) {
    if (ptr == NULL) {
        return; // return safely on freed null
    }

    uintptr_t virt = (uintptr_t)ptr;

    if (virt < hhdm_offset) {
        return; // non kernel address
    }

    uintptr_t phys = (uintptr_t)VIRT_TO_PHYS(ptr);

    if ((phys & 0xfff) != 0) {
        return; // unaligned pointer
    }

    uint64_t frame_idx = phys >> 12;
    if (frame_idx >= num_frames) {
        return; // invalid frame
    }

    if (bitmap_test(frame_idx) == 0) {
        return; // double free
    }

    // mark page as free
    bitmap_clear(frame_idx);
}