#!/usr/bin/env python3
"""Extract one function from the user's ELF; never commit game instructions."""
import pathlib
import struct
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
if data[:6] != b'\x7fELF\x01\x02':
    raise SystemExit('Expected big-endian ELF32')
u16 = lambda off: struct.unpack_from('>H', data, off)[0]
u32 = lambda off: struct.unpack_from('>I', data, off)[0]
sections = [struct.unpack_from('>10I', data, u32(32) + i * u16(46)) for i in range(u16(48))]
for section in sections:
    if section[1] != 2:
        continue
    strings = sections[section[6]]
    names = data[strings[4]:strings[4] + strings[5]]
    for off in range(section[4], section[4] + section[5], section[9]):
        name, addr, size, info, other, index = struct.unpack_from('>IIIBBH', data, off)
        if names[name:].split(b'\0', 1)[0] != b'requestControllerPakFreeSpaceUpdate':
            continue
        if addr != 0x80001858 or size != 0x44:
            raise SystemExit(f'Unexpected function identity: {addr:#x}/{size:#x}')
        source = sections[index]
        start = source[4] + addr - source[3]
        words = struct.unpack_from('>' + 'I' * (size // 4), data, start)
        pathlib.Path(sys.argv[2]).write_text('\n'.join(f'{w:08x}' for w in words) + '\n')
        print(f'Extracted local ELF function at {addr:#x}, {size} bytes')
        sys.exit(0)
raise SystemExit('Function not found in matching ELF')
