#!/usr/bin/env python3
"""List the DLLs a PE32+ image imports (normal and delay-load), without dumpbin.

Release staging runs on any host, so the Windows runtime dependency set is read
straight from the import directories instead of assumed.
"""
import argparse
from pathlib import Path
import struct
import sys
from typing import Dict, List

IMAGE_DIRECTORY_ENTRY_IMPORT = 1
IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT = 13
PE32_PLUS_MAGIC = 0x20B
IMAGE_FILE_MACHINE_AMD64 = 0x8664


class PeFormatError(ValueError):
    pass


def _rva_to_offset(rva: int, sections) -> int:
    for virtual_address, virtual_size, raw_offset, raw_size in sections:
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            return raw_offset + (rva - virtual_address)
    raise PeFormatError(f"RVA 0x{rva:X} is outside every section")


def _c_string(data: bytes, offset: int) -> str:
    end = data.find(b"\0", offset)
    if end < 0:
        raise PeFormatError("unterminated import name")
    return data[offset:end].decode("ascii")


def read_imports(data: bytes) -> Dict[str, List[str]]:
    """Return {'machine': ..., 'imports': [...], 'delay_imports': [...]} for a PE32+ image."""
    if data[:2] != b"MZ" or len(data) < 0x40:
        raise PeFormatError("not an MZ executable")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise PeFormatError("missing PE signature")
    machine, section_count = struct.unpack_from("<HH", data, pe + 4)
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    if struct.unpack_from("<H", data, optional)[0] != PE32_PLUS_MAGIC:
        raise PeFormatError("only PE32+ (x64) images are supported")
    directory_count = struct.unpack_from("<I", data, optional + 108)[0]
    directories = [struct.unpack_from("<II", data, optional + 112 + 8 * i) for i in range(directory_count)]
    table = optional + optional_size
    sections = []
    for i in range(section_count):
        header = table + 40 * i
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, header + 8)
        sections.append((virtual_address, virtual_size, raw_offset, raw_size))

    def names(entry: int, descriptor_size: int, name_field: int) -> List[str]:
        if entry >= len(directories) or directories[entry][0] == 0:
            return []
        offset = _rva_to_offset(directories[entry][0], sections)
        found = []
        while True:
            descriptor = data[offset:offset + descriptor_size]
            if len(descriptor) < descriptor_size:
                raise PeFormatError("truncated import directory")
            if descriptor == b"\0" * descriptor_size:
                return found
            name_rva = struct.unpack_from("<I", descriptor, name_field)[0]
            found.append(_c_string(data, _rva_to_offset(name_rva, sections)))
            offset += descriptor_size

    return {
        "machine": machine,
        "imports": names(IMAGE_DIRECTORY_ENTRY_IMPORT, 20, 12),
        "delay_imports": names(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT, 32, 4),
    }


def read_imports_file(path: Path) -> Dict[str, List[str]]:
    return read_imports(Path(path).read_bytes())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="+", type=Path)
    args = parser.parse_args()
    for image in args.images:
        info = read_imports_file(image)
        print(f"{image.name}:")
        for name in info["imports"]:
            print(f"  {name}")
        for name in info["delay_imports"]:
            print(f"  {name} (delay-load)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
