"""Draws the snowflake app icon and writes src/app.ico (PNG-compressed entries, no dependencies)."""
import math
import struct
import zlib
from pathlib import Path


def render(size):
    s = 4
    n = size * s
    buf = bytearray(n * n)
    c = (n - 1) / 2
    thick = max(n * 0.055, s * 0.9)

    def line(x0, y0, x1, y1, w):
        for y in range(max(0, int(min(y0, y1) - w)), min(n, int(max(y0, y1) + w) + 1)):
            for x in range(max(0, int(min(x0, x1) - w)), min(n, int(max(x0, x1) + w) + 1)):
                dx, dy = x1 - x0, y1 - y0
                t = max(0, min(1, ((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy)))
                if math.hypot(x - x0 - t * dx, y - y0 - t * dy) <= w:
                    buf[y * n + x] = 1

    r = n * 0.46
    for k in range(6):
        a = k * math.pi / 3
        ex, ey = c + r * math.cos(a), c + r * math.sin(a)
        line(c, c, ex, ey, thick)
        if size >= 24:
            for f, l in ((0.62, 0.24), (0.38, 0.16)):
                bx, by = c + r * f * math.cos(a), c + r * f * math.sin(a)
                for side in (-1, 1):
                    b = a + side * math.pi / 3
                    line(bx, by, bx + r * l * math.cos(b), by + r * l * math.sin(b), thick * 0.8)

    rows = []
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            cov = sum(buf[(y * s + j) * n + x * s + i] for j in range(s) for i in range(s)) / (s * s)
            row += bytes((206, 232, 255, int(cov * 255)))
        rows.append(bytes(row))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
            + chunk(b"IEND", b""))


sizes = [16, 20, 24, 32, 48, 64, 256]
images = [render(s) for s in sizes]
offset = 6 + 16 * len(sizes)
head = struct.pack("<HHH", 0, 1, len(sizes))
table = b""
for s, img in zip(sizes, images):
    table += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(img), offset)
    offset += len(img)
Path(__file__).resolve().parent.parent.joinpath("src", "app.ico").write_bytes(head + table + b"".join(images))
