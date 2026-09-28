#!/usr/bin/env python3
"""UI-MOD-01 regression: the options menu cannot build a ModMenu while mods are off.

Compiles the tab policy test (no RmlUi, no window) and checks the structural
facts it relies on: RecompFrontend builds a ModMenu only from the Mods tab, the
boot path registers that tab only through the policy (after setting the game
mod id), and this game registers no mod id (mods not enabled yet).
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1].parent
FRONTEND = ROOT / '.deps-renderer/RecompFrontend/recompui/src'


def check(ok, what):
    print(f"{'PASS' if ok else 'FAIL'} {what}", flush=True)
    return ok


def main():
    ok = True
    builders = sorted(str(p.relative_to(FRONTEND)) for p in FRONTEND.rglob('*.cpp')
                      if 'create_element<ModMenu>' in p.read_text(errors='replace'))
    ok &= check(builders == ['config/ui_config_tab_mods.cpp'], f'ModMenu is built only by the Mods tab ({builders})')

    boot = (ROOT / 'src/main/native_boot.cpp').read_text()
    ok &= check(boot.count('create_mods_tab(') == 1 and 'case sbk::frontend::ConfigTab::Mods:' in boot,
                'boot registers the Mods tab only through config_tabs()')
    mods_case = boot[boot.index('case sbk::frontend::ConfigTab::Mods:'):boot.index('create_mods_tab(')]
    ok &= check('update_game_mod_id(game.mod_game_id)' in mods_case, 'game mod id is set before the Mods tab exists')
    ok &= check(re.search(r'\.mod_game_id\s*=\s*""', boot) is not None, 'Snowboard Kids registers no mod_game_id (mods off)')

    with tempfile.TemporaryDirectory(prefix='sbk-frontend-') as directory:
        binary = Path(directory) / 'config_tabs'
        subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-I' + str(ROOT / 'src'),
                        str(ROOT / 'tests/frontend/config_tabs.cpp'), '-o', str(binary)], check=True, timeout=300)
        ok &= subprocess.run([str(binary)], timeout=60).returncode == 0
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
