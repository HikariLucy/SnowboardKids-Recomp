#!/usr/bin/env python3
from pathlib import Path
import argparse
import os
import subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');args=p.parse_args()
build=ROOT/'build-tools/production-continuation';build.mkdir(parents=True,exist_ok=True)
recomp=ROOT/'.deps/N64Recomp';libs=ROOT/'build-tools/n64recomp'
cxx=os.environ.get('CXX','c++')
def run(args):subprocess.run([str(x) for x in args],cwd=ROOT,check=True,timeout=180)
inc=['-I'+str(recomp/x) for x in ['include','lib/rabbitizer/include','lib/rabbitizer/cplusplus/include','lib/fmt/include']]
archives=[libs/x for x in ['libN64Recomp.a','libSymbolLists.a','librabbitizer.a','lib/fmt/libfmt.a']]
run([cxx,'-std=c++20','-O2','-I'+str(ROOT/'tools/continuation'),*inc,ROOT/'tests/production_continuation/generate_flow.cpp',*archives,'-o',build/'generate_flow'])
run([build/'generate_flow',build/'flow.cpp'])
flags=['-O2'] if not args.sanitize else ['-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
run([cxx,'-std=c++20',*flags,'-fno-strict-aliasing','-I'+str(ROOT/'src'),'-I'+str(recomp/'include'),ROOT/'tests/production_continuation/flow.cpp',build/'flow.cpp',ROOT/'src/continuation/dispatch.cpp','-o',build/'flow'])
run([build/'flow'])
