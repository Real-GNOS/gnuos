# gnuos

x86-64 Unix-like 教学操作系统，**step2 阶段的中文注释版**（2026-08-09 出生会话产出）。GPLv2。

> ## 同宗同源（务必阅读）
>
> - 本项目与 GitHub 上的 **`Real-GNOS/GNOS`**、**`Real-GNOS/gnos-birth`**
>   **同宗同源**：同一作者、同一 2026-08-09 出生会话的代码流。
>   本仓是 step2 阶段的中文注释快照，GNOS 是同一代码流的主线演进，
>   出生实录存档于 `gnos-birth`；仓内 `conv.txt` 等会话文件即实录副本。
> - 2026-08-25 旧版免责声明（称与 GNOS 无任何关系）与事实不符，
>   已废止，详见 [DISCLAIMER.md](DISCLAIMER.md) 的撤回说明。
> - 名称中的 "gnu" **不代表**与 GNU 计划（gnu.org）/ FSF 有任何隶属或背书关系，
>   本项目仅采用 GPL-2.0 许可证发布。

## 当前状态（step 2）

- **自研 UEFI 引导管理器**（gnu-efi 编写，不依赖 GRUB/Limine）：
  - GOP 图形帧缓冲
  - 从 ESP 读取 `\gnuos\kernel.elf` 并解析 ELF64 加载
  - UEFI 内存表快照传递给内核
  - `ExitBootServices` 后以 `rdi = bootinfo*` 跳入 64 位内核
- **x86-64 内核**：帧缓冲文本输出（内置 8x16 字体）+ COM1 串口
- 启动镜像：GPT + ESP(FAT32)，可直接 `dd` 到 U 盘真机启动

## 构建

```sh
sudo apt install gcc nasm gnu-efi dosfstools mtools util-linux qemu-system-x86 ovmf
make all   # build/kernel.elf + build/BOOTX64.EFI
make img   # build/gnuos.img (GPT + ESP)
```

## 运行

```sh
make run        # QEMU + OVMF 开发验证
# 真机： dd if=build/gnuos.img of=/dev/sdX bs=4M && 从 U 盘 EFI 启动
```

## 目录

```
src/bootloader/   UEFI 引导管理器（BOOTX64.EFI）
src/kernel/       64 位内核入口 + 帧缓冲/串口驱动
src/shared/       bootinfo.h — 引导器/内核共享契约
src/include/      freestanding 头文件
linker.ld         内核链接脚本（恒等映射 @1MiB）
LICENSE           GPLv2
```

## 许可

GNU GPL v2 或更高版本。`src/kernel/font8x16.h` 派生自 Linux 内核
font_8x16.c（GPL-2.0）。
