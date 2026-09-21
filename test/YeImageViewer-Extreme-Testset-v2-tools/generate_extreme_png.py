#!/usr/bin/env python3
"""
YeImageViewer Extreme PNG Generator v2
Standard-library only: no Pillow, numpy or ImageMagick required.
"""

from __future__ import annotations

import argparse
import binascii
import os
from pathlib import Path
import struct
import zlib


PNG_SIG = b"\x89PNG\r\n\x1a\n"


def chunk(kind: bytes, data: bytes) -> bytes:
    crc = binascii.crc32(kind)
    crc = binascii.crc32(data, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", crc)


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def row_pattern(row_bytes: int, y: int, variant: int) -> bytes:
    """Highly compressible but non-identical deterministic scanlines."""
    patterns = [
        b"\x00", b"\xff", b"\xaa", b"\x55",
        b"\xf0", b"\x0f", b"\xcc", b"\x33",
        b"\x99", b"\x66",
    ]
    p1 = patterns[variant % len(patterns)]
    p2 = patterns[(variant + 3) % len(patterns)]
    if y % (16 + (variant % 7)) < (8 + variant % 3):
        p = p1
    else:
        p = p2
    return p * row_bytes


def write_stream_png(
    path: Path,
    width: int,
    height: int,
    *,
    bit_depth: int = 1,
    color_type: int = 0,
    variant: int = 0,
    extra_chunks: list[tuple[bytes, bytes]] | None = None,
    compression_level: int = 1,
    invalid_filter: int | None = None,
) -> None:
    """
    Streaming writer.
    color_type 0 supported for huge/ultra tests.
    RGB8 supported for metadata fixtures.
    """
    if color_type == 0:
        channels = 1
    elif color_type == 2:
        channels = 3
    else:
        raise ValueError("generator currently supports color type 0 or 2")

    bits_per_pixel = channels * bit_depth
    row_bytes = (width * bits_per_pixel + 7) // 8

    ensure_dir(path.parent)
    with path.open("wb") as f:
        f.write(PNG_SIG)
        ihdr = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, 0)
        f.write(chunk(b"IHDR", ihdr))

        if extra_chunks:
            for k, d in extra_chunks:
                f.write(chunk(k, d))

        comp = zlib.compressobj(compression_level)
        outbuf = bytearray()

        def flush_compressed(data: bytes) -> None:
            nonlocal outbuf
            if data:
                outbuf.extend(data)
            while len(outbuf) >= 1024 * 1024:
                block = bytes(outbuf[:1024 * 1024])
                del outbuf[:1024 * 1024]
                f.write(chunk(b"IDAT", block))

        if color_type == 0:
            for y in range(height):
                filter_byte = invalid_filter if (invalid_filter is not None and y == 0) else 0
                row = row_pattern(row_bytes, y, variant)
                flush_compressed(comp.compress(bytes([filter_byte]) + row))
        else:
            # RGB8 metadata fixtures.
            if bit_depth != 8:
                raise ValueError("RGB generator only supports 8 bit")
            for y in range(height):
                filter_byte = invalid_filter if (invalid_filter is not None and y == 0) else 0
                row = bytearray(row_bytes)
                for x in range(width):
                    i = x * 3
                    row[i] = (x + variant * 23) & 0xFF
                    row[i + 1] = (y + variant * 37) & 0xFF
                    row[i + 2] = ((x ^ y) + variant * 19) & 0xFF
                flush_compressed(comp.compress(bytes([filter_byte]) + row))

        flush_compressed(comp.flush())
        if outbuf:
            f.write(chunk(b"IDAT", bytes(outbuf)))

        f.write(chunk(b"IEND", b""))


def make_text(keyword: str, text: str) -> bytes:
    return keyword.encode("latin-1") + b"\x00" + text.encode("latin-1", errors="replace")


def make_ztxt(keyword: str, text: str) -> bytes:
    return (
        keyword.encode("latin-1")
        + b"\x00"
        + b"\x00"
        + zlib.compress(text.encode("latin-1", errors="replace"))
    )


def make_itxt(keyword: str, text: str, compressed: bool = False, lang: str = "zh-CN") -> bytes:
    text_b = text.encode("utf-8")
    if compressed:
        flag = b"\x01"
        payload = zlib.compress(text_b)
    else:
        flag = b"\x00"
        payload = text_b
    return (
        keyword.encode("latin-1")
        + b"\x00"
        + flag
        + b"\x00"
        + lang.encode("ascii")
        + b"\x00"
        + keyword.encode("utf-8")
        + b"\x00"
        + payload
    )


def generate_200mp(root: Path, smoke: bool) -> None:
    dest = root / "03_Generated_200MP_Plus"
    ensure_dir(dest)

    sizes = [
        (20000, 10000),
        (25000, 8000),
        (20000, 12000),
        (30000, 8000),
        (16000, 16000),
        (32768, 8192),
        (18000, 15000),
        (24000, 12000),
        (30000, 10000),
        (20000, 16000),
    ]
    if smoke:
        sizes = [(2000 + i * 100, 1000 + i * 50) for i in range(10)]

    for i, (w, h) in enumerate(sizes, 1):
        mp = (w * h) / 1_000_000
        path = dest / f"{i:02d}_{w}x{h}_{mp:.0f}MP_1bit.png"
        if path.exists():
            continue
        print(f"[GEN ] {path.name}")
        write_stream_png(path, w, h, bit_depth=1, color_type=0, variant=i)


def generate_ultra(root: Path, smoke: bool) -> None:
    dest = root / "08_UltraWide_Tall"
    ensure_dir(dest)

    wide = [
        (100000, 64),
        (80000, 96),
        (65535, 128),
        (50000, 160),
        (40000, 200),
        (32000, 256),
        (24000, 320),
        (20000, 480),
        (16000, 720),
        (12000, 1000),
    ]
    tall = [(h, w) for (w, h) in wide]

    if smoke:
        wide = [(5000 + i * 100, 16 + i) for i in range(10)]
        tall = [(h, w) for (w, h) in wide]

    idx = 1
    for orientation, sizes in (("UltraWide", wide), ("UltraTall", tall)):
        for w, h in sizes:
            path = dest / f"{idx:02d}_{orientation}_{w}x{h}_1bit.png"
            idx += 1
            if path.exists():
                continue
            print(f"[GEN ] {path.name}")
            write_stream_png(path, w, h, bit_depth=1, color_type=0, variant=idx)


def generate_metadata(root: Path) -> None:
    dest = root / "10_Metadata_PNG"
    ensure_dir(dest)

    chrm = struct.pack(
        ">8I",
        31270, 32900,  # white
        64000, 33000,  # red
        30000, 60000,  # green
        15000,  6000,  # blue
    )

    cases: list[tuple[str, list[tuple[bytes, bytes]]]] = [
        ("tEXt", [(b"tEXt", make_text("Comment", "YeImageViewer tEXt metadata test"))]),
        ("zTXt", [(b"zTXt", make_ztxt("Comment", "Compressed zTXt metadata test " * 20))]),
        ("iTXt_UTF8", [(b"iTXt", make_itxt("Description", "中文元数据：YeImageViewer PNG iTXt 测试。"))]),
        ("iTXt_Compressed", [(b"iTXt", make_itxt("Description", "压缩 UTF-8 iTXt：" + "测试" * 100, True))]),
        ("gAMA", [(b"gAMA", struct.pack(">I", 45455))]),
        ("pHYs", [(b"pHYs", struct.pack(">IIB", 3780, 3780, 1))]),
        ("tIME", [(b"tIME", struct.pack(">HBBBBB", 2026, 9, 21, 10, 30, 0))]),
        ("sRGB", [(b"sRGB", b"\x00")]),
        ("cHRM", [(b"cHRM", chrm)]),
        ("Multi", [
            (b"sRGB", b"\x00"),
            (b"gAMA", struct.pack(">I", 45455)),
            (b"pHYs", struct.pack(">IIB", 11811, 11811, 1)),
            (b"tEXt", make_text("Software", "YeImageViewer Testset v2")),
            (b"iTXt", make_itxt("Title", "多元数据组合测试：sRGB + gAMA + pHYs + tEXt + iTXt")),
        ]),
    ]

    for i, (name, extras) in enumerate(cases, 1):
        path = dest / f"{i:02d}_{name}.png"
        if path.exists():
            continue
        print(f"[GEN ] {path.name}")
        write_stream_png(
            path, 512, 512,
            bit_depth=8, color_type=2, variant=i,
            extra_chunks=extras, compression_level=6
        )


def parse_chunks(data: bytes):
    if not data.startswith(PNG_SIG):
        raise ValueError("bad signature")
    pos = 8
    out = []
    while pos + 12 <= len(data):
        length = struct.unpack(">I", data[pos:pos+4])[0]
        typ = data[pos+4:pos+8]
        data_start = pos + 8
        data_end = data_start + length
        crc_start = data_end
        crc_end = crc_start + 4
        if crc_end > len(data):
            break
        out.append((pos, length, typ, data_start, data_end, crc_start, crc_end))
        pos = crc_end
    return out


def generate_broken(root: Path) -> None:
    dest = root / "09_Broken_PNG"
    ensure_dir(dest)

    base = dest / "_base_valid.png"
    write_stream_png(base, 256, 256, bit_depth=8, color_type=2, variant=7, compression_level=6)
    data = bytearray(base.read_bytes())
    chunks = parse_chunks(bytes(data))
    ihdr = next(c for c in chunks if c[2] == b"IHDR")
    idat = next(c for c in chunks if c[2] == b"IDAT")
    iend = next(c for c in chunks if c[2] == b"IEND")

    outputs = {}

    # 1 bad signature
    d = bytearray(data)
    d[0] ^= 0xFF
    outputs["01_bad_signature.png"] = bytes(d)

    # 2 truncated header
    outputs["02_truncated_header.png"] = bytes(data[:20])

    # 3 truncate in IDAT payload
    cut = idat[3] + max(1, idat[1] // 2)
    outputs["03_truncated_idat.png"] = bytes(data[:cut])

    # 4 missing IEND
    outputs["04_missing_iend.png"] = bytes(data[:iend[0]])

    # 5 IHDR CRC mismatch
    d = bytearray(data)
    d[ihdr[5]] ^= 0x01
    outputs["05_bad_ihdr_crc.png"] = bytes(d)

    # 6 IDAT CRC mismatch
    d = bytearray(data)
    d[idat[5]] ^= 0x01
    outputs["06_bad_idat_crc.png"] = bytes(d)

    # 7 zero width with recalculated IHDR CRC
    d = bytearray(data)
    ihdr_payload = bytearray(d[ihdr[3]:ihdr[4]])
    ihdr_payload[0:4] = b"\x00\x00\x00\x00"
    d[ihdr[3]:ihdr[4]] = ihdr_payload
    new_crc = binascii.crc32(b"IHDR")
    new_crc = binascii.crc32(ihdr_payload, new_crc) & 0xFFFFFFFF
    d[ihdr[5]:ihdr[6]] = struct.pack(">I", new_crc)
    outputs["07_zero_width_valid_crc.png"] = bytes(d)

    # 8 duplicate IHDR
    ihdr_chunk = bytes(data[ihdr[0]:ihdr[6]])
    d = bytes(data[:ihdr[6]]) + ihdr_chunk + bytes(data[ihdr[6]:])
    outputs["08_duplicate_ihdr.png"] = d

    # 9 unknown critical chunk (uppercase first letter => critical)
    critical = chunk(b"ABCD", b"YeImageViewer unknown critical chunk")
    d = bytes(data[:idat[0]]) + critical + bytes(data[idat[0]:])
    outputs["09_unknown_critical_chunk.png"] = d

    # 10 absurd chunk length immediately after signature
    d = bytearray(data)
    d[8:12] = struct.pack(">I", 0x7FFFFFF0)
    outputs["10_invalid_chunk_length.png"] = bytes(d)

    for name, blob in outputs.items():
        path = dest / name
        print(f"[GEN ] {path.name}")
        path.write_bytes(blob)

    base.unlink(missing_ok=True)


def write_expected(root: Path) -> None:
    p = root / "09_Broken_PNG" / "EXPECTED.txt"
    p.write_text(
        """All 10 files are intentionally malformed.

Expected viewer behavior:
- No crash
- No hang / infinite loop
- No unbounded allocation
- Show a clear decode/load error if possible
- Remain responsive
- Allow Next/Previous to continue to another file

Do NOT treat “successfully displaying every file” as the goal.
""",
        encoding="utf-8",
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--smoke", action="store_true",
                    help="Generate smaller dimensions for generator validation.")
    args = ap.parse_args()

    root = Path(args.root)
    ensure_dir(root)

    generate_200mp(root, args.smoke)
    generate_ultra(root, args.smoke)
    generate_metadata(root)
    generate_broken(root)
    write_expected(root)

    print("[DONE] Generated extreme PNG groups.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
