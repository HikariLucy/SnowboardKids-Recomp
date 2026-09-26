"""Single source of truth for upstream repositories, commits and patch order."""
from dataclasses import dataclass
import hashlib
from pathlib import Path


@dataclass(frozen=True)
class Dependency:
    path: str
    url: str
    commit: str
    patches: tuple[str, ...] = ()


DEPENDENCIES = {
    'recomp': Dependency('.deps/N64Recomp', 'https://github.com/N64Recomp/N64Recomp.git',
        'ffb39cdad1da5de07eaaa48bd1db4a89a7986771',
        ('n64recomp-continuations.patch',)),
    'runtime': Dependency('.deps-runtime/N64ModernRuntime',
        'https://github.com/cdlewis/N64ModernRuntime.git',
        '6ccb2e7c2e7f6708257b461097e0aaf03c445e2a',
        ('n64modernruntime-osstopthread.patch', 'n64modernruntime-quiescence.patch',
         'n64modernruntime-continuations.patch', 'n64modernruntime-savestate.patch',
         'n64modernruntime-shutdown.patch', 'n64modernruntime-input-neutral.patch',
         'n64modernruntime-config-shape.patch', 'n64modernruntime-vsync.patch')),
    'rt64': Dependency('.deps-renderer/rt64', 'https://github.com/cdlewis/rt64.git',
        '6a4166b2cfa952d931a08481d1037da995f28b54',
        ('rt64-quiescence.patch', 'rt64-aspect-coverage.patch',
         'rt64-vsync-presentation.patch')),
    'frontend': Dependency('.deps-renderer/RecompFrontend',
        'https://github.com/cdlewis/RecompFrontend.git',
        'e85b912d9df677b04f9358867dd010c8af27ea05',
        ('recompfrontend-resolution.patch', 'recompfrontend-quiescence.patch',
         'recompfrontend-input.patch', 'recompfrontend-graphics-options.patch',
         'recompfrontend-theme-focus.patch', 'recompfrontend-vsync.patch')),
    'theme': Dependency('.deps-renderer/recomp-theme',
        'https://github.com/cdlewis/snowboardkids-recomp-theme.git',
        '0cb9a83a263607fbc8ab6176a758a00726e237cc'),
}


def compute_lock_digest() -> str:
    hasher = hashlib.sha256()
    for name in sorted(DEPENDENCIES.keys()):
        dep = DEPENDENCIES[name]
        hasher.update(f"{name}:{dep.url}:{dep.commit}\n".encode("utf-8"))
    return hasher.hexdigest()


def compute_patch_digest(root: Path | None = None) -> str:
    if root is None:
        root = Path(__file__).resolve().parents[1]
    hasher = hashlib.sha256()
    for name in sorted(DEPENDENCIES.keys()):
        dep = DEPENDENCIES[name]
        for p in dep.patches:
            patch_file = root / "patches" / p
            if patch_file.is_file():
                hasher.update(f"{p}:{hashlib.sha256(patch_file.read_bytes()).hexdigest()}\n".encode("utf-8"))
    return hasher.hexdigest()
