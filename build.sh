#!/usr/bin/env bash
# ── Parametric Wayland Compositor — build helper ─────────────────────
# Usage: ./build.sh [build|install|run|clean]
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$DIR/build"
PREFIX="${PREFIX:-/usr/local}"

GRN='\033[0;32m'; YEL='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
ok()  { echo -e "${GRN}[parametric]${NC} $*"; }
warn(){ echo -e "${YEL}[parametric]${NC} $*"; }
die() { echo -e "${RED}[parametric]${NC} $*" >&2; exit 1; }

# ── dependency check ────────────────────────────────────────────────
check() {
    local fail=0
    for cmd in meson ninja pkg-config gcc; do
        command -v "$cmd" &>/dev/null || { warn "Missing tool: $cmd"; fail=1; }
    done
    for pkg in wlroots-0.18 wayland-server wayland-protocols \
               xkbcommon cairo pangocairo pixman-1; do
        pkg-config --exists "$pkg" 2>/dev/null || { warn "Missing pkg: $pkg"; fail=1; }
    done
    [[ $fail -eq 0 ]] || die "Install missing dependencies (see README.md)"
    ok "All dependencies found."
}

# ── build ────────────────────────────────────────────────────────────
build() {
    check
    if [[ ! -d "$BUILD" ]]; then
        ok "Configuring..."
        meson setup "$BUILD" "$DIR" \
            --prefix="$PREFIX" \
            --buildtype=debugoptimized \
            -Db_lto=false
    fi
    ok "Building..."
    ninja -C "$BUILD" -j"$(nproc)"
    ok "Done → $BUILD/parametric"
}

# ── install ──────────────────────────────────────────────────────────
install_() {
    build
    ok "Installing to $PREFIX ..."
    ninja -C "$BUILD" install
    local sess=/usr/share/wayland-sessions
    if [[ -d "$sess" ]]; then
        install -Dm644 "$DIR/parametric.desktop" "$sess/parametric.desktop" \
            && ok "Session entry installed to $sess" || true
    fi
}

# ── run (nested) ─────────────────────────────────────────────────────
run_() {
    build
    ok "Starting Parametric (nested)..."
    exec "$BUILD/parametric" "$@"
}

# ── clean ────────────────────────────────────────────────────────────
clean() { rm -rf "$BUILD"; ok "Cleaned."; }

case "${1:-build}" in
    build)   build ;;
    install) install_ ;;
    run)     shift; run_ "$@" ;;
    clean)   clean ;;
    *)  echo "Usage: $0 [build|install|run [args]|clean]"; exit 1 ;;
esac
