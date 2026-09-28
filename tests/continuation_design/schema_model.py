"""Executable P1.5 schema specification, NOT a production continuation codec.

All inputs are stable guest/build metadata. Host storage, load addresses, discovery
indices, compiler layout and absolute source paths are intentionally absent.
"""
import hashlib
import json

PHASES = {'entry': 0, 'after_call': 1, 'backedge': 2, 'hle_pending': 3,
          'hle_committed': 4, 'tail_pending': 5}
VARIANTS = {'ordinary': 0, 'taken_slot': 1, 'fallthrough_slot': 2}

def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode('ascii')

def function_id(image_sha256, section_rom, section_size, offset, role='original'):
    if len(image_sha256) != 64 or any(c not in '0123456789abcdef' for c in image_sha256):
        raise ValueError('invalid image digest')
    if not (0 <= section_rom <= 0xffffffff and 0 < section_size <= 0xffffffff and
            0 <= offset < section_size and offset % 4 == 0):
        raise ValueError('invalid guest identity')
    if role not in ('original', 'replacement', 'patch'):
        raise ValueError('unregistered role')
    key = ['p15-function-v1', image_sha256, section_rom, section_size, offset, role]
    return hashlib.sha256(canonical(key)).hexdigest()[:32]

def label(offset, phase, variant='ordinary', owner_offset=None):
    if not (0 <= offset <= 0xffffffff and offset % 4 == 0):
        raise ValueError('invalid instruction offset')
    owner = 0xffffffff if owner_offset is None else owner_offset
    if owner != 0xffffffff and not (0 <= owner <= 0xfffffffc and owner % 4 == 0):
        raise ValueError('invalid owning transfer offset')
    return (offset, owner, PHASES[phase], VARIANTS[variant])

def manifest(functions):
    """Reject duplicate identities/labels/fields, normalize discovery order."""
    result = []
    seen = set()
    for fid, points in functions:
        if fid in seen:
            raise ValueError('duplicate function identity or hash collision')
        seen.add(fid)
        point_ids = set()
        normalized = []
        for lid, fields in points:
            if lid in point_ids:
                raise ValueError('duplicate continuation label')
            point_ids.add(lid)
            names = [name for name, scalar in fields]
            if len(set(names)) != len(names):
                raise ValueError('duplicate local')
            if any(scalar not in ('u32', 'i32', 'u64', 'f32_bits', 'f64_bits') for _, scalar in fields):
                raise ValueError('unsupported or native local type')
            normalized.append([lid, sorted(fields)])
        result.append([fid, sorted(normalized)])
    return canonical({'abi': 'p15-design-v1', 'functions': sorted(result)})
