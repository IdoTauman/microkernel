#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stddef.h>

#define COM1_PORT 0x3F8

int  serial_init(void);
void serial_putc(char c);
void serial_print(const char *str);

#endif