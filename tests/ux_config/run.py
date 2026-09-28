#!/usr/bin/env python3
"""Audio, input and accessibility settings: real schemas + Config/JSON persistence.

Process A saves, process B loads the exact values; malformed and out-of-range
files fall back to defaults. Compiled with ASan/UBSan like tests/graphics_config.
"""
from pathlib import Path
import json
import math
import os
import subprocess
import tempfile

R = Path(__file__).resolve().parents[2]
rt = R / '.deps-runtime/N64ModernRuntime'
fe = R / '.deps-renderer/RecompFrontend'
incs = [fe / 'lib/GamepadMotionHelpers', Path('/usr/include/SDL2'), rt / 'librecomp/include',
        rt / 'librecomp/include/librecomp', rt / 'ultramodern/include', rt / 'N64Recomp/include',
        rt / 'thirdparty', rt / 'thirdparty/concurrentqueue', rt / 'thirdparty/miniz',
        fe / 'recompui/include', fe / 'recompui/src', fe / 'recompinput/include', fe / 'recompinput/include/recompinput',
        fe / 'recompui/lib/RmlUi/Include', R / '.deps-renderer/rt64/src', R / 'src']
DEFAULTS = {'main_volume': 100, 'reduced_motion': 'Off', 'joystick_deadzone': 5, 'rumble_strength': 25}

with tempfile.TemporaryDirectory(prefix='sbk ux config ') as tmp:
    tmp = Path(tmp)
    binary = tmp / 'config'
    (tmp / 'miniz_export.h').write_text('#define MINIZ_EXPORT\n')
    incs.append(tmp)
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++20', '-g', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    *['-I' + str(i) for i in incs],
                    str(R / 'tests/ux_config/config.cpp'), str(R / 'src/ui/ux_settings.cpp'),
                    str(fe / 'recompui/src/config/ui_config_tab_sound.cpp'),
                    str(fe / 'recompui/src/config/ui_config_tab_general.cpp'),
                    str(fe / 'recompinput/src/input_types.cpp'),
                    str(rt / 'librecomp/src/config.cpp'), str(rt / 'librecomp/src/config_option.cpp'),
                    str(rt / 'librecomp/src/files.cpp'), '-lSDL2', '-o', str(binary)], check=True)

    def run(path, *args):
        path.mkdir(exist_ok=True)
        p = subprocess.run([str(binary), *map(str, args)], env={**os.environ, 'SBK_CONFIG_TEST_DIR': str(path)},
                           text=True, capture_output=True)
        assert p.returncode == 0, p.stderr
        return json.loads(p.stdout)

    def values(j):
        return {'main_volume': j['sound']['main_volume'], 'reduced_motion': j['accessibility']['reduced_motion'],
                'joystick_deadzone': j['general']['joystick_deadzone'],
                'rumble_strength': j['general']['rumble_strength']}

    # Defaults: 100% volume is unity gain, Reduced Motion off, files created in user config only.
    d = tmp / 'defaults'
    j = run(d)
    assert values(j) == DEFAULTS and j['gain'] == 1.0 and j['reduced_motion'] is False, j
    assert sorted(p.name for p in d.iterdir() if p.suffix == '.json') == ['accessibility.json', 'general.json', 'sound.json']

    # Process A saves; process B loads the exact values and applies them.
    cases = [(0, 1, 0, 0), (35, 1, 12, 0), (50, 0, 30, 60), (100, 1, 95, 100), (1, 0, 0, 25)]
    for n, (volume, reduced, deadzone, rumble) in enumerate(cases):
        p = tmp / f'persist {n}'
        run(p, 'save', volume, reduced, deadzone, rumble)
        b = run(p)
        assert values(b) == {'main_volume': volume, 'reduced_motion': ['Off', 'On'][reduced],
                             'joystick_deadzone': deadzone, 'rumble_strength': rumble}, b
        assert math.isclose(b['gain'], volume / 100, rel_tol=1e-6) and b['reduced_motion'] is bool(reduced), b
        assert b['recompui_volume'] == volume and b['recompui_deadzone'] == deadzone, b
        assert b['reduced_motion_calls'] == 1, b
    # A -> B -> C: B changes the values again, C sees B's values.
    p = tmp / 'abc'
    run(p, 'save', 20, 1, 10, 0)
    assert values(run(p))['main_volume'] == 20
    run(p, 'save', 80, 0, 7, 40)
    c = run(p)
    assert values(c) == {'main_volume': 80, 'reduced_motion': 'Off', 'joystick_deadzone': 7, 'rumble_strength': 40}, c

    # Out-of-range, non-numeric and wrong-type values fall back to each default.
    bad_numbers = [-1, -0.01, 100.5, 101, 1e308, -1e308, '50', None, True, {}, []]
    for n, value in enumerate(bad_numbers):
        p = tmp / f'bad number {n}'
        p.mkdir()
        for name, key in (('sound', 'main_volume'), ('general', 'joystick_deadzone'), ('general', 'rumble_strength')):
            f = p / f'{name}.json'
            data = json.loads(f.read_text()) if f.exists() else {}
            data[key] = value
            f.write_text(json.dumps(data))
        j = run(p)
        assert values(j) == DEFAULTS and j['gain'] == 1.0, (value, j)
    # The deadzone's real backend range stops at 95%.
    p = tmp / 'deadzone 96'
    p.mkdir()
    (p / 'general.json').write_text(json.dumps({'joystick_deadzone': 96}))
    assert values(run(p))['joystick_deadzone'] == 5
    # librecomp matches enum keys case-insensitively by design.
    for n, value in enumerate(['on', 'ON']):
        p = tmp / f'case reduced {n}'
        p.mkdir()
        (p / 'accessibility.json').write_text(json.dumps({'reduced_motion': value}))
        assert run(p)['reduced_motion'] is True, value
    for n, value in enumerate(['', 1, True, None, {}, [], 'Maybe', 'Onn']):
        p = tmp / f'bad reduced {n}'
        p.mkdir()
        (p / 'accessibility.json').write_text(json.dumps({'reduced_motion': value}))
        j = run(p)
        assert j['accessibility']['reduced_motion'] == 'Off' and j['reduced_motion'] is False, (value, j)
    # Malformed files never crash startup.
    for n, text in enumerate(['{', '[]', '"hello"', 'null', '42', '', '\x00\xff', '{"main_volume": NaN}']):
        p = tmp / f'malformed {n}'
        p.mkdir()
        for name in ('sound', 'general', 'accessibility'):
            (p / f'{name}.json').write_text(text, errors='surrogateescape')
        assert values(run(p)) == DEFAULTS

    labels = run(tmp / 'labels', 'labels')
    assert labels == {'not_assigned': 'Not assigned', 'keyboard': 'Keyboard', 'named': 'Xbox Wireless Controller',
                      'unnamed': 'Controller', 'empty': 'Controller',
                      'disconnected': 'Disconnected - reconnect, then Re-assign Players',
                      'none': 'No controller connected'}, labels

    names = run(tmp / 'names', 'names')
    expected = {'X_AXIS_NEG': 'Analog Stick Left', 'X_AXIS_POS': 'Analog Stick Right',
                'Y_AXIS_NEG': 'Analog Stick Down', 'Y_AXIS_POS': 'Analog Stick Up',
                'A': 'N64 A', 'B': 'N64 B', 'Z': 'Z Trigger', 'L': 'L Button', 'R': 'R Button',
                'START': 'Start', 'C_UP': 'C Up', 'C_DOWN': 'C Down', 'C_LEFT': 'C Left', 'C_RIGHT': 'C Right',
                'DPAD_UP': 'D-Pad Up', 'DPAD_DOWN': 'D-Pad Down', 'DPAD_LEFT': 'D-Pad Left',
                'DPAD_RIGHT': 'D-Pad Right', 'TOGGLE_MENU': 'Open / Close Menu', 'ACCEPT_MENU': 'Menu: Confirm',
                'BACK_MENU': 'Menu: Back', 'APPLY_MENU': 'Menu: Apply', 'TAB_LEFT_MENU': 'Menu: Previous Tab',
                'TAB_RIGHT_MENU': 'Menu: Next Tab'}
    # Every input is named, names are unique, and controls.json keys (enum names) are unchanged.
    assert names == expected and len(set(names.values())) == len(names), names

    print(f'PASS ux settings: defaults, {len(cases)} A/B cases + A/B/C, '
          f'{len(bad_numbers)} invalid numbers x3 options, deadzone range, 8 invalid Reduced Motion + case-insensitive keys, 8 malformed files')
    print('PASS input names: 24 inputs, unique, enum keys unchanged')
    print('PASS device labels: SDL name used as-is, no guessed names, disconnected and absent states in words')
