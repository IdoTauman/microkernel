#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"
#include "serial.h"
#include "pmm.h"

// set the base revision to 2 (supported by Limine v8/v7)
__attribute__((used, section(".requests")))
volatile LIMINE_BASE_REVISION(2);

// marker for start of requests
__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

// framebuffer request
__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

// marker for end of requests
__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;

// helper function to halt the cpu
__attribute__((noreturn)) static inline void halt(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

// kernel entry point
void kmain(void) {
    // init serial io
    if (serial_init() != 0) {
        halt();
    }

    serial_print("[+] UART COM1 initialized successfully!\n");
    serial_print("[+] Booting microkernel...\n");

    pmm_init();

    // allocate two pages
    void *p1 = pmm_alloc_page();
    void *p2 = pmm_alloc_page();

    kprintf("[*] Allocated p1: %p (phys %p)\n", p1, (void *)VIRT_TO_PHYS(p1));
    kprintf("[*] Allocated p2: %p (phys %p)\n", p2, (void *)VIRT_TO_PHYS(p2));

    // memory write + readback test
    volatile uint64_t *test_ptr = (volatile uint64_t *)p1;
    *test_ptr = 0xCAFEBABE12345678ULL;
    kprintf("[*] Write/Readback p1: %x\n", *test_ptr);

    // free p1 and reallocate (should recycle p1)
    pmm_free_page(p1);
    void *p3 = pmm_alloc_page();
    kprintf("[*] Reallocated p3: %p (matches p1: %s)\n", 
            p3, (p3 == p1) ? "YES" : "NO");

    // ensure the bootloader supports the base revision
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        halt();
    }

    // verify that we received a framebuffer response
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        halt();
    }

    serial_print("[+] Framebuffer acquired successfully.\n");

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    volatile uint32_t *fb_ptr = (volatile uint32_t *)fb->address;

    kprintf("[+] Core specifier test: string='%s', char='%c'\n", "microkernel", 'A');
    kprintf("[+] Number formats: dec=%u, signed=%d, hex=%x, ptr=%p, bin=%b\n",
            1048576, -42, 0x1000, fb->address, 0b10110010);

    halt();
}