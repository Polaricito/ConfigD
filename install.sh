#!/usr/bin/env bash
# HyprChange — one-command installer.
#
#   curl -fsSL https://raw.githubusercontent.com/Polaricito/ConfigD/master/install.sh | bash
#
# - Already installed via pacman? That binary is kept and the start-menu entry
#   is (re)created for it.
# - Otherwise the AppImage from the latest build is downloaded to ~/.local/bin
#   and the start-menu entry + icon are installed.
# - If FUSE is missing, the AppImage is extracted once so it still runs.
#
# Set FORCE_UPDATE=1 to force re-downloading the AppImage.
set -euo pipefail

OWNER="Polaricito"
REPO="ConfigD"
BRANCH="master"
APP="HyprChange"

ICON_URL="https://raw.githubusercontent.com/$OWNER/$REPO/$BRANCH/resources/logo.png"
APPIMAGE_URL="https://github.com/$OWNER/$REPO/releases/download/continuous/HyprChange-continuous-x86_64.AppImage"

# 1. Prefer an already-packaged install (e.g. via pacman).
PACKAGED="$(command -v hyprchange 2>/dev/null || true)"
if [[ -z "$PACKAGED" ]] && [[ -x /usr/bin/hyprchange ]]; then
    PACKAGED=/usr/bin/hyprchange
fi

IS_APPIMAGE=0
if [[ -n "$PACKAGED" ]]; then
    if pacman -Q hyprchange >/dev/null 2>&1; then
        echo "Found HyprChange already installed via pacman ($PACKAGED)."
    else
        echo "Found HyprChange already on PATH ($PACKAGED)."
    fi
    BIN="$PACKAGED"
else
    IS_APPIMAGE=1
    mkdir -p "$HOME/.local/bin"
    BIN="$HOME/.local/bin/hyprchange"
    if [[ "${FORCE_UPDATE:-0}" == "1" ]] || [[ ! -x "$BIN" ]]; then
        echo "Downloading the HyprChange AppImage…"
        curl -fLsS "$APPIMAGE_URL" -o "$BIN"
        chmod +x "$BIN"
        echo "Saved to $BIN"
    else
        echo "HyprChange already installed at $BIN (FORCE_UPDATE=1 to re-download)."
    fi
fi

# 2. FUSE-less run when possible.
if [[ "$IS_APPIMAGE" == "1" ]] && ! command -v fusermount3 >/dev/null 2>&1 \
    && ! command -v fusermount >/dev/null 2>&1; then
    echo "FUSE not found — extracting the AppImage so it runs without it."
    ROOT="$HOME/.local/share/hyprchange"
    rm -rf "$ROOT"
    mkdir -p "$ROOT"
    (cd "$ROOT" && "$BIN" --appimage-extract)
    BIN="$ROOT/squashfs-root/AppRun"
    rm -f "$HOME/.local/bin/hyprchange"
fi

# 3. Start-menu entry + icon.
mkdir -p "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/128x128/apps"
ICON="$HOME/.local/share/icons/hicolor/128x128/apps/hyprchange.png"
if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$ICON_URL" -o "$ICON" || echo "Icon download skipped — using the bundled one on next app run."
fi

DESKTOP="$HOME/.local/share/applications/hyprchange.desktop"
cat > "$DESKTOP" <<EOF
[Desktop Entry]
Type=Application
Name=HyprChange
Comment=Hyprland settings editor
Exec="$BIN"
Icon=hyprchange
Terminal=false
Categories=Settings;Utility;
StartupWMClass=hyprchange
EOF

update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true

# The element that pays off: remove artifacts of the pre-rename release.
rm -f "$HOME/.local/share/applications/hyprset.desktop"
rm -f "$HOME/.local/share/icons/hicolor/128x128/apps/hyprset.png"

echo
echo "HyprChange is installed. Find it in your start menu,"
echo "or run it right away:"
echo "  $BIN"