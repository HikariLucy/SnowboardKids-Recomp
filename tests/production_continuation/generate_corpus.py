#!/usr/bin/env python3
"""Generate/compile the actual corpus into ignored output, preserving the native backend."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse
import csv
import hashlib
import json
import os
import subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--compile',action='store_true');args=p.parse_args()
build=ROOT/'build-tools/production-continuation';build.mkdir(parents=True,exist_ok=True)
out=build/'corpus';out.mkdir(exist_ok=True)
# Keep every production option; only relocate paths for the temporary config.
config=(ROOT/'us.toml').read_text().replace('"../snowboardkids-decomp/build/snowboardkids.elf"',json.dumps(str(ROOT.parent/'snowboardkids-decomp/build/snowboardkids.elf'))).replace('"RecompiledFuncs"',json.dumps(str(out)))
config_path=build/'corpus.toml';config_path.write_text(config)
log=build/'generation.log'
with log.open('w') as stream:
    subprocess.run([str(build/'generate'),str(config_path),str(build/'manifest.tsv')],cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,check=True,timeout=180)
rows=list(csv.DictReader((build/'manifest.tsv').open(),delimiter='\t'))
functions=[r for r in rows if r['kind']=='function']
hle=[r for r in rows if r['kind']=='hle']
print(f'Generated {len(functions)} functions; {len(hle)} distinct direct HLE targets. Native leaves: none classified.')
if args.compile:
    object_dir=build/'objects';object_dir.mkdir(exist_ok=True)
    def compile(path):
        cmd=[os.environ.get('CXX','c++'),'-std=c++20','-O0','-fno-strict-aliasing','-Wno-unused-variable','-Wno-unused-but-set-variable','-I'+str(ROOT/'src'),'-I'+str(ROOT/'.deps/N64Recomp/include'),'-I'+str(out),'-x','c++','-c',str(path),'-o',str(object_dir/(path.stem+'.o'))]
        result=subprocess.run(cmd,text=True,capture_output=True,timeout=180)
        if result.returncode:raise RuntimeError(path.name+':\n'+result.stderr)
    sources=sorted(out.glob('funcs_*.c'))
    with ThreadPoolExecutor(max_workers=4) as pool:list(pool.map(compile,sources))
    print(f'Compiled {len(sources)} corpus translation units.')
print('Manifest SHA256:',hashlib.sha256((build/'manifest.tsv').read_bytes()).hexdigest())
print('P4-A real-game gate: NOT RUN (generation/compilation is insufficient).')
