CC = clang
LD = ld.lld

CFLAGS = -Wall -Wextra -std=c11 -O2 -g \
         -target x86_64-unknown-none-elf \
         -ffreestanding -fno-stack-protector -fno-stack-check \
         -fno-lto -fPIE -mno-red-zone \
         -Isrc

LDFLAGS = -nostdlib -static -T linker.ld -z max-page-size=0x1000

IMAGE = os.iso

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)

all: $(IMAGE)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

./limine/limine:
	$(MAKE) -C limine

$(IMAGE): kernel.elf limine.conf ./limine/limine
	rm -rf iso_root
	mkdir -p iso_root
	cp kernel.elf limine.conf limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/
	xorriso -as mkisofs -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE)
	./limine/limine bios-install $(IMAGE)

run: $(IMAGE)
	qemu-system-x86_64 -cdrom $(IMAGE) -m 256M -serial stdio -no-reboot -no-shutdown

clean:
	rm -rf $(OBJS) kernel.elf $(IMAGE) iso_root

.PHONY: all run clean