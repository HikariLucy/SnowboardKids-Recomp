#!/usr/bin/env python3
"""Pinned DirectX Shader Compiler release for Windows builds and packages.

RT64 links dxcompiler.lib, so SnowboardKidsEngine.exe needs dxcompiler.dll at
run time: on D3D12 RT64 compiles generated shader text and links it with the
library shaders embedded at build time. The copies in RT64's pinned
src/contrib/dxc (rt64/dxc-bin@cc15e715) are not used on Windows: its
dxcompiler.dll is an unsigned development build matching no Microsoft release,
and that generation of DXC loads a separate, proprietary-licensed validator
(dxil.dll) from the DLL search path to sign shaders. See docs/DXC-PROVENANCE.md.

Windows builds use one official Microsoft release, pinned here by URL and
SHA-256 (archive and every extracted file): its dxc.exe compiles RT64's and
RecompFrontend's shaders at build time (via rt64-dxc-executable.patch) and its
dxcompiler.dll is the only DXC file shipped. Since v1.8.2505 the compiler
always validates and hashes ("signs") DXIL with its internal validator and
never searches for dxil.dll, so no validator is extracted or shipped.

    python scripts/dxc_redist.py            # fetch + verify (bootstrap --only dxc)
    python scripts/dxc_redist.py --verify   # verify only
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import sys
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]

RELEASE_TAG = "v1.8.2505.1"
RELEASE_COMMIT = "b106a961d09221b3c5bdb37be45b679257da08b8"
ARCHIVE_URL = ("https://github.com/microsoft/DirectXShaderCompiler/releases/download/"
               "v1.8.2505.1/dxc_2025_07_14.zip")
ARCHIVE_SHA256 = "9ad895a6b039e3a8f8c22a1009f866800b840a74b50db9218d13319e215ea8a4"
INSTALL_DIR = Path(".deps-renderer") / "dxc-redist" / RELEASE_TAG

# Archive member -> SHA-256. Only these are extracted: never dxil.dll/dxv.exe.
FILES = {
    "bin/x64/dxcompiler.dll": "5888d3e590f5bfd8743484e7f31313996734adec5741506271a5c6368cbe8ba9",
    "bin/x64/dxc.exe": "ce11bb02f6027055e0b9f8c062d13b14f373c876a16557a4fa744314117d31e7",
    "LICENSE-LLVM.txt": "729615317e28dd03907e46f0fc3b5e88f7853cee61d1a1471d2749335516b46f",
    "ReleaseNotes.md": "d3ad10d52b810b9aaebad1fcbb2872150aaba763c5461aac711d1ba8f4539a0b",
}

LICENSE_DIR = Path("licenses") / "DirectXShaderCompiler"
# Vendored text -> (SHA-256, origin). The release notes' license table maps
# LICENSE-LLVM.txt to every file but d3d12shader.h (not used); LICENSE.TXT and
# ThirdPartyNotices.txt are the source tree's texts at the release commit.
LICENSE_TEXTS = {
    "LICENSE-LLVM.txt": ("729615317e28dd03907e46f0fc3b5e88f7853cee61d1a1471d2749335516b46f",
                         f"{ARCHIVE_URL} (LICENSE-LLVM.txt)"),
    "LICENSE.TXT": ("27a49e35d1da96eba18fba54bc882667ff0ff8c0254f16f2b6e165d605ba7df8",
                    f"microsoft/DirectXShaderCompiler@{RELEASE_COMMIT}:LICENSE.TXT"),
    "ThirdPartyNotices.txt": ("19512a5d0a015ef16d167272c164da49a604614a786206212a80ad486ed0be6d",
                              f"microsoft/DirectXShaderCompiler@{RELEASE_COMMIT}:ThirdPartyNotices.txt"),
}

# Packaged DLL -> PE FileVersion (Microsoft-signed) and the licenses/<name>.txt
# notices that must ship with it.
DLLS = {
    "dxcompiler.dll": {
        "version": "1.8.2505.32",
        "member": "bin/x64/dxcompiler.dll",
        "notices": {
            "DirectXShaderCompiler-LICENSE-LLVM": "LICENSE-LLVM.txt",
            "DirectXShaderCompiler-LICENSE": "LICENSE.TXT",
            "DirectXShaderCompiler-ThirdPartyNotices": "ThirdPartyNotices.txt",
        },
    },
}
PROVENANCE = f"Microsoft DirectXShaderCompiler {RELEASE_TAG} release ({ARCHIVE_URL})"
# The validator is never extracted, built against or shipped.
FORBIDDEN_DLLS = ("dxil.dll",)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def install_dir(root: Path = ROOT) -> Path:
    return root / INSTALL_DIR


def dll_dir(root: Path = ROOT) -> Path:
    return install_dir(root) / "bin" / "x64"


def dxc_exe(root: Path = ROOT) -> Path:
    return dll_dir(root) / "dxc.exe"


def notice_files(dll: str, root: Path = ROOT) -> dict:
    """licenses/<notice>.txt name -> vendored source path, for one DLL."""
    return {notice: root / LICENSE_DIR / text for notice, text in DLLS[dll.lower()]["notices"].items()}


def verify_license_texts(root: Path = ROOT) -> list:
    errors = []
    for name, (digest, origin) in LICENSE_TEXTS.items():
        path = root / LICENSE_DIR / name
        if not path.is_file():
            errors.append(f"missing vendored DXC license text {path.relative_to(root)} ({origin})")
        elif sha256(path) != digest:
            errors.append(f"{path.relative_to(root)} differs from the pinned upstream text ({origin})")
    return errors


def verify_dll(path: Path) -> list:
    """A bundled DXC DLL must be byte-identical to the pinned release file."""
    entry = DLLS.get(path.name.lower())
    if entry is None:
        return []
    expected = FILES[entry["member"]]
    if not path.is_file():
        return [f"{path.name}: not found"]
    actual = sha256(path)
    if actual != expected:
        return [f"{path.name} ({actual[:16]}...) is not the pinned {PROVENANCE} binary "
                f"({expected[:16]}...); run: python scripts/bootstrap.py --only dxc and rebuild"]
    return []


def verify_install(root: Path = ROOT) -> list:
    errors = []
    base = install_dir(root)
    for member, digest in FILES.items():
        path = base / member
        if not path.is_file():
            errors.append(f"missing {path.relative_to(root)}; run: python scripts/bootstrap.py --only dxc")
        elif sha256(path) != digest:
            errors.append(f"{path.relative_to(root)} does not match its pinned SHA-256")
    return errors + verify_license_texts(root)


def fetch(root: Path = ROOT, archive: Path = None) -> None:
    base = install_dir(root)
    if not verify_install(root):
        print(f"dxc: {RELEASE_TAG} already present and verified", flush=True)
        return
    with tempfile.TemporaryDirectory(prefix="sbk_dxc_") as tmp:
        if archive is None:
            archive = Path(tmp) / "dxc.zip"
            print(f"dxc: downloading {ARCHIVE_URL}", flush=True)
            with urllib.request.urlopen(ARCHIVE_URL, timeout=300) as response, archive.open("wb") as out:
                shutil.copyfileobj(response, out)
        if sha256(archive) != ARCHIVE_SHA256:
            raise RuntimeError(f"{archive.name}: SHA-256 does not match the pinned {RELEASE_TAG} archive")
        staging = Path(tmp) / "extract"
        with zipfile.ZipFile(archive) as bundle:
            for member, digest in FILES.items():
                data = bundle.read(member)
                if hashlib.sha256(data).hexdigest() != digest:
                    raise RuntimeError(f"{member}: SHA-256 does not match the pin")
                target = staging / member
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
        shutil.rmtree(base, ignore_errors=True)
        base.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(staging, base)
    errors = verify_install(root)
    if errors:
        raise RuntimeError("; ".join(errors))
    print(f"dxc: {RELEASE_TAG} ({ARCHIVE_SHA256[:12]}) verified in {INSTALL_DIR.as_posix()}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--verify", action="store_true", help="only verify the installed files")
    parser.add_argument("--archive", type=Path, help="use an already downloaded release archive")
    args = parser.parse_args()
    try:
        if args.verify:
            errors = verify_install()
            for error in errors:
                print(f"FAIL: {error}", file=sys.stderr)
            return 1 if errors else 0
        fetch(archive=args.archive)
    except (OSError, RuntimeError, KeyError, zipfile.BadZipFile) as error:
        print(f"dxc: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
