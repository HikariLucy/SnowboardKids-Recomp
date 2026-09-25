#!/usr/bin/env python3
"""Explicit release gate: verify licensing, provenance, and clean checkout inputs."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]

blockers = []

# 1. Project license
if not (root / "LICENSE").is_file() and not (root / "COPYING").is_file():
    blockers.append(
        ("project_license_missing", "repository owner has not yet selected a project license")
    )

# 2. Dependency licenses and obligations
runtime_license = root / ".deps-runtime/N64ModernRuntime/COPYING"
if runtime_license.is_file() and "GNU GENERAL PUBLIC LICENSE" in runtime_license.read_text(errors="ignore"):
    blockers.append(
        ("dependency_gpl_compliance", "N64ModernRuntime is GNU GPLv3; binary distribution requires GPLv3 source disclosure")
    )

frontend_license = root / ".deps-renderer/RecompFrontend/LICENSE"
if not frontend_license.is_file():
    blockers.append(
        ("upstream_recompfrontend_license", "RecompFrontend lacks an explicit top-level license grant")
    )

theme_assets = root / ".deps-renderer/recomp-theme/assets"
if theme_assets.is_dir():
    for font_name in ("LatoLatin-Regular.ttf", "Fredoka.ttf", "NotoEmoji-Regular.ttf"):
        if (theme_assets / font_name).is_file():
            license_file = theme_assets / (font_name.split(".")[0] + "-LICENSE.txt")
            if not license_file.is_file():
                blockers.append(
                    ("theme_font_notices", f"font '{font_name}' lacks adjacent license file required by OFL Section 2")
                )
                break

theme_repo = root / ".deps-renderer/recomp-theme"
if not (theme_repo / "LICENSE").is_file() and not (theme_repo / "LICENSE.txt").is_file():
    blockers.append(
        ("theme_asset_provenance", "theme SVG/PNG artwork redistribution rights are undocumented by author")
    )

# 3. Game-derived recompiled material
blockers.append(
    ("game_derived_redistribution_unresolved", "recompiled CPU corpus and RSP microcode derived from proprietary ROM have unresolved redistribution status")
)

# 4. Clean checkout inputs (clean checkouts do not bundle user ROM outputs)
if not (root / "build-tools/production-continuation/corpus").is_dir():
    blockers.append(
        ("clean_checkout_missing_generated_corpus", "generated CPU corpus is absent from the clean checkout")
    )
if not (root / "rsp/aspMain.cpp").is_file():
    blockers.append(
        ("clean_checkout_missing_generated_rsp", "generated RSP source is absent from the clean checkout")
    )

if blockers:
    for name, detail in blockers:
        print(f"BLOCKER {name}: {detail}", file=sys.stderr)
    print("\nHosted CI must not fetch or store a user ROM. No public binary artifact may be uploaded.", file=sys.stderr)
    sys.exit(1)

print("Release inputs present and licensing cleared.")
sys.exit(0)
