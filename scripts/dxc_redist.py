#!/usr/bin/env python3
"""Pinned DirectX Shader Compiler redistributable for Windows packages.

RT64 links dxcompiler.lib, so SnowboardKidsEngine.exe needs dxcompiler.dll
(which loads dxil.dll to sign shaders) at run time. The copies in RT64's pinned
src/contrib/dxc (rt64/dxc-bin@cc15e715) are not shipped: its dxil.dll is the
official v1.7.2212 binary, but its dxcompiler.dll is an unsigned build of DXC
commit 0dc8d9060 that matches no Microsoft release. See docs/DXC-PROVENANCE.md.

Windows builds and packages instead use both DLLs from one official Microsoft
release archive, pinned here by URL and SHA-256 (archive and every extracted
file). The license texts that apply to them are vendored byte-exact in
licenses/DirectXShaderCompiler/ and pinned here too.

    python scripts/dxc_redist.py            # fetch + verify (bootstrap --only dxc)
    python scripts/dxc_redist.py --verify   # verify only
"""
import argparse
import hashlib
import re
from pathlib import Path
import shutil
import sys
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]

RELEASE_TAG = "v1.7.2308"
RELEASE_COMMIT = "69e54e29086b7035acffb304cec57a350225f8b0"
ARCHIVE_URL = ("https://github.com/microsoft/DirectXShaderCompiler/releases/download/"
               "v1.7.2308/dxc_2023_08_14.zip")
ARCHIVE_SHA256 = "01d4c4dfa37dee21afe70cac510d63001b6b611a128e3760f168765eead1e625"
INSTALL_DIR = Path(".deps-renderer") / "dxc-redist" / RELEASE_TAG

# Archive member -> SHA-256. Only these are extracted.
FILES = {
    "bin/x64/dxcompiler.dll": "570a1a7357893615417edf5ab356625b5b6b721a131bc7331bf17289d4928ed7",
    "bin/x64/dxil.dll": "9cccc7ef419da73fa314fdaecae831c6c20206ae70732c9093f95193378ced10",
    "bin/x64/dxc.exe": "1c9e7cb6c9fb8593e9253ff7fcae998d2e23a9730722d44229a56497a0d366e7",
    "LICENSE-LLVM.txt": "729615317e28dd03907e46f0fc3b5e88f7853cee61d1a1471d2749335516b46f",
    "LICENSE-MS.txt": "734f72f239fe7b07b4c7203f294c1a7ce27095687278bab7e56d630d7c672963",
    "README.md": "c2941b8018b9ab913b60e8d326937505dec3e713e47a26ca747cae2c1df6b7d6",
}

LICENSE_DIR = Path("licenses") / "DirectXShaderCompiler"
# Vendored text -> (SHA-256, origin). The release README maps LICENSE-LLVM.txt
# to dxcompiler.dll and LICENSE-MS.txt to dxil.dll; LICENSE.TXT and
# ThirdPartyNotices.txt are the source tree's texts at the release commit.
LICENSE_TEXTS = {
    "LICENSE-LLVM.txt": ("729615317e28dd03907e46f0fc3b5e88f7853cee61d1a1471d2749335516b46f",
                         f"{ARCHIVE_URL} (LICENSE-LLVM.txt)"),
    "LICENSE-MS.txt": ("734f72f239fe7b07b4c7203f294c1a7ce27095687278bab7e56d630d7c672963",
                       f"{ARCHIVE_URL} (LICENSE-MS.txt)"),
    "LICENSE.TXT": ("9c9393bb14872aca75124c65558174da9cf2aa13f69be4aaffbfb96b29de1910",
                    f"microsoft/DirectXShaderCompiler@{RELEASE_COMMIT}:LICENSE.TXT"),
    "ThirdPartyNotices.txt": ("19512a5d0a015ef16d167272c164da49a604614a786206212a80ad486ed0be6d",
                              f"microsoft/DirectXShaderCompiler@{RELEASE_COMMIT}:ThirdPartyNotices.txt"),
}

# Packaged DLL -> PE FileVersion (Microsoft-signed) and the licenses/<name>.txt
# notices that must ship with it.
DLLS = {
    "dxcompiler.dll": {
        "version": "1.7.2308.7",
        "member": "bin/x64/dxcompiler.dll",
        "notices": {
            "DirectXShaderCompiler-LICENSE-LLVM": "LICENSE-LLVM.txt",
            "DirectXShaderCompiler-LICENSE": "LICENSE.TXT",
            "DirectXShaderCompiler-ThirdPartyNotices": "ThirdPartyNotices.txt",
        },
    },
    "dxil.dll": {
        "version": "101.7.2308.12",
        "member": "bin/x64/dxil.dll",
        "notices": {"DirectXShaderCompiler-dxil-LICENSE-MS": "LICENSE-MS.txt"},
    },
}
PROVENANCE = f"Microsoft DirectXShaderCompiler {RELEASE_TAG} release ({ARCHIVE_URL})"

# dxil.dll is Microsoft proprietary "Distributable Code" (LICENSE-MS.txt). Its
# distribution requirements are a maintainer decision recorded in this file;
# public packages need "Decision: ACCEPTED".
DXIL_DECISION = Path("docs") / "DXIL-REDISTRIBUTION.md"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def install_dir(root: Path = ROOT) -> Path:
    return root / INSTALL_DIR


def dll_dir(root: Path = ROOT) -> Path:
    return install_dir(root) / "bin" / "x64"


def notice_files(dll: str, root: Path = ROOT) -> dict:
    """licenses/<notice>.txt name -> vendored source path, for one DLL."""
    return {notice: root / LICENSE_DIR / text for notice, text in DLLS[dll.lower()]["notices"].items()}


def dxil_redistribution_accepted(root: Path = ROOT) -> bool:
    path = root / DXIL_DECISION
    if not path.is_file():
        return False
    # Only the first "Decision:" line counts, and it must name who and when;
    # examples further down the file never do.
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("Decision:"):
            return re.fullmatch(r"Decision: ACCEPTED \((?!<)[^,()<>]+, \d{4}-\d{2}-\d{2}\)", line.strip()) is not None
    return False


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
