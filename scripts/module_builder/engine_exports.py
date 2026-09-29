"""Engine export surface shared by the engine .def (CMake) and Windows module import library."""

from pathlib import Path
import re
from typing import List

ENGINE_EXECUTABLE_NAME = "SnowboardKidsEngine.exe"
EXPORT_LIST_RELATIVE = Path("src") / "module" / "engine_exports.inc"
# Compiled into the RSP microcode unit only: the DLL reads the engine's DMEM
# through the pointer handed over by SbkEngineApiV1 (see game_module_entry.cpp).
WINDOWS_RSP_DEFINES = ("dmem=(*sbk_module_dmem)",)

_ENTRY = re.compile(r"^SBK_ENGINE_EXPORT(?:_C)?\(([A-Za-z_][A-Za-z0-9_]*)\)\s*$")


def read_engine_exports(root: Path) -> List[str]:
    """Symbols the engine exports to the game module, in declaration order."""
    path = root / EXPORT_LIST_RELATIVE
    symbols = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = _ENTRY.match(line.strip())
        if match:
            symbols.append(match.group(1))
    if not symbols:
        raise ValueError(f"no engine exports found in {path}")
    if len(set(symbols)) != len(symbols):
        raise ValueError(f"duplicate engine exports in {path}")
    return symbols


def import_definition_text(symbols: List[str]) -> str:
    """Module-definition file describing SnowboardKidsEngine.exe's exports."""
    lines = [f"NAME {ENGINE_EXECUTABLE_NAME}", "EXPORTS"]
    lines.extend(f"    {symbol}" for symbol in symbols)
    return "\n".join(lines) + "\n"
