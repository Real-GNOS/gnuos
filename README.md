# gnuos

从零编写的 x86-64 Unix-like 教学操作系统。GPLv2。

**与 GitHub 上的 Real-GNOS/GNOS 项目无关，仅名称形近。**

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
