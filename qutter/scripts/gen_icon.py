"""生成 Qutter 占位图标 icons/icon.png（纯 Python，无第三方依赖）"""
import struct
import zlib

W = H = 512
CX = CY = 256
R_IN, R_OUT = 120, 165  # 中心圆环


def pixel(x, y):
    # 蓝紫渐变背景
    t = y / H
    r = int(59 + t * 60)
    g = int(130 + t * 30)
    b = int(246 - t * 40)
    a = 255
    d = ((x - CX) ** 2 + (y - CY) ** 2) ** 0.5
    if R_IN <= d <= R_OUT:
        r, g, b = 245, 248, 255  # 白色圆环（Q 的缺口造型）
    return bytes((r, g, b, a))


raw = bytearray()
for y in range(H):
    raw.append(0)  # filter type 0
    for x in range(W):
        raw += pixel(x, y)


def chunk(tag, data):
    c = struct.pack(">I", len(data)) + tag + data
    c += struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return c


ihdr = struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0)  # 8-bit RGBA
png = (
    b"\x89PNG\r\n\x1a\n"
    + chunk(b"IHDR", ihdr)
    + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    + chunk(b"IEND", b"")
)

import os

out = "src-tauri/icons/icon.png"
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "wb") as f:
    f.write(png)
print(out, "written:", len(png), "bytes")
