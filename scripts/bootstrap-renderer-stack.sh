#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="$ROOT_DIR/.deps-renderer"
RT64_DIR="$DEPS_DIR/rt64"
FRONTEND_DIR="$DEPS_DIR/RecompFrontend"

RT64_REPO="https://github.com/cdlewis/rt64.git"
RT64_COMMIT="6a4166b2cfa952d931a08481d1037da995f28b54"

FRONTEND_REPO="https://github.com/cdlewis/RecompFrontend.git"
FRONTEND_COMMIT="e85b912d9df677b04f9358867dd010c8af27ea05"

THEME_DIR="$DEPS_DIR/recomp-theme"
THEME_REPO="https://github.com/cdlewis/snowboardkids-recomp-theme.git"
THEME_COMMIT="0cb9a83a263607fbc8ab6176a758a00726e237cc"

for cmd in git cmake ninja clang clang++ pkg-config; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Missing dependency: $cmd" >&2
        echo "On Ubuntu/Linux Mint install:" >&2
        echo "  sudo apt install -y git cmake ninja-build clang pkg-config" >&2
        exit 1
    fi
done

missing_pkgs=()
pkg-config --exists sdl2 || missing_pkgs+=("libsdl2-dev")
pkg-config --exists freetype2 || missing_pkgs+=("libfreetype6-dev")
pkg-config --exists gtk+-3.0 || missing_pkgs+=("libgtk-3-dev")

if [[ ${#missing_pkgs[@]} -ne 0 ]]; then
    echo "Missing renderer/frontend development packages:" >&2
    printf '  %s\n' "${missing_pkgs[@]}" >&2
    echo >&2
    echo "Install with:" >&2
    echo "  sudo apt install -y ${missing_pkgs[*]} libx11-dev libxrandr-dev lld llvm" >&2
    exit 1
fi

mkdir -p "$DEPS_DIR"

clone_and_pin() {
    local repo_url="$1"
    local commit="$2"
    local dir="$3"

    if [[ ! -d "$dir/.git" ]]; then
        git clone --recurse-submodules "$repo_url" "$dir"
    fi

    git -C "$dir" fetch origin "$commit" || git -C "$dir" fetch origin
    git -C "$dir" checkout --detach "$commit"
    git -C "$dir" submodule update --init --recursive
}

clone_and_pin "$RT64_REPO" "$RT64_COMMIT" "$RT64_DIR"
clone_and_pin "$FRONTEND_REPO" "$FRONTEND_COMMIT" "$FRONTEND_DIR"
clone_and_pin "$THEME_REPO" "$THEME_COMMIT" "$THEME_DIR"

echo
echo "Renderer stack ready."
echo "RT64:           $RT64_COMMIT"
echo "RecompFrontend: $FRONTEND_COMMIT"
echo "Recomp theme:   $THEME_COMMIT"
echo "RT64 path:      $RT64_DIR"
echo "Frontend path:  $FRONTEND_DIR"
echo "Theme path:     $THEME_DIR"
