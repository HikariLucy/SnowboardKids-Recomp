#!/usr/bin/env python3
"""Explicit release gate: verify licensing, provenance, and individual blockers."""
import argparse
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from dependency_lock import DEPENDENCIES
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


def run_checks(root=ROOT, assets=None, archive=None):
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

    # 3. Upstream RecompFrontend license
    frontend_lic = root / ".deps-renderer/RecompFrontend/LICENSE"
    if frontend_lic.is_file():
        passes.append(("recompfrontend_license", "RecompFrontend top-level license cleared"))
    else:
        blockers.append(("recompfrontend_license", "RecompFrontend lacks an explicit top-level license grant"))

    # 4. Project license
    if (root / "LICENSE").is_file() or (root / "COPYING").is_file():
        passes.append(("project_license", "project root license cleared"))
    else:
        blockers.append(("project_license", "repository owner has not yet selected a project license"))

    # 5. Dependency GPL compliance
    runtime_license = root / ".deps-runtime/N64ModernRuntime/COPYING"
    if runtime_license.is_file() and "GNU GENERAL PUBLIC LICENSE" in runtime_license.read_text(errors="ignore"):
        blockers.append(("dependency_gpl_compliance", "N64ModernRuntime is GNU GPLv3; binary distribution requires GPLv3 source disclosure"))
    else:
        passes.append(("dependency_gpl_compliance", "GPL source disclosure obligations satisfied"))

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

    # 7. Game-derived recompiled material / distribution model
    blockers.append(("game_distribution_model", "MODEL_D_TECHNICALLY_IMPLEMENTED: local user-ROM module builder implemented; release clearance pending distribution policy and legal clearance"))

    return passes, blockers


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT, help='Project root directory')
    parser.add_argument('--assets', type=Path, help='Staged build assets directory')
    parser.add_argument('--archive', type=Path, help='Local draft archive for byte verification')
    parser.add_argument('--quiet-pass', action='store_true', help='Only print blockers')
    args = parser.parse_args()

    passes, blockers = run_checks(args.root, assets=args.assets, archive=args.archive)

    if not args.quiet_pass:
        for name, _ in passes:
            print(f"PASS {name}")

    if blockers:
        for name, detail in blockers:
            print(f"BLOCKER {name}: {detail}", file=sys.stderr)
        print("\nHosted CI must not fetch or store a user ROM. No public binary artifact may be uploaded.", file=sys.stderr)
        return 1

    print("Release inputs present and licensing cleared.")
    return 0


if __name__ == '__main__':
    sys.exit(main())
