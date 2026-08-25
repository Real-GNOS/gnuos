/*
 * bootloader.c — gnuos UEFI 引导管理器 (GPLv2)
 *
 * 流程：
 *   1. ConOut 打印横幅
 *   2. 通过 GOP 获取帧缓冲信息
 *   3. 从启动卷打开 \gnuos\kernel.elf 并读入内存池
 *   4. 解析 ELF64，把每个 PT_LOAD 段拷到 p_paddr（恒等映射），bss 清零
 *   5. 把 UEFI 内存表快照拷到 bootinfo 之后的固定地址
 *   6. 重试式 ExitBootServices 后跳转内核入口（rdi = bootinfo 指针）
 *
 * 许可：GNU GPL v2 或更高版本。
 */
#include <efi.h>
#include <efilib.h>
#include <stdint.h>

#include "bootinfo.h"

#define ELF_EM_X86_64 62
#define ELF_ET_EXEC   2
#define ELF_PT_LOAD   1

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

/* ---------------- 基础输出 ---------------- */

static VOID raw_print(EFI_SYSTEM_TABLE *st, CONST CHAR16 *s)
{
    uefi_call_wrapper(st->ConOut->OutputString, 2,
                      st->ConOut, (CHAR16 *)s);
}

/* 独立于 Print 格式化器的十六进制输出（交叉验证用） */
static VOID puthex_raw(EFI_SYSTEM_TABLE *st, UINT64 v)
{
    static const CHAR16 h[] = L"0123456789ABCDEF";
    CHAR16 b[19];
    INTN i;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 16; i++)
        b[2 + i] = h[(v >> ((15 - i) * 4)) & 0xF];
    b[18] = 0;
    raw_print(st, b);
}

/* ---------------- 文件读取 ---------------- */

static EFI_STATUS open_kernel(EFI_HANDLE image, EFI_SYSTEM_TABLE *st,
                              EFI_FILE **out)
{
    EFI_LOADED_IMAGE_PROTOCOL       *loaded = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs     = NULL;
    EFI_FILE                        *root   = NULL;
    CHAR16 path[] = L"\\gnuos\\kernel.elf";
    EFI_STATUS r;

    r = uefi_call_wrapper(st->BootServices->HandleProtocol, 3,
                          image, &gEfiLoadedImageProtocolGuid,
                          (VOID **)&loaded);
    if (EFI_ERROR(r)) { raw_print(st, L"[!] no loaded-image\r\n"); return r; }

    r = uefi_call_wrapper(st->BootServices->HandleProtocol, 3,
                          loaded->DeviceHandle,
                          &gEfiSimpleFileSystemProtocolGuid, (VOID **)&fs);
    if (EFI_ERROR(r)) { raw_print(st, L"[!] boot volume is not FAT/ESP\r\n"); return r; }

    r = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
    if (EFI_ERROR(r)) return r;

    return uefi_call_wrapper(root->Open, 5, root, out, path,
                             EFI_FILE_MODE_READ, 0);
}

static VOID *read_kernel(EFI_SYSTEM_TABLE *st, EFI_FILE *f, UINTN *size_out)
{
    EFI_FILE_INFO *info = NULL;
    UINTN infosz = 0, size = 0;
    VOID *buf;
    EFI_STATUS r;

    infosz = 0;
    r = uefi_call_wrapper(f->GetInfo, 4, f, &gEfiFileInfoGuid, &infosz, NULL);
    if (r != EFI_BUFFER_TOO_SMALL || infosz == 0) {
        raw_print(st, L"[!] GetInfo(size) failed\r\n");
        return NULL;
    }
    info = AllocateZeroPool(infosz);
    if (!info) return NULL;

    r = uefi_call_wrapper(f->GetInfo, 4, f, &gEfiFileInfoGuid, &infosz, info);
    if (EFI_ERROR(r)) { FreePool(info); return NULL; }

    size = (UINTN)info->FileSize;
    FreePool(info);
    if (size == 0) return NULL;

    buf = AllocateZeroPool(size);
    if (!buf) return NULL;

    r = uefi_call_wrapper(f->Read, 3, f, &size, buf);
    if (EFI_ERROR(r)) { FreePool(buf); return NULL; }

    *size_out = size;
    return buf;
}

/* ---------------- ELF 加载 ---------------- */

static UINT64 map_kernel(EFI_SYSTEM_TABLE *st, const VOID *file, UINTN size)
{
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)file;
    UINT16 i;

    if (size < sizeof(elf64_ehdr_t)) {
        raw_print(st, L"[!] truncated ELF\r\n");
        return 0;
    }
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F') {
        raw_print(st, L"[!] not an ELF file\r\n");
        return 0;
    }
    if (eh->e_type != ELF_ET_EXEC || eh->e_machine != ELF_EM_X86_64 ||
        eh->e_phentsize < sizeof(elf64_phdr_t)) {
        raw_print(st, L"[!] not a x86-64 ELF executable\r\n");
        return 0;
    }

    raw_print(st, L"  e_entry=");
    puthex_raw(st, eh->e_entry);
    raw_print(st, L"\r\n");

    for (i = 0; i < eh->e_phnum; i++) {
        const elf64_phdr_t *ph = (const elf64_phdr_t *)
            ((const char *)file + eh->e_phoff + (UINTN)i * eh->e_phentsize);
        char       *dst;
        const char *src;
        UINT64      n;

        if (ph->p_type != ELF_PT_LOAD) continue;

        if (ph->p_vaddr != ph->p_paddr) {
            raw_print(st, L"[!] kernel must be identity-mapped\r\n");
            return 0;
        }
        if (ph->p_offset + ph->p_filesz > size) {
            raw_print(st, L"[!] segment beyond end of file\r\n");
            return 0;
        }

        /* 规范做法：向固件预留目标页（EfiLoaderData），避免与固件数据冲突 */
        {
            UINTN pg = (UINTN)((ph->p_memsz + 0xFFF) >> 12);
            EFI_PHYSICAL_ADDRESS want = ph->p_paddr;
            EFI_STATUS ar = uefi_call_wrapper(
                st->BootServices->AllocatePages, 4,
                AllocateAddress, EfiLoaderData, pg, &want);
            if (EFI_ERROR(ar)) {
                raw_print(st, L"[!] AllocateAddress 0x");
                puthex_raw(st, (UINT64)ph->p_paddr);
                raw_print(st, L": ");
                puthex_raw(st, (UINT64)ar);
                raw_print(st, L"\r\n");
            }
        }

        dst = (char *)(UINTN)ph->p_paddr;
        src = (const char *)file + ph->p_offset;

        for (n = ph->p_filesz; n--; ) *dst++ = *src++;
        for (n = ph->p_memsz - ph->p_filesz; n--; ) *dst++ = 0;

        raw_print(st, L"  PT_LOAD paddr=");
        puthex_raw(st, ph->p_paddr);
        raw_print(st, L" filesz=");
        puthex_raw(st, ph->p_filesz);
        raw_print(st, L" memsz=");
        puthex_raw(st, ph->p_memsz);
        raw_print(st, L"\r\n");
    }
    return eh->e_entry;
}

/* ---------------- GOP ---------------- */

static BOOLEAN grab_gop(EFI_SYSTEM_TABLE *st, bootinfo_t *bi)
{
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi;
    EFI_STATUS r;

    r = uefi_call_wrapper(st->BootServices->LocateProtocol, 3,
                          &gop_guid, NULL, (VOID **)&gop);
    if (EFI_ERROR(r) || !gop || !gop->Mode || !gop->Mode->Info)
        return FALSE;

    mi = gop->Mode->Info;
    if (mi->PixelFormat == PixelBltOnly)
        return FALSE;

    bi->fb_addr   = gop->Mode->FrameBufferBase;
    bi->fb_width  = mi->HorizontalResolution;
    bi->fb_height = mi->VerticalResolution;
    bi->fb_bpp    = 32;
    bi->fb_pitch  = mi->PixelsPerScanLine * 4;
    bi->fb_type   = GNUCOS_FB_RGB;
    return TRUE;
}

/*
 * ---------------- 内存表 ----------------
 *
 * GetMemoryMap 的缓冲需求在每次分配/释放后都可能变化，
 * 所以用"失败就扩大缓冲重来"的循环，直到成功。
 *
 * 注意：bootinfo 与快照绝不能硬编码到固定低地址——固件可能正在使用
 * 那里（实测 OVMF 在 0x8000 附近有活数据，ExitBS 时会踩中）。
 * 正确做法是先用 AllocateAddress 向固件预留一段页。
 */
#define BI_BLOCK_PAGES 16          /* 64 KiB 足够 bootinfo + mmap 快照 */

static bootinfo_t *reserve_bootinfo(EFI_SYSTEM_TABLE *st)
{
    static const UINT64 cand[] = {
        0x80000, 0x70000, 0x60000, 0x50000, 0x40000, 0x30000, 0x20000, 0x10000
    };
    UINTN i;

    for (i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
        EFI_PHYSICAL_ADDRESS a = cand[i];
        EFI_STATUS r = uefi_call_wrapper(st->BootServices->AllocatePages, 4,
                                         AllocateAddress, EfiLoaderData,
                                         BI_BLOCK_PAGES, &a);
        if (!EFI_ERROR(r)) {
            raw_print(st, L"[m] bootinfo block @ ");
            puthex_raw(st, (UINT64)a);
            raw_print(st, L"\r\n");
            return (bootinfo_t *)(UINTN)a;
        }
    }
    return NULL;
}

static EFI_STATUS
get_map_snapshot(EFI_SYSTEM_TABLE *st, bootinfo_t *bi, UINTN *key_out)
{
    UINTN size = 4096, dsize = 0, key = 0, i, nwords;
    UINT32 dver = 0;
    VOID *map = NULL;
    UINT64 *dst;
    const UINT64 *src;
    EFI_STATUS r;
    int tries = 0;

    for (;;) {
        r = uefi_call_wrapper(st->BootServices->GetMemoryMap, 5,
                              &size, map, &key, &dsize, &dver);
        if (!EFI_ERROR(r))
            break;
        if (r != EFI_BUFFER_TOO_SMALL || ++tries > 10)
            goto fail;
        if (map)
            FreePool(map);
        size += dsize ? dsize * 16 : 1024;
        map = AllocateZeroPool(size);
        if (!map)
            goto fail;
    }

    /* 快照拷贝到 bootinfo 之后的固定区域（<1MiB） */
    dst = (UINT64 *)(UINTN)((UINTN)bi + sizeof(bootinfo_t));
    src = (const UINT64 *)map;
    nwords = size / 8;
    for (i = 0; i < nwords; i++)
        dst[i] = src[i];

    bi->mmap_addr      = (UINTN)bi + sizeof(bootinfo_t);
    bi->mmap_size      = size;
    bi->mmap_desc_size = dsize;
    bi->mmap_ver       = dver;
    if (key_out)
        *key_out = key;

    FreePool(map);
    return EFI_SUCCESS;

fail:
    Print(L"[!] GetMemoryMap: %r\r\n", r);
    if (map)
        FreePool(map);
    bi->mmap_addr = 0;
    if (key_out)
        *key_out = 0;
    return r;
}

/*
 * 最终取 MapKey 用：预先分配一次大缓冲，此后刷新 key 不再分配/释放，
 * 保证取到 key 后到 ExitBootServices 之间内存表不会因我们而变化。
 */
#define KEYBUF_SIZE (256 * 1024)
static VOID *g_keybuf = NULL;

static EFI_STATUS refresh_key(EFI_SYSTEM_TABLE *st, UINTN *key_out)
{
    UINTN size = KEYBUF_SIZE, dsize = 0, key = 0;
    UINT32 dver = 0;
    EFI_STATUS r;

    if (!g_keybuf) {
        g_keybuf = AllocateZeroPool(KEYBUF_SIZE);
        if (!g_keybuf)
            return EFI_OUT_OF_RESOURCES;
    }
    r = uefi_call_wrapper(st->BootServices->GetMemoryMap, 5,
                          &size, g_keybuf, &key, &dsize, &dver);
    if (!EFI_ERROR(r) && key_out)
        *key_out = key;
    return r;
}


EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    bootinfo_t *bi;
    EFI_FILE   *f = NULL;
    VOID       *buf = NULL;
    UINTN       len = 0, key = 0;
    UINT64      entry = 0;
    EFI_STATUS  r;
    int t;

    InitializeLib(image, st);
    raw_print(st, L"\r\n");
    raw_print(st, L"=================================\r\n");
    raw_print(st, L"  gnuos bootloader 0.3  (GPLv2)\r\n");
    raw_print(st, L"=================================\r\n");

    bi = reserve_bootinfo(st);
    if (!bi) {
        raw_print(st, L"[!] no low memory for bootinfo\r\n");
        return EFI_OUT_OF_RESOURCES;
    }

    if (grab_gop(st, bi))
        Print(L"  GOP: %dx%d pitch=%d fb=0x%lx\r\n",
              bi->fb_width, bi->fb_height, bi->fb_pitch, bi->fb_addr);
    else {
        bi->fb_addr = 0;
        raw_print(st, L"  GOP: none, text-only\r\n");
    }
    bi->vga_cols = 80;
    bi->vga_rows = 25;

    r = open_kernel(image, st, &f);
    if (EFI_ERROR(r)) {
        raw_print(st, L"[!] cannot open \\gnuos\\kernel.elf\r\n");
        return r;
    }
    buf = read_kernel(st, f, &len);
    if (!buf) {
        raw_print(st, L"[!] cannot read kernel.elf\r\n");
        return EFI_LOAD_ERROR;
    }
    Print(L"  kernel.elf: %d bytes\r\n", len);

    bi->magic        = GNUCOS_BOOTINFO_MAGIC;
    bi->kernel_entry = 0;
    entry = map_kernel(st, buf, len);
    if (!entry) return EFI_LOAD_ERROR;
    bi->kernel_entry = entry;

    raw_print(st, L"[m] snap-enter\r\n");
    get_map_snapshot(st, bi, &key);
    raw_print(st, L"[m] snap-exit\r\n");
    if (!bi->mmap_addr) return EFI_LOAD_ERROR;
    Print(L"  mmap: %d entries x %d bytes\r\n",
          bi->mmap_size / bi->mmap_desc_size, bi->mmap_desc_size);

    /* 预分配最终取 key 的缓冲 */
    raw_print(st, L"[m] keybuf...\r\n");
    if (EFI_ERROR(refresh_key(st, &key))) {
        raw_print(st, L"[!] refresh_key failed\r\n");
        return EFI_LOAD_ERROR;
    }
    raw_print(st, L"[m] key-ok\r\n");

    Print(L"\r\n  entry=");
    puthex_raw(st, entry);
    Print(L"  bootinfo=0x%x  mmap=%d bytes\r\n", (UINTN)bi, bi->mmap_size);

    /*
     * 退出引导服务（带重试）。
     * 铁律：ExitBootServices 成功后不得再调用任何固件接口
     * （Boot Services / ConOut / Print 全部禁止），否则行为未定义。
     */
    for (t = 0; ; t++) {
        r = refresh_key(st, &key);
        if (EFI_ERROR(r))
            return r;
        r = uefi_call_wrapper(st->BootServices->ExitBootServices, 2,
                              image, key);
        if (!EFI_ERROR(r))
            break;
        if (t >= 4) {
            raw_print(st, L"[!] ExitBootServices failed\r\n");
            return r;
        }
    }

    /* 直接跳转内核：SysV 调用约定 rdi = bootinfo，中间不得插入任何调用 */
    __asm__ __volatile__("cli" ::: "memory");
    ((void (*)(bootinfo_t *))(UINTN)entry)(bi);

    for (;;)
        __asm__ __volatile__("cli; hlt");
}
