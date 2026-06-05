#!/usr/bin/env bash

set -e

# Default to empty, will auto-detect
PREFIX=""

# Function to print usage
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Uninstall Viewstack (filemanager)."
    echo ""
    echo "Options:"
    echo "  --system         Uninstall from system-wide paths (/usr/local)"
    echo "  --local          Uninstall from user-local paths (~/.local) [default if no arg]"
    echo "  --prefix=PREFIX  Uninstall from a specific PREFIX"
    echo "  --help           Show this help message"
}

for arg in "$@"; do
    case "$arg" in
        --system)
            PREFIX="/usr/local"
            ;;
        --local)
            PREFIX="$HOME/.local"
            ;;
        --prefix=*)
            PREFIX="${arg#*=}"
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            usage
            exit 1
            ;;
    esac
done

# If no prefix specified, check if it exists in local or system
if [ -z "$PREFIX" ]; then
    if [ -f "$HOME/.local/bin/filemanager" ]; then
        PREFIX="$HOME/.local"
        echo "Found installation in user-local path: $PREFIX"
    elif [ -f "/usr/local/bin/filemanager" ]; then
        PREFIX="/usr/local"
        echo "Found installation in system-wide path: $PREFIX"
    else
        PREFIX="$HOME/.local"
        echo "Could not auto-detect installation. Defaulting to $PREFIX."
    fi
fi

BINDIR="${PREFIX}/bin"
DATADIR="${PREFIX}/share"
APPDIR="${DATADIR}/applications"
ICONDIR="${DATADIR}/icons/hicolor/scalable/apps"

echo "Uninstalling Viewstack from PREFIX: $PREFIX"

# Require sudo if removing from system paths and we don't have write access
if [ ! -w "$BINDIR" ] && [ "$(id -u)" -ne 0 ] && [ -d "$BINDIR" ]; then
    echo "Root privileges are required to uninstall from $PREFIX."
    echo "Please run: sudo $0 --prefix=$PREFIX"
    exit 1
fi

rm -vf "${BINDIR}/filemanager"
rm -vf "${APPDIR}/filemanager.desktop"
rm -vf "${ICONDIR}/filemanager.svg"

# Update desktop database if available
if command -v update-desktop-database >/dev/null 2>&1; then
    echo "Updating desktop database..."
    update-desktop-database "$APPDIR" || true
fi

# Update icon cache if available
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    echo "Updating icon cache..."
    gtk-update-icon-cache -q -t -f "${DATADIR}/icons/hicolor" || true
fi

# Reset default file manager if it was set to viewstack/filemanager
MIMEAPPS="$HOME/.config/mimeapps.list"
if [ -f "$MIMEAPPS" ]; then
    if grep -q "inode/directory=filemanager.desktop" "$MIMEAPPS"; then
        echo "Removing filemanager as the default handler for folders in mimeapps.list..."
        sed -i.bak '/inode\/directory=filemanager.desktop/d' "$MIMEAPPS"
        echo "Default folder handler removed. You may want to set your preferred file manager again."
    fi
fi

echo "Viewstack uninstallation complete."
