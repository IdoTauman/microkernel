#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"

// set the base revision to 2 (supported by Limine v8/v7)
__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(2);

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
static void halt(void) {
    for (;;) {
        __asm__("hlt");
    }
}

// kernel entry point
void kmain(void) {
    // ensure the bootloader supports the base revision
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        halt();
    }

    // verify that we received a framebuffer response
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        halt();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    volatile uint32_t *fb_ptr = (volatile uint32_t *)fb->address;

    // fill screen with black
    for (size_t y = 0; y < fb->height; y++) {
        for (size_t x = 0; x < fb->width; x++) {
            fb_ptr[y * (fb->pitch / 4) + x] = 0x00000000;
        }
    }

    // draw a small white rectangle (100x50) in the center
    size_t start_y = fb->height / 2 - 25;
    size_t start_x = fb->width / 2 - 50;
    for (size_t y = start_y; y < start_y + 50; y++) {
        for (size_t x = start_x; x < start_x + 100; x++) {
            fb_ptr[y * (fb->pitch / 4) + x] = 0x00FFFFFF;
        }
    }

    halt();
}