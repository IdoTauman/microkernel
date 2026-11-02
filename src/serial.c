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