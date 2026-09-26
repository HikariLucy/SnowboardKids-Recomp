#!/usr/bin/env python3
"""Real graphics schema + Config/file persistence; only host UI/renderer seams stubbed."""
from pathlib import Path
import json, os, subprocess, tempfile
R=Path(__file__).resolve().parents[2]
rt=R/'.deps-runtime/N64ModernRuntime'; fe=R/'.deps-renderer/RecompFrontend'; render=R/'.deps-renderer/rt64'
incs=[fe/"lib/GamepadMotionHelpers",Path("/usr/include/SDL2"),rt/'librecomp/include',rt/'librecomp/include/librecomp',rt/'ultramodern/include',rt/'N64Recomp/include',rt/'thirdparty',rt/'thirdparty/concurrentqueue',rt/'thirdparty/miniz',fe/'recompui/include',fe/'recompui/src',fe/'recompinput/include',fe/'recompui/lib/RmlUi/Include',render/'src']
with tempfile.TemporaryDirectory(prefix='sbk-graphics-') as tmp:
 b=Path(tmp)/'config'
 (Path(tmp)/'miniz_export.h').write_text('#define MINIZ_EXPORT\n')
 incs.append(Path(tmp))
 cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-g','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-Wl,--gc-sections']+['-I'+str(i) for i in incs]+[str(R/'tests/graphics_config/config.cpp'),str(fe/'recompui/src/config/ui_config_tab_graphics.cpp'),str(rt/'librecomp/src/config.cpp'),str(rt/'librecomp/src/config_option.cpp'),str(rt/'librecomp/src/files.cpp'),'-o',str(b)]
 subprocess.run(cmd,check=True)
 def run(path,*args):
  path.mkdir(exist_ok=True)
  p=subprocess.run([str(b),*map(str,args)],env={**os.environ,'SBK_CONFIG_TEST_DIR':str(path)},text=True,capture_output=True)
  assert p.returncode==0,p.stderr
  return json.loads(p.stdout)
 p=Path(tmp)/'defaults'; j=run(p)
 assert j['rr_option']=='Original' and j['applied_rr']==0,j
 assert j['hidden']['rr_option'] and j['hidden']['rr_manual_value']
 assert j['backend_ds_bad']==1,j
 keys=['Original','Original2x','Auto','720p','1080p','1440p','2160p']
 for n,key in enumerate(keys):
  for aspect in range(2):
   for ds in (0,2,4):
    wm=(n+aspect+ds)%2
    p=Path(tmp)/f'{n}-{aspect}-{ds}'
    a=run(p,'save',n,aspect,ds,wm); bval=run(p)
    assert a==bval and bval['res_option']==key and bval['ds_option']==ds,bval
    assert bval['ar_option']==['Original','Expand'][aspect] and bval['applied_wm']==wm,bval
 for idx,value in enumerate([-1,1,3,4294967298,18446744073709551615,2.5,'2',None,{},[],True]):
  p=Path(tmp)/f'bad{idx}';p.mkdir()
  (p/'graphics.json').write_text(json.dumps({'ds_option':value,'rr_option':'Manual','rr_manual_value':1e100,'res_option':'invalid','ar_option':42}))
  j=run(p); assert j['ds_option']==0 and j['applied_ds']==0 and j['applied_rr']==0 and j['rr_option']=='Original',j
  assert 20<=j['applied_rr_manual']<=240 and j['res_option']=='Auto' and j['ar_option']=='Original',j
 for idx,text in enumerate(['{', '[]','"hello"','null','42']):
  p=Path(tmp)/f'malformed{idx}';p.mkdir();(p/'graphics.json').write_text(text)
  j=run(p);assert j['ds_option']==0 and j['applied_rr']==0,j
 print('PASS graphics config: process A/B 42 combinations, malformed values, original timing, backend bound')
