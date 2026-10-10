# 免责声明 / DISCLAIMER

> **中文（以中文为准）**

## 1. 同宗同源声明

本仓库 `Real-GNOS/gnuos`（原 `Yinyuan34513/gnuos`）与 `Real-GNOS/GNOS`、
`Real-GNOS/gnos-birth` **同宗同源**，均出自同一作者：

- 代码源头是 2026-08-09 的出生会话（完整实录见 `Real-GNOS/gnos-birth` 的
  `docs/01-birth-session-2026-08-09.md`）：从"写一个基础的 Unix-like 操作系统，
  先从第 1 步开始 (qemu)"起步，当天产出 step2（自研 UEFI 引导管理器 +
  x86-64 内核）。
- 本仓库是该阶段代码的**中文注释版**（2026-08-25 建仓）。仓库内的
  `conv.txt`、`conversation-2026-08-25.txt`、`conversation-20d26-08-25.txt`、
  `session-ses_fde4.md` 即上述会话实录的副本，与 `Real-GNOS/gnos-birth`
  的 `docs/02`–`docs/04` 及开发会话档案同源（逐字节相同）。
- `Real-GNOS/GNOS` 是同一代码流的后续主线演进（英文注释，持续开发至今）。
- **撤回说明**：本仓库 2026-08-25 提交 `9a3d4cf` 中的旧版免责声明声称本项目与
  GNOS"没有任何关系""未复制、未引用、未翻译其任何代码或文档"，该表述
  **与事实不符，自本版起废止**。旧版仅留存于 git 历史，不代表本项目立场。

## 2. 与 GNU 计划的关系

本项目名称中的 "gnu" 字样**不代表**本项目与 GNU 计划（gnu.org）或自由软件基金会
（FSF）存在任何隶属或认可关系。本项目仅采用 GNU GPL v2 许可证发布代码，许可证
本身不构成任何组织背书。

## 3. 与任何第三方的关系

本项目与 QEMU、OVMF/EDK II、gnu-efi、Linux 内核等第三方组件的作者及维护者无隶属
关系；上述组件仅按其各自许可证作为构建/测试工具或字体来源使用
（详见 README「许可」一节）。

## 4. 免责范围

本项目按"现状"提供，仅用于教学与学习目的。作者不对使用本项目（包括将镜像写入
存储设备引导真机）造成的任何数据丢失、硬件或软件问题承担责任。请自行备份重要
数据，并在充分理解的前提下操作。

---

> **English (for reference only; the Chinese text above prevails)**

`Real-GNOS/gnuos` (formerly `Yinyuan34513/gnuos`) shares a **common origin**
with `Real-GNOS/GNOS` and `Real-GNOS/gnos-birth`: all three come from the same
author, and their code descends from the same birth session of 2026-08-09
(transcripts archived in `Real-GNOS/gnos-birth`). This repository is the
Chinese-commented snapshot of that session's step-2 output (created 2026-08-25);
the conversation files it ships are verbatim copies of the session archives.
`Real-GNOS/GNOS` is the continued mainline of the same code. The earlier
disclaimer in commit `9a3d4cf` — which denied any relationship with GNOS and
denied copying any of its code or documentation — was factually incorrect and
is hereby retracted; it remains only in git history and does not represent this
project's position. The word "gnu" in the project name does NOT imply any
affiliation with or endorsement by the GNU Project (gnu.org) or the FSF; the
project merely uses the GNU GPL v2 license. Third-party components (QEMU,
OVMF/EDK II, gnu-efi, Linux kernel font, etc.) are used only under their
respective licenses. This project is provided "as is" for educational purposes;
the author assumes no liability for any damage or data loss arising from its use.
