/*
 * bootinfo.h — gnuos 引导器与内核共享的引导信息（GPLv2）
 *
 * 引导器在跳转内核前把该结构放到固定物理地址 GNUCOS_BOOTINFO_ADDR，
 * 并以 rdi 传递指针。
 */
#ifndef GNUCOS_BOOTINFO_H
#define GNUCOS_BOOTINFO_H

#include <stdint.h>

#define GNUCOS_BOOTINFO_MAGIC 0x474E55434F533132ULL /* "GNUCOS12" 小端 */
#define GNUCOS_BOOTINFO_ADDR  0x8000

#define GNUCOS_FB_UNKNOWN     0
#define GNUCOS_FB_RGB         1

typedef struct {
    uint64_t magic;            /* = GNUCOS_BOOTINFO_MAGIC          */
    uint64_t kernel_entry;     /* 内核入口物理地址                  */

    /* UEFI 内存表快照（引导器已拷贝到 mmap_addr） */
    uint64_t mmap_addr;        /* EFI_MEMORY_DESCRIPTOR 数组地址    */
    uint64_t mmap_size;        /* 总字节数                          */
    uint64_t mmap_desc_size;   /* 单条描述符大小                    */
    uint64_t mmap_ver;         /* 描述符版本                        */

    /* GOP 帧缓冲（fb_addr=0 表示不可用，仅串口输出） */
    uint64_t fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;         /* 每扫描线字节数                    */
    uint32_t fb_bpp;           /* 每像素位数                        */
    uint32_t fb_type;          /* GNUCOS_FB_*                       */

    uint32_t rsdp_addr;        /* ACPI RSDP，暂未抓取置 0           */
    uint32_t vga_cols;         /* 固件文本回退信息                  */
    uint32_t vga_rows;
} bootinfo_t;

#endif