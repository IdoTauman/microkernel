CC = clang
LD = ld.lld

CFLAGS = -Wall -Wextra -std=c11 -O2 -g \
         -target x86_64-unknown-none-elf \
         -ffreestanding -fno-stack-protector -fno-stack-check \
         -fno-lto -fPIE -mno-red-zone \
         -Isrc

LDFLAGS = -nostdlib -static -T linker.ld -z max-page-size=0x1000

IMAGE = os.iso

all: $(IMAGE)

kernel.elf: src/kernel.c
	$(CC) $(CFLAGS) -c src/kernel.c -o kernel.o
	$(LD) $(LDFLAGS) kernel.o -o kernel.elf

$(IMAGE): kernel.elf limine.conf
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
	qemu-system-x86_64 -cdrom $(IMAGE) -m 256M

clean:
	rm -rf *.o kernel.elf $(IMAGE) iso_root