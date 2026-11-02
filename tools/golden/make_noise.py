import random
import struct
import sys
import zlib


def chunk(kind: bytes, data: bytes) -> bytes:
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def main() -> None:
    width, height = 600, 600
    generator = random.Random(1320)
    rows = bytearray()
    base = [128, 128, 128]
    for _ in range(height):
        rows.append(0)
        for x in range(width):
            if generator.random() < 0.05:
                base = [generator.randint(0, 255) for _ in range(3)]
            elif generator.random() < 0.4:
                base = [max(0, min(255, channel + generator.randint(-6, 6))) for channel in base]
            if x % 97 < 40:
                pixel = base
            else:
                pixel = [max(0, min(255, channel + generator.randint(-3, 3))) for channel in base]
            rows.extend(pixel)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    png += chunk(b"IEND", b"")
    with open(sys.argv[1], "wb") as out:
        out.write(png)


if __name__ == "__main__":
    main()
