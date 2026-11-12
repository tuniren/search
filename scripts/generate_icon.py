"""Generate the orange search magnifier as a Windows ICO, using only stdlib.

The geometry and color match Glyph::Search in src/ui/theme.cpp and theme.h.
The committed icon is used by Windows resources; builds do not run Python.
"""

from math import hypot
from pathlib import Path
import struct
import zlib

COLOR = (234, 116, 28)
SIZES = (16, 24, 32, 48, 64, 128, 256)


def png(size: int) -> bytes:
    rows = bytearray()
    for y in range(size):
        rows.append(0)  # PNG filter: None
        for x in range(size):
            covered = 0
            for dy in range(4):
                for dx in range(4):
                    px = (x + (dx + 0.5) / 4) * 24 / size
                    py = (y + (dy + 0.5) / 4) * 24 / size
                    ring = abs(hypot(px - 9.5, py - 9.5) - 5.5)
                    along = max(0.0, min(1.0, (px + py - 28) / 12))
                    handle = hypot(px - 14 - 6 * along, py - 14 - 6 * along)
                    covered += min(ring, handle) <= 1.65 / 2
            rows.extend((*COLOR, round(covered * 255 / 16)))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows, 9))
        + chunk(b"IEND", b"")
    )


def main() -> None:
    images = [png(size) for size in SIZES]
    offset = 6 + 16 * len(images)
    directory = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    for size, image in zip(SIZES, images):
        directory.extend(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(image), offset))
        offset += len(image)
    target = Path(__file__).resolve().parent.parent / "assets" / "search.ico"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(directory + b"".join(images))
    print(f"Generated {target} ({target.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
