#!/usr/bin/env python3
"""Explicit release gate: verify licensing, provenance, and individual blockers."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from dependency_lock import DEPENDENCIES
import dxc_redist
from stage_ui_assets import validate_assets, validate_archive


def check_provenance(root=ROOT):
    # Check that dependency pins in dependency_lock match checkout HEADs if present
    for name, dep in DEPENDENCIES.items():
        dep_path = root / dep.path
        if dep_path.is_dir() and (dep_path / '.git').exists():
            try:
                head = subprocess.check_output(
                    ['git', '-C', str(dep_path), 'rev-parse', 'HEAD'],
                    text=True, stderr=subprocess.DEVNULL
                ).strip()
                if head != dep.commit:
                    return False, f"dependency '{name}' checkout ({head}) does not match lock pin ({dep.commit})"
            except Exception as e:
                return False, f"failed checking git HEAD for {name}: {e}"
    return True, None


def run_checks(root=ROOT, assets=None, archive=None, public_beta=False):
    passes = []
    blockers = []

    # 1. Dependency provenance
    prov_ok, prov_msg = check_provenance(root)
    if prov_ok:
        passes.append(("dependency_provenance", "all dependencies match canonical lock pins"))
    else:
        blockers.append(("dependency_provenance", prov_msg))

    # 2. Font licenses
    promptfont_lic = root / ".deps-renderer/recomp-theme/assets/promptfont/LICENSE.txt"
    if promptfont_lic.is_file():
        passes.append(("promptfont_license", "OFL 1.1 license text present alongside font"))
    else:
        blockers.append(("promptfont_license", "promptfont lacks LICENSE.txt in asset directory"))

    # Bundled fonts (LatoLatin, Fredoka, NotoEmoji)
    font_lics = [
        ("LatoLatin", root / "licenses/LatoLatin-OFL.txt"),
        ("Fredoka", root / "licenses/Fredoka-OFL.txt"),
        ("NotoEmoji", root / "licenses/NotoEmoji-OFL.txt"),
    ]
    all_fonts_ok = all(path.is_file() for _, path in font_lics)
    if all_fonts_ok:
        passes.append(("bundled_font_licenses", "OFL 1.1 license texts bundled for LatoLatin, Fredoka, and NotoEmoji"))
    else:
        missing = [name for name, path in font_lics if not path.is_file()]
        blockers.append(("bundled_font_licenses", f"missing license texts for: {', '.join(missing)}"))

    # 3. Upstream RecompFrontend license. The public-beta policy does not
    # pretend a license exists: it permits packaging only when the unresolved
    # state and upstream clarification issue are explicitly disclosed.
    frontend_lic = root / ".deps-renderer/RecompFrontend/LICENSE"
    beta_policy = root / "docs/BETA-DISTRIBUTION-POLICY.md"
    if frontend_lic.is_file():
        passes.append(("recompfrontend_license", "RecompFrontend top-level license cleared"))
    elif public_beta and beta_policy.is_file() and "RecompFrontend/issues/44" in beta_policy.read_text(errors="ignore"):
        passes.append(("recompfrontend_pending_disclosed",
                       "RecompFrontend top-level license is still pending; issue #44 is disclosed by beta policy"))
    else:
        blockers.append(("recompfrontend_license", "RecompFrontend lacks an explicit top-level license grant"))

    # 4. Project license
    if (root / "LICENSE").is_file() or (root / "COPYING").is_file():
        passes.append(("project_license", "project root GPL-3.0 license present"))
    else:
        blockers.append(("project_license", "repository owner has not yet selected a project license"))

    # 5. Dependency GPL compliance evidence. This is a packaging gate for the
    # repository/source directions, not a legal opinion.
    runtime_license = root / ".deps-runtime/N64ModernRuntime/COPYING"
    source_directions = root / "SOURCE-COMPLIANCE.md"
    if (runtime_license.is_file() and
        "GNU GENERAL PUBLIC LICENSE" in runtime_license.read_text(errors="ignore") and
        (root / "LICENSE").is_file() and source_directions.is_file()):
        passes.append(("dependency_gpl_compliance",
                       "GPL license plus corresponding-source/build directions are present"))
    else:
        blockers.append(("dependency_gpl_compliance",
                         "GPL runtime release requires project LICENSE and SOURCE-COMPLIANCE.md"))

    # 6. Owned asset provenance needs evidence from staging AND final packaging.
    # This clears only old-theme icon provenance, never the project license.
    if assets is None or archive is None:
        blockers.append(("theme_asset_icons", "provide --assets and --archive for owned asset byte verification"))
    else:
        try:
            validate_assets(Path(assets), root)
            validate_archive(Path(archive), root)
        except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
            blockers.append(("theme_asset_icons", str(error)))
        else:
            passes.append(("theme_asset_icons", "staged and packaged assets match reviewed original SVGs and licensed fonts"))

    # 7. Game-module distribution model. A public beta may carry the reviewed
    # precompiled module only at its canonical path and only with the explicit
    # disclosure policy present. The ROM itself remains forbidden by the
    # artifact scanner.
    if public_beta and archive is not None and beta_policy.is_file():
        try:
            with zipfile.ZipFile(archive) as bundle:
                module_names = {
                    "SnowboardKidsRecompiled/modules/snowboardkids-us/SnowboardKidsGame.so",
                    "SnowboardKidsRecompiled/modules/snowboardkids-us/SnowboardKidsGame.dll",
                    "SnowboardKidsRecompiled/modules/snowboardkids-us/SnowboardKidsGame.dylib",
                }
                present = module_names.intersection(bundle.namelist())
        except (OSError, zipfile.BadZipFile, RuntimeError) as error:
            blockers.append(("game_distribution_model", str(error)))
        else:
            if present:
                passes.append(("game_module_disclosure",
                               "precompiled game module present at canonical path with beta disclosure policy"))
            else:
                blockers.append(("game_distribution_model",
                                 "public beta archive is missing the reviewed precompiled game module"))
    else:
        blockers.append(("game_distribution_model",
                         "public release requires --public-beta plus an audited archive containing the reviewed game module"))

    # 8. Windows runtime redistribution: bundled DXC bytes are the pinned
    # official release, their license texts are the pinned upstream texts, and
    # dxil.dll ships only after the maintainer recorded the Microsoft
    # distributable-code decision. Linux archives carry no DXC.
    if archive is not None:
        name, detail = check_windows_runtime(Path(archive), root)
        if name is not None:
            (passes if detail.startswith("ok:") else blockers).append((name, detail))

    return passes, blockers


def check_windows_runtime(archive, root=ROOT):
    try:
        with zipfile.ZipFile(archive) as bundle:
            names = set(bundle.namelist())
            if "SnowboardKidsRecompiled/SnowboardKidsEngine.exe" not in names:
                return None, ""
            problems = list(dxc_redist.verify_license_texts(root))
            for dll, entry in dxc_redist.DLLS.items():
                member = next((n for n in names if n.lower() == f"snowboardkidsrecompiled/{dll}"), None)
                if member is None:
                    continue
                digest = hashlib.sha256(bundle.read(member)).hexdigest()
                if digest != dxc_redist.FILES[entry["member"]]:
                    problems.append(f"{dll} is not the pinned {dxc_redist.PROVENANCE} binary")
                if dll == "dxil.dll" and not dxc_redist.dxil_redistribution_accepted(root):
                    problems.append(f"dxil.dll redistribution decision is not ACCEPTED in "
                                    f"{dxc_redist.DXIL_DECISION.as_posix()}")
    except (OSError, zipfile.BadZipFile, RuntimeError) as error:
        return "windows_runtime_redistribution", str(error)
    if problems:
        return "windows_runtime_redistribution", "; ".join(problems)
    return "windows_runtime_redistribution", "ok: bundled DXC matches the pinned release and its notices"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT, help='Project root directory')
    parser.add_argument('--assets', type=Path, help='Staged build assets directory')
    parser.add_argument('--archive', type=Path, help='Local draft archive for byte verification')
    parser.add_argument('--quiet-pass', action='store_true', help='Only print blockers')
    parser.add_argument('--public-beta', action='store_true',
                        help='Apply the disclosed v0.9.0-beta distribution policy')
    args = parser.parse_args()

    passes, blockers = run_checks(args.root, assets=args.assets, archive=args.archive,
                                  public_beta=args.public_beta)

    if not args.quiet_pass:
        for name, _ in passes:
            print(f"PASS {name}")

    if blockers:
        for name, detail in blockers:
            print(f"BLOCKER {name}: {detail}", file=sys.stderr)
        print("\nHosted CI must not fetch or store a user ROM. Public upload remains blocked until the selected release policy gates pass.", file=sys.stderr)
        return 1

    if args.public_beta:
        print("Public beta package gates passed. RecompFrontend license clarification remains pending and disclosed.")
    else:
        print("Release inputs present and licensing cleared.")
    return 0


if __name__ == '__main__':
    sys.exit(main())
