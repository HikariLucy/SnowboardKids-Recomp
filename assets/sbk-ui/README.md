# Original Snowboard Kids frontend assets

Created for this project on 2026-09-25 as hand-written SVG basic geometry.
The eleven 32×32 navigation icons retain filenames required by the frontend;
the drawings are new and do not copy the upstream theme icons. `slope.svg` is
an original static mountain/snow composition. No ROM data, game artwork,
commercial logo, screenshot, external SVG references or extracted assets are
used. `recomp.rcss` provides project typography roles.

These project-owned sources establish provenance only. The repository owner
has not selected a distribution license; the project-license release blocker
still applies. Fonts are separately licensed under OFL: existing Lato Latin,
Fredoka, Noto Emoji and PromptFont files come from the pinned theme checkout,
with their license texts included by the staging allowlist.

Only scripts/stage_ui_assets.py may populate a build assets directory. It
removes stale output and copies reviewed sources; packaging rejects extra,
missing, symlinked or byte-modified files. No upstream drawings are shipped.
