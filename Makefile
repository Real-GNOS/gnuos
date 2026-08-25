# gnuos Makefile (GPLv2)
# 目标：
#   make all     — 构建 kernel.elf 与 BOOTX64.EFI
#   make img     — 打包 build/gnuos.img（GPT + ESP，可直接 dd 到 U 盘）
#   make run     — QEMU + OVMF 验证（仅开发测试；真机无需 QEMU）
#   make clean

BUILD  := build
STAGING := $(BUILD)/staging
IMG    := $(BUILD)/gnuos.img

GNUEFI_INC  := /usr/include/efi /usr/include/efi/x86_64
GNUEFI_LIB  := /usr/lib
GNUEFI_CRT  := $(GNUEFI_LIB)/crt0-efi-x86_64.o
GNUEFI_LDS  := $(GNUEFI_LIB)/elf_x86_64_efi.lds

# ---------- 内核（x86_64 freestanding） ----------
KCFLAGS := -m64 -ffreestanding -nostdlib -fno-pic -fno-pie \
           -fno-stack-protector -fno-builtin -nostdinc -std=gnu11 \
           -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
           -Wall -Wextra -O2 -g -Isrc/include -Isrc/shared -Isrc/kernel
KASFLAGS := -f elf64

KOBJS := $(BUILD)/entry.o $(BUILD)/kernel.o

# ---------- UEFI 引导器（gnu-efi） ----------
ECFLAGS := -m64 -ffreestanding -fno-stack-protector -fno-stack-check \
           -fshort-wchar -fpic -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
           -Wall -Wextra -O2 -g \
           -D_GNU_SOURCE \
           $(addprefix -I,$(GNUEFI_INC)) -Isrc/shared
ELDFLAGS := -nostdlib -znocombreloc -shared -Bsymbolic \
            -T $(GNUEFI_LDS) -L$(GNUEFI_LIB)

.PHONY: all img run clean

all: $(BUILD)/kernel.elf $(BUILD)/BOOTX64.EFI

# 内核
$(BUILD)/entry.o: src/kernel/entry.asm | $(BUILD)
	nasm $(KASFLAGS) -o $@ $<

$(BUILD)/kernel.o: src/kernel/kernel.c src/kernel/font8x16.h src/shared/bootinfo.h | $(BUILD)
	gcc $(KCFLAGS) -c -o $@ $<

$(BUILD)/kernel.elf: $(KOBJS) linker.ld
	ld -m elf_x86_64 -T linker.ld --no-dynamic-linker -static -o $@ $(KOBJS)

# 引导器
$(BUILD)/bootloader.o: src/bootloader/bootloader.c src/shared/bootinfo.h | $(BUILD)
	gcc $(ECFLAGS) -c -o $@ $<

$(BUILD)/bootloader.so: $(BUILD)/bootloader.o
	ld $(ELDFLAGS) -o $@ $(GNUEFI_CRT) $< -lefi -lgnuefi

$(BUILD)/BOOTX64.EFI: $(BUILD)/bootloader.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	        -j .rel -j .rela -j .rel.* -j .rela.* -j .reloc \
	        --target=efi-app-x86_64 --subsystem=10 $< $@

$(BUILD):
	mkdir -p $(BUILD)

# ---------- 镜像：GPT + ESP(FAT32) ----------
# 无需 root：sfdisk 直接写镜像文件；mkfs.fat --offset 定位分区；
# mtools 用 image@@offset 语法读写分区内容。
$(IMG): all | $(STAGING)
	truncate -s 64M $@
	printf 'label: gpt\n	start=2048, size=126976, type=uefi\n' | sfdisk -q $@
	mkfs.fat -F32 --offset 2048 $@
	mmd   -i $(CURDIR)/$(IMG)@@1048576 ::/EFI ::/EFI/BOOT ::/gnuos
	mcopy -i $(CURDIR)/$(IMG)@@1048576 $(BUILD)/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $(CURDIR)/$(IMG)@@1048576 $(BUILD)/kernel.elf   ::/gnuos/kernel.elf
	@echo "==> $(IMG) 就绪（GPT+ESP）。写入 U 盘: dd if=$@ of=/dev/sdX bs=4M"

$(STAGING):
	mkdir -p $(STAGING)

img: $(IMG)

# ---------- QEMU + OVMF 开发验证（真机不需要） ----------
OVMF_CODE := /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS := $(BUILD)/VARS.fd
QOUT      := $(BUILD)/qemu

run: $(IMG) $(OVMF_VARS)
	qemu-system-x86_64 \
	    -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
	    -drive if=pflash,format=raw,file=$(OVMF_VARS) \
	    -drive file=$(IMG),format=raw \
	    -serial stdio -display none -qmp unix:$(QOUT).qmp,server,nowait & \
	echo $$! > $(QOUT).pid; sleep 6; kill $$(cat $(QOUT).pid) 2>/dev/null || true

$(OVMF_VARS):
	cp /usr/share/OVMF/OVMF_VARS_4M.fd $@

clean:
	rm -rf $(BUILD)
