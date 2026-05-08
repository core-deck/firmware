#!/usr/bin/env bash
# Build CoreDeck firmware against the vendored vial-qmk submodule.
#
# Usage:
#   ./build.sh              # build default keymap
#   ./build.sh vial         # build vial keymap
#   ./build.sh -- flash     # pass-through 'make' target (e.g. flash, clean)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
QMK_DIR="$REPO_ROOT/vendor/vial-qmk"
KB_NAME="core_deck"
KB_REV="rev1"
KEYMAP="${1:-default}"

if [[ ! -d "$QMK_DIR/.git" && ! -f "$QMK_DIR/.git" ]]; then
    echo "vial-qmk submodule not initialized. Run:"
    echo "  git submodule update --init --recursive"
    exit 1
fi

# Apply patches to vial-qmk (idempotent — skip if already applied)
for patch in "$REPO_ROOT/patches"/*.patch; do
    [[ -e "$patch" ]] || continue
    if git -C "$QMK_DIR" apply --check --reverse "$patch" >/dev/null 2>&1; then
        : # already applied
    else
        echo "Applying $(basename "$patch")"
        git -C "$QMK_DIR" apply "$patch"
    fi
done

# Symlink keyboards/core_deck into the QMK tree.
# Use a relative symlink so the source file is single-source-of-truth in this repo.
LINK_TARGET="$QMK_DIR/keyboards/$KB_NAME"
if [[ ! -L "$LINK_TARGET" ]]; then
    rm -rf "$LINK_TARGET"
    ln -s "../../../keyboards/$KB_NAME" "$LINK_TARGET"
    # Tell vial-qmk's git to ignore this local symlink. Submodules use a
    # gitlink file (.git is a text pointer) — resolve it to find info/exclude.
    QMK_GITDIR="$(git -C "$QMK_DIR" rev-parse --git-dir)"
    case "$QMK_GITDIR" in /*) ;; *) QMK_GITDIR="$QMK_DIR/$QMK_GITDIR" ;; esac
    EXCLUDE_FILE="$QMK_GITDIR/info/exclude"
    mkdir -p "$(dirname "$EXCLUDE_FILE")"
    grep -qxF "/keyboards/$KB_NAME" "$EXCLUDE_FILE" 2>/dev/null \
        || echo "/keyboards/$KB_NAME" >> "$EXCLUDE_FILE"
fi

# Build
cd "$QMK_DIR"
qmk compile -kb "$KB_NAME/$KB_REV" -km "$KEYMAP" "${@:2}"

# Copy resulting UF2 back to repo root
UF2="${KB_NAME}_${KB_REV}_${KEYMAP}.uf2"
if [[ -f "$QMK_DIR/$UF2" ]]; then
    cp "$QMK_DIR/$UF2" "$REPO_ROOT/$UF2"
    echo "Output: $REPO_ROOT/$UF2"
fi
