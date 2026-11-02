#include <stdint.h>
#include <stdarg.h>
#include "serial.h"
#include "io.h"

#define PORT(n) (COM1_PORT + n)

int serial_init(void) {
    // set DLAB to 0
    outb(PORT(3), 0x00);

    // disable UART interrupts
    outb(PORT(1), 0x00);

    // enable DLAB
    outb(PORT(3), 0x80);

    // set divisor to 3 (38400 baud)
    outb(PORT(0), 0x03); // dll
    outb(PORT(1), 0x00); // dlh

    // set 8-N-1 and clear DLAB
    outb(PORT(3), 0x03);

    // enable FIFO, clear TX/RX queues, 14 byte threshold
    outb(PORT(2), 0xc7);

    // init MCR with loopback
    outb(PORT(4), 0x1e);

    // simple loopback test
    outb(PORT(0), 0b10101010);
    uint8_t byte = inb(PORT(0));
    if (byte != 0b10101010) return 1; // hardware failure

    // cancel loopback
    outb(PORT(4), 0x0f);

    // success
    return 0;
}

static int transmit_ready() {
    uint8_t lsr = inb(PORT(5));
    // return non zero iff bit 5 is set
    return lsr & (1 << 5);
}

void serial_putc(char c) {
    // spinwait until transmission is ready
    while (!transmit_ready()) {
        __asm__ volatile ("pause");
    }

    // transmit character
    outb(PORT(0), c);
}

void serial_print(const char *str) {
    while (*str) {
        if (*str == '\n') {
            serial_putc('\r');
        }
        serial_putc(*str);
        str++;
    }
}

void serial_print_hex(uint64_t val) {
    static const char lut[] = "0123456789abcdef";

    serial_print("0x");
    if (val == 0) {
        serial_putc('0');
        return;
    }

    // number of leading zero nibbles to skip
    int leading_zeros = __builtin_clzll(val) >> 2;
    int start_nibble = 15 - leading_zeros;

    for (int i = start_nibble; i >= 0; i--) {
        serial_putc(lut[(val >> (i * 4)) & 0xf]);
    }
}

void serial_print_dec(uint64_t val) {
    if (val == 0) {
        serial_putc('0');
        return;
    }

    char buf[20];
    int i = 0;

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    // print the buffer in reverse
    for (int j = i - 1; j >= 0; j--) {
        serial_putc(buf[j]);
    }
}

void serial_print_signed(int64_t val) {
    if (val < 0) {
        serial_putc('-');
        serial_print_dec((uint64_t)-(uint64_t)val);
        return;
    }
    serial_print_dec((uint64_t)val);
}

void serial_print_ptr(uintptr_t val) {
    static const char lut[] = "0123456789abcdef";
    serial_print("0x");
    for (int i = (sizeof(uintptr_t) * 2) - 1; i >= 0; i--) {
        serial_putc(lut[(val >> (i * 4)) & 0xf]);
    }
}

void serial_print_bin(uint64_t val) {
    serial_print("0b");
    if (val == 0) {
        serial_putc('0');
        return;
    }

    int start_bit = 63 - __builtin_clzll(val);

    for (int i = start_bit; i >= 0; i--) {
        char bit = (val & (1ULL << i)) ? '1' : '0';
        serial_putc(bit);
    }
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            if (*fmt == '\n') {
                serial_putc('\r');
            }
            serial_putc(*fmt);
            fmt++;
            continue;
        }

        fmt++; // Skip '%'

        switch (*fmt) {
            case 's': {
                const char *s = va_arg(args, const char *);
                serial_print(s ? s : "(null)");
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                serial_putc(c);
                break;
            }
            case 'x': {
                uint64_t val = va_arg(args, uint64_t);
                serial_print_hex(val);
                break;
            }
            case 'p': {
                uintptr_t val = (uintptr_t)va_arg(args, const void *);
                serial_print_ptr(val);
                break;
            }
            case 'u': {
                uint64_t val = va_arg(args, uint64_t);
                serial_print_dec(val);
                break;
            }
            case 'd': {
                int64_t val = va_arg(args, int);
                serial_print_signed((int64_t)val);
                break;
            }
            case 'b': {
                uint64_t val = va_arg(args, uint64_t);
                serial_print_bin(val);
                break;
            }
            case '%': {
                serial_putc('%');
                break;
            }
            default: {
                // unknown specifier, print verbatim
                serial_putc('%');
                serial_putc(*fmt);
                break;
            }
        }
        fmt++;
    }

    va_end(args);
}