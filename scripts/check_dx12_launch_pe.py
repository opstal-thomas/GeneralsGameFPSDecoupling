#!/usr/bin/env python3
"""DX12-B10: refuse developer launch executables importing the legacy DX8 API.

This audits the actual built PE import and delay-import tables without pip
dependencies, for both Win32 and x64. Source-level checks alone cannot detect
accidental import of d3d8.dll by a newly linked static library.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import struct


class PEFormatError(ValueError):
    pass


def u16(data: bytes, position: int) -> int:
    if position < 0 or position + 2 > len(data):
        raise PEFormatError("16-bit field outside PE file")
    return struct.unpack_from("<H", data, position)[0]


def u32(data: bytes, position: int) -> int:
    if position < 0 or position + 4 > len(data):
        raise PEFormatError("32-bit field outside PE file")
    return struct.unpack_from("<I", data, position)[0]


def null_string(data: bytes, position: int) -> str:
    if position < 0 or position >= len(data):
        raise PEFormatError("DLL name outside PE file")
    end = data.find(b"\x00", position, min(len(data), position + 512))
    if end < 0:
        raise PEFormatError("DLL name is not NUL-terminated")
    try:
        return data[position:end].decode("ascii").lower()
    except UnicodeDecodeError as exc:
        raise PEFormatError("DLL import name is not ASCII") from exc


def imports_from_pe(path: Path) -> tuple[str, list[str]]:
    data = path.read_bytes()
    if len(data) < 256 or data[:2] != b"MZ":
        raise PEFormatError("Missing DOS MZ header")
    pe_offset = u32(data, 0x3C)
    if pe_offset + 24 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\x00\x00":
        raise PEFormatError("Missing PE signature")
    machine = u16(data, pe_offset + 4)
    sections_count = u16(data, pe_offset + 6)
    optional_size = u16(data, pe_offset + 20)
    optional = pe_offset + 24
    if optional + optional_size > len(data):
        raise PEFormatError("Truncated PE optional header")

    magic = u16(data, optional)
    if machine == 0x14C and magic == 0x10B:
        arch = "Win32"
        directory_offset = 96
        image_base = u32(data, optional + 28)
    elif machine == 0x8664 and magic == 0x20B:
        arch = "x64"
        directory_offset = 112
        image_base = struct.unpack_from("<Q", data, optional + 24)[0]
    else:
        raise PEFormatError(f"Unexpected machine/optional-header pair: {machine:04X}/{magic:04X}")

    sections = []
    section_table = optional + optional_size
    for i in range(sections_count):
        o = section_table + i * 40
        if o + 40 > len(data):
            raise PEFormatError("Truncated section header")
        virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, o + 8)
        sections.append((rva, max(virtual_size, raw_size), raw_size, raw_offset))

    header_size = u32(data, optional + 60)

    def map_rva(rva: int, length: int = 1) -> int:
        if rva < header_size and rva + length <= len(data):
            return rva
        for section_rva, virtual_length, raw_length, raw in sections:
            delta = rva - section_rva
            if 0 <= delta and delta + length <= virtual_length and delta + length <= raw_length:
                offset = raw + delta
                if offset + length > len(data):
                    raise PEFormatError("Mapped RVA exceeds PE file")
                return offset
        raise PEFormatError(f"Unmapped RVA 0x{rva:X}")

    if optional_size < directory_offset + 14 * 8:
        raise PEFormatError("Missing PE data directories")

    found: set[str] = set()
    # IMAGE_DIRECTORY_ENTRY_IMPORT=1, IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT=13
    for kind in (1, 13):
        rva, size = struct.unpack_from("<II", data, optional + directory_offset + kind * 8)
        if not rva or not size:
            continue
        stride = 20 if kind == 1 else 32
        terminated = False
        for i in range(4096):
            offset = map_rva(rva + i * stride, stride)
            entries = struct.unpack_from("<" + "I" * (stride // 4), data, offset)
            if not any(entries):
                terminated = True
                break
            name_rva = entries[3] if kind == 1 else entries[1]
            if kind == 13 and not (entries[0] & 1):
                name_rva -= image_base
            found.add(null_string(data, map_rva(name_rva)))
        if not terminated:
            raise PEFormatError("Unterminated PE import descriptor table")

    return arch, sorted(found)


def validate(path: Path) -> None:
    arch, imports = imports_from_pe(path)
    forbidden = [name for name in imports if name.startswith(("d3d8", "d3dx8"))]
    if forbidden:
        raise PEFormatError(f"DX12-B10 executable imports legacy DX8 DLLs: {forbidden}")
    if "d3d12.dll" not in imports or "dxgi.dll" not in imports:
        raise PEFormatError("DX12-B10 executable must import both d3d12.dll and dxgi.dll")
    print(f"DX12-B10 PE IMPORT AUDIT PASS ({arch}): {path.name}")
    print("  D3D12 and DXGI: present; Direct3D 8 and D3DX8: absent")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args()
    try:
        validate(args.exe)
    except (OSError, PEFormatError) as exc:
        parser.exit(1, f"DX12-B10 PE IMPORT AUDIT FAILED: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
