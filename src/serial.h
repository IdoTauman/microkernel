#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stddef.h>

#define COM1_PORT 0x3F8

int  serial_init(void);
void serial_putc(char c);
void serial_print(const char *str);
void serial_print_hex(uint64_t val);
void serial_print_ptr(uintptr_t val);
void serial_print_dec(uint64_t val);
void serial_print_signed(int64_t val);
void serial_print_bin(uint64_t val);
void kprintf(const char *fmt, ...);

#endif