import os
import sys

# 目标句子：支持中文，编码为 UTF-8 字节串
TARGET_STR = "XJ380"
TARGET = TARGET_STR.encode('utf-8')   # 转为 bytes

# 每次读取的块大小（4KB）
CHUNK_SIZE = 4096

# 滑动窗口：用于跨块匹配
WINDOW = len(TARGET) - 1   # len(TARGET) 是字节数，正确

def main():
    # 开场白（满足“必须是一个句子”）
    print("Searching for the exact sentence in /dev/urandom, attempts will be counted until found.")
    print(f"Target: '{TARGET_STR}'")          # 直接打印原始字符串，无需 decode
    print("Press Ctrl+C to stop at any time.\n")

    with open("/dev/urandom", "rb") as f:
        tail = b""
        chunks_read = 0
        total_bytes = 0

        while True:
            chunk = f.read(CHUNK_SIZE)
            if not chunk:
                break

            chunks_read += 1
            total_bytes += len(chunk)

            # 拼接上一块尾部，检查是否包含目标（现在两者都是 bytes）
            combined = tail + chunk
            if TARGET in combined:
                print(f"\n✅ Blue Archive found! Total attempts: {chunks_read} chunks ({total_bytes} bytes).")
                sys.exit(0)

            # 更新尾部数据，用于跨块检查
            tail = chunk[-WINDOW:] if len(chunk) >= WINDOW else chunk

            # 实时进度（每秒刷新，减少打印开销）
            print(f"Attempts: {chunks_read:,} | Data read: {total_bytes / (1024**2):.2f} MB", end="\r")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n⏹️  Manual interrupt. Total attempts before stop: " +
              f"{chunks_read:,} chunks" if 'chunks_read' in locals() else "0")
        sys.exit(1)
