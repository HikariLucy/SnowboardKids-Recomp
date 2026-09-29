#!/usr/bin/env python3
"""Stage and package the Windows x86_64 Snowboard Kids Recompiled archive.

Windows counterpart of scripts/package-beta-linux.sh; both end in the shared
package_release.py / audit / readiness gates. Steps:

  1. refuse a dirty tree (release mode);
  2. run SnowboardKidsEngine.exe --version and match HEAD + project version;
  3. run --validate-module on the reviewed SnowboardKidsGame.dll;
  4. derive runtime DLLs from the real PE imports (scripts/windows_runtime.py);
  5. stage reviewed assets, build the deterministic zip, audit it, and run the
     public-beta readiness gate.

--engine-only-draft packages the ROM-free engine without a game module for CI
review. Draft archives are never release candidates.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from windows_runtime import msvc_redist_dirs, resolve_runtime  # noqa: E402


def project_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(SnowboardKidsRecompiled VERSION (\d+\.\d+\.\d+)", text)
    if not match:
        raise SystemExit("ERROR: cannot read project version from CMakeLists.txt")
    return match.group(1)


def default_module() -> Path:
    appdata = os.environ.get("APPDATA", str(Path.home() / "AppData" / "Roaming"))
    return Path(appdata) / "SnowboardKids" / "modules" / "snowboardkids-us" / "SnowboardKidsGame.dll"


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def fail(message: str) -> int:
    print(f"ERROR: {message}", file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--engine", type=Path, default=ROOT / "build-engine" / "SnowboardKidsEngine.exe")
    parser.add_argument("--game-module", type=Path, default=None,
                        help="Reviewed SnowboardKidsGame.dll (default: %%APPDATA%%\\SnowboardKids\\modules\\snowboardkids-us)")
    parser.add_argument("--version", default=None,
                        help="Archive version label (default: <project version>-dev; set explicitly for a release)")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--engine-only-draft", action="store_true",
                        help="Package the ROM-free engine without a game module (CI review draft)")
    parser.add_argument("--runtime-dir", type=Path, action="append", default=[],
                        help="Extra directory searched for runtime DLLs")
    parser.add_argument("--no-run", action="store_true",
                        help="Draft only: skip executing the engine (cross-host staging tests)")
    args = parser.parse_args()

    draft = args.engine_only_draft
    if args.no_run and not draft:
        return fail("--no-run is only allowed with --engine-only-draft")
    version = args.version or f"{project_version()}-dev"
    engine = args.engine.resolve()
    if engine.name != "SnowboardKidsEngine.exe" or not engine.is_file():
        return fail(f"release engine not found: {engine}")

    module = None
    if not draft:
        module = (args.game_module or default_module()).resolve()
        if module.name != "SnowboardKidsGame.dll" or not module.is_file():
            return fail(f"reviewed game module not found: {module}")
        if git("status", "--porcelain"):
            return fail("working tree is not clean; refuse to package an uncommitted release")

    if not args.no_run:
        banner = subprocess.run([str(engine), "--version"], capture_output=True, text=True)
        print(banner.stdout, end="")
        head = git("rev-parse", "--short=12", "HEAD")
        if banner.returncode != 0:
            return fail("engine --version failed")
        if f"Snowboard Kids Recompiled {project_version()}" not in banner.stdout:
            return fail(f"engine does not report project version {project_version()}")
        if f"commit {head}" not in banner.stdout:
            return fail(f"engine was not built from current HEAD {head}")
        if module is not None:
            print("=== VALIDATING GAME MODULE ===")
            check = subprocess.run([str(engine), "--validate-module", str(module)], capture_output=True, text=True)
            print(check.stdout, end="")
            if check.returncode != 0:
                return fail("game module validation failed:\n" + check.stderr)

    images = [engine] + ([module] if module else [])
    runtime = resolve_runtime(images, [engine.parent, *args.runtime_dir, *msvc_redist_dirs()])
    print("=== WINDOWS RUNTIME DEPENDENCIES ===")
    for name in runtime.system:
        print(f"  system   {name}")
    for name, path in runtime.bundled.items():
        print(f"  bundled  {name}  ({path})")
    if runtime.errors:
        return fail("runtime dependency policy:\n  " + "\n  ".join(runtime.errors))
    pending = runtime.pending_licenses()
    if pending:
        message = ("bundled DLLs without reviewed license text in the pinned tree: " + ", ".join(pending))
        if not draft:
            return fail(message + "\nAdd and review their license texts before a public Windows package.")
        print(f"WARNING (draft only): {message}")

    out_dir = args.out_dir.resolve()
    staging = out_dir / "staging-windows"
    assets = staging / "assets"
    suffix = "-engine-draft" if draft else ""
    archive = out_dir / f"SnowboardKidsRecompiled-{version}-Windows-x86_64{suffix}.zip"
    shutil.rmtree(staging, ignore_errors=True)
    out_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(ROOT / "scripts" / "stage_ui_assets.py"), "--out", str(assets)], check=True)
    if archive.exists():
        archive.unlink()

    cmd = [sys.executable, str(ROOT / "scripts" / "package_release.py"),
           "--binary", str(engine), "--assets", str(assets), "--version", version,
           "--commit", git("rev-parse", "HEAD"), "--platform", "windows",
           "--architecture", "x86_64", "--out", str(archive)]
    for path in runtime.bundled.values():
        cmd += ["--library", str(path)]
    for notice, path in sorted(runtime.license_files(ROOT).items()):
        cmd += ["--license", f"{notice}={path}"]
    cmd += ["--draft"] if draft else ["--game-module", str(module), "--public-beta"]
    subprocess.run(cmd, check=True)

    if not draft:
        subprocess.run([sys.executable, str(ROOT / "scripts" / "check_release_readiness.py"),
                        "--assets", str(assets), "--archive", str(archive), "--public-beta"], check=True)
    print(f"\n{'Draft' if draft else 'Release candidate'} archive: {archive}")
    if draft:
        print("Draft archives contain no game module and are not release candidates.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
