/*
 * kernel.c — gnuos 内核（GPLv2）
 *
 * 第 2 步：由自研 UEFI 引导器直接引导。
 *   - 帧缓冲（GOP 提供）+ 内置 8x16 字体绘制文本
 *   - COM1 串口同步输出（便于无头调试）
 *   - 解析 bootinfo：内存表统计
 */
#include <stddef.h>
#include <stdint.h>

#include "font8x16.h"
#include "bootinfo.h"

/* ---------------- 端口 IO ---------------- */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* ---------------- 串口 COM1 ---------------- */
#define COM1 0x3F8

static void serial_init(void)
{
    outb(COM1 + 1, 0x00);    /* 关中断 */
    outb(COM1 + 3, 0x80);    /* DLAB=1 */
    outb(COM1 + 0, 0x01);    /* 除数低字节：115200 */
    outb(COM1 + 1, 0x00);    /* 除数高字节 */
    outb(COM1 + 3, 0x03);    /* 8N1 */
    outb(COM1 + 2, 0xC7);    /* FIFO */
    outb(COM1 + 4, 0x0B);    /* RTS/DSR */
}

static void serial_putc(char c)
{
    while (!(inb(COM1 + 5) & 0x20))
        ;
    outb(COM1, (uint8_t)c);
}

static void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

/* ---------------- 帧缓冲绘制 ---------------- */
typedef struct {
    uint64_t addr;
    uint32_t w, h, pitch, bpp;
} fb_t;

static fb_t   g_fb;
static size_t g_cx, g_cy;          /* 光标，单位：字符 */

#define FG 0xFFFFFFFFu             /* 白 */
#define BG 0xFF101418u             /* 深色背景 */
#define CH_W 8
#define CH_H 16

static inline uint32_t *pixel(size_t x, size_t y)
{
    return (uint32_t *)(g_fb.addr + y * g_fb.pitch + x * 4);
}

static void fb_clear(uint32_t color)
{
    for (size_t y = 0; y < g_fb.h; y++)
        for (size_t x = 0; x < g_fb.w; x++)
            *pixel(x, y) = color;
}

static void fb_draw_char(size_t px, size_t py, char c, uint32_t fg)
{
    if ((uint8_t)c >= FONT_GLYPHS)
        return;
    const unsigned char *glyph =
        &font8x16[(size_t)(uint8_t)c * FONT_HEIGHT];
    for (size_t row = 0; row < CH_H; row++) {
        unsigned bits = glyph[row];
        for (size_t col = 0; col < CH_W; col++)
            *pixel(px + col, py + row) =
                (bits & (0x80u >> col)) ? fg : BG;
    }
}

/* 文本输出：支持 \n；超出屏幕滚一行 */
static void kputs(const char *s)
{
    serial_puts(s);
    if (!g_fb.addr)
        return;

    for (; *s; s++) {
        if (*s == '\n') {
            g_cx = 0;
            g_cy++;
        } else {
            fb_draw_char(g_cx * CH_W, g_cy * CH_H, *s, FG);
            g_cx++;
        }
        if (g_cx >= g_fb.w / CH_W) {
            g_cx = 0;
            g_cy++;
        }
        if (g_cy >= g_fb.h / CH_H) {
            /* 滚动：整屏上移一行字符高度 */
            for (size_t y = 0; y < g_fb.h - CH_H; y++)
                for (size_t x = 0; x < g_fb.w; x++)
                    *pixel(x, y) = *pixel(x, y + CH_H);
            for (size_t y = g_fb.h - CH_H; y < g_fb.h; y++)
                for (size_t x = 0; x < g_fb.w; x++)
                    *pixel(x, y) = BG;
            g_cy--;
        }
    }
}

/* ---------------- 简易格式化 ---------------- */
static void puthex_buf(char *buf, uint64_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++)
        buf[2 + i] = hex[(v >> (60 - i * 4)) & 0xF];
    buf[18] = 0;
}
static void putdec_buf(char *buf, uint64_t v)
{
    char tmp[21];
    int  n = 0, i = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n--) buf[i++] = tmp[n];
    buf[i] = 0;
}

/* ---------------- EFI 内存表类型名 ---------------- */
static const char *mmap_type_name(uint32_t t)
{
    switch (t) {
    case 1: return "usable ";
    case 2: return "reserved";
    case 3: return "ACPI rec";
    case 4: return "ACPI NVS";
    case 5: return "bad mem";
    case 6: return "MMIO";
    case 7: return "usable ";   /* EfiConventionalMemory */
    case 13: return "persist";
    default: return "other";
    }
}

void kernel_main(bootinfo_t *bi)
{
    serial_init();

    if (bi && bi->magic == GNUCOS_BOOTINFO_MAGIC) {
        g_fb.addr  = bi->fb_addr;
        g_fb.w     = bi->fb_width;
        g_fb.h     = bi->fb_height;
        g_fb.pitch = bi->fb_pitch;
        g_fb.bpp   = bi->fb_bpp;
    } else {
        g_fb.addr = 0;
        serial_puts("[!] bad bootinfo magic\n");
    }

    if (g_fb.addr)
        fb_clear(BG);

    kputs("gnuos kernel 0.2 — booted by gnuos-efi bootloader\n");
    kputs("==============================================\n");

    if (!bi) {
        kputs("no bootinfo!\n");
        goto halt;
    }
    {
        char b[24];

        if (g_fb.addr) {
            kputs("framebuffer: ");
            putdec_buf(b, g_fb.w); kputs(b);
            kputs("x");
            putdec_buf(b, g_fb.h); kputs(b);
            kputs(", pitch=");
            putdec_buf(b, g_fb.pitch); kputs(b);
            kputs("\n");
        } else {
            kputs("no framebuffer, serial-only\n");
        }
    }

    /* 遍历内存表，统计可用内存 */
    if (bi->mmap_addr) {
        uint64_t off = 0, usable = 0, entries = 0;

        while (off + 24 <= bi->mmap_size) {
            /* EFI_MEMORY_DESCRIPTOR 头 40 字节内含 Type/PhysStart/Pages */
            volatile uint8_t *d =
                (volatile uint8_t *)(uintptr_t)(bi->mmap_addr + off);
            uint32_t type      = *(const volatile uint32_t *)(const void *)d;
            uint64_t physstart = *(const volatile uint64_t *)(const void *)(d + 8);
            uint64_t pages     = *(const volatile uint64_t *)(const void *)(d + 24);

            entries++;
            if (type == 7) {           /* EfiConventionalMemory */
                usable += pages * 4096;

                if (entries <= 12) {   /* 只打印前几条，防刷屏 */
                    char b[24];
                    kputs("  mmap ");
                    puthex_buf(b, physstart); kputs(b);
                    kputs("  ");
                    putdec_buf(b, pages * 4096 / 1024); kputs(b);
                    kputs(" KiB  ");
                    kputs(mmap_type_name(type));
                    kputs("\n");
                }
            }
            off += bi->mmap_desc_size ? bi->mmap_desc_size : 40;
        }

        {
            char b[24];
            kputs("memory entries: ");
            putdec_buf(b, entries); kputs(b);
            kputs("\nusable RAM: ");
            putdec_buf(b, usable / (1024 * 1024)); kputs(b);
            kputs(" MiB\n");
        }
    }

    kputs("\nstep 2 complete: own EFI bootloader -> kernel.\n");

halt:
    for (;;)
        __asm__ __volatile__("hlt");
}
