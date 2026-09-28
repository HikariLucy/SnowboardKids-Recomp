#!/usr/bin/env python3
"""Fresh-process continuation proof; output equality includes every RDRAM byte."""
import pathlib
import subprocess
import sys
import tempfile

exe = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='sbk-p1-') as tmp:
    tmp = pathlib.Path(tmp)
    def run(*args, ok=True):
        p = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True)
        if (p.returncode == 0) != ok:
            raise AssertionError(f'{args}: exit {p.returncode}\n{p.stdout}{p.stderr}')
        if ok:
            print(p.stdout.strip())
    for fr in (0, 1):
        checkpoint = tmp / 'continuation.dat'
        reference, restored = tmp / 'reference.dat', tmp / 'restored.dat'
        run('reference', reference, fr)
        run('export', checkpoint, fr)  # process exits with blocked thread destroyed
        run('import', checkpoint, restored)
        assert reference.read_bytes() == restored.read_bytes(), 'guest state mismatch'
        data = checkpoint.read_bytes()
        for label, damaged in [('truncated', data[:-1]), ('magic', b'BAD!' + data[4:]),
                               ('trailing', data + b'x'),
                               ('build', data[:8] + bytes([data[8] ^ 1]) + data[9:]),
                               ('version', data[:4] + b'\x02\0\0\0' + data[8:])]:
            bad = tmp / label
            bad.write_bytes(damaged)
            run('import', bad, restored, ok=False)
        print(f'PASS FR={fr}: separate-process resume equals native-call baseline')
    run('selftest')
