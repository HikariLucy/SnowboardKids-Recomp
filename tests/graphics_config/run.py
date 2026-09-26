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
 # VSync: default On, persisted with every window mode, aspect and scale.
 j=run(Path(tmp)/'defaults')
 assert j['vsync']=='On' and j['applied_vsync']==0 and not j['hidden']['vsync'],j
 for res in (0,4):
  for aspect in range(2):
   for wm in range(2):
    for vs in range(2):
     p=Path(tmp)/f'vsync-{res}-{aspect}-{wm}-{vs}'
     a=run(p,'save',res,aspect,0,wm,vs); bval=run(p)
     assert a==bval and bval['vsync']==['On','Off'][vs] and bval['applied_vsync']==vs,bval
     assert bval['applied_wm']==wm and bval['applied_ar']==aspect and bval['res_option']==keys[res],bval
     assert json.loads((p/'graphics.json').read_text())['vsync']==['On','Off'][vs]
 # Process A: Off; process B: loads Off, sets On; process C: loads On.
 p=Path(tmp)/'vsync-abc'
 assert run(p,'save',2,0,0,0,1)['applied_vsync']==1
 pb=run(p); assert pb['vsync']=='Off' and pb['applied_vsync']==1,pb
 assert run(p,'save',2,0,0,0,0)['applied_vsync']==0
 cval=run(p); assert cval['vsync']=='On' and cval['applied_vsync']==0,cval
 # A window-mode toggle (F11 / Alt+Enter path) must not reset VSync.
 for wm in range(2):
  p=Path(tmp)/f'vsync-toggle-{wm}'
  run(p,'save',2,1,0,wm,1); t=run(p,'toggle')
  assert t['applied_wm']==1-wm and t['applied_vsync']==1 and t['vsync']=='Off' and t['ar_option']=='Expand',t
 # Invalid VSync values fall back to On; an old high-FPS request stays Original.
 for idx,value in enumerate([None,'maybe','',-1,2,4294967296,18446744073709551615,1.5,True,{},[],'OFF ','Adaptive','Mailbox']):
  p=Path(tmp)/f'vsync-bad{idx}';p.mkdir()
  (p/'graphics.json').write_text(json.dumps({'vsync':value,'rr_option':'Display','rr_manual_value':240,'wm_option':'Fullscreen'}))
  j=run(p); assert j['vsync']=='On' and j['applied_vsync']==0,(value,j)
  assert j['rr_option']=='Original' and j['applied_rr']==0 and j['applied_wm']==1,(value,j)
 p=Path(tmp)/'vsync-missing';p.mkdir()
 (p/'graphics.json').write_text(json.dumps({'res_option':'1080p','ar_option':'Expand','rr_option':'Manual'}))
 j=run(p); assert j['vsync']=='On' and j['applied_vsync']==0 and j['applied_rr']==0 and j['res_option']=='1080p',j
 # Capability gating: Off is disabled (On never) when the swap chain cannot present unsynchronized.
 p=Path(tmp)/'vsync-caps';p.mkdir()
 proc=subprocess.run([str(b),'caps'],env={**os.environ,'SBK_CONFIG_TEST_DIR':str(p)},text=True,capture_output=True)
 assert proc.returncode==0,proc.stderr
 caps=json.loads(proc.stdout)
 assert caps['off_disabled']==1 and caps['on_disabled']==0 and caps['details'] and caps['off_disabled_after']==0 and caps['details_after']=='',caps
 print('PASS graphics config: process A/B 42 combinations, malformed values, original timing, backend bound')
 print('PASS vsync: default On, 16 window/aspect/scale combinations, A/B/C persistence, toggle keeps VSync, 15 invalid values, capability gating')
