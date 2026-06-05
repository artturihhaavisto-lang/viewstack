# Viewstack

A native GTK file manager inspired by Dolphin and imgview, with sharp black surfaces and media previews.

Viewstack is a companion project to [imgview](https://github.com/artturihhaavisto-lang/imgview). It keeps the same lightweight C/GTK approach and crisp visual direction, but focuses on browsing folders, drives, images, videos, and archives.

## Features

- **Grid and compact list views**: Switch between thumbnail browsing and dense file rows
- **Media previews**: Image thumbnails and video thumbnails with hover preview playback
- **Folder tabs**: Open folders in separate tabs and keep independent navigation history
- **Sidebar places**: Home, desktop, documents, downloads, music, pictures, videos, projects, root, and mounted drives
- **Drive tools**: Enable or disable automount on startup from the drive context menu
- **File actions**: Open, copy, cut, paste, rename, trash, delete, create folder, and properties
- **AppImage helpers**: Install an AppImage as an app or create a shell command for it
- **Archive helpers**: Extract common archive formats from the context menu
- **Desktop integration**: Register as the default handler for folders

## Screenshots

Screenshots coming soon.

## Keybindings

| Key | Action |
|-----|--------|
| `Alt` + `Left` | Back |
| `Alt` + `Right` | Forward |
| `Alt` + `Up` | Parent folder |
| `Backspace` | Parent folder |
| `Ctrl` + `L` | Focus path entry |
| `Ctrl` + `T` | New tab |
| `Ctrl` + `W` | Close tab |
| `Ctrl` + `Scroll` | Zoom grid thumbnails |
| `F5` | Refresh |
| `Double click` | Open file or folder |
| `Middle click folder` | Open folder in new tab |
| `Right click` | Context menu |

## Install

From a local checkout:

```bash
git clone https://github.com/artturihhaavisto-lang/viewstack.git
cd viewstack
make
make install PREFIX="$HOME/.local"
filemanager --set-default "$HOME/.local/bin/filemanager"
```

This installs `filemanager` for the current user under `~/.local`, adds Viewstack to the desktop app launcher with its icon, refreshes desktop caches when available, and sets it as the default handler for folders.

For a system-wide install:

```bash
make
sudo make install PREFIX=/usr/local
filemanager --set-default /usr/local/bin/filemanager
```

Browser upload dialogs are controlled by the desktop file chooser portal/toolkit. Replacing those globally requires an `xdg-desktop-portal` FileChooser backend, not only a default file manager association.

## Uninstall
```bash
cd viewstack
chmod +x uninstall.sh
./uninstall.sh
```

## Build

```bash
make
./build/filemanager [directory]
```

## Requirements

- C compiler
- make
- pkg-config
- GTK3 development headers
- GIO/GDesktopAppInfo headers
- GStreamer development headers
- GStreamer video, pbutils, and app libraries
- Optional runtime helpers for more previews/actions: `ffmpegthumbnailer`, `tar`, `unzip`, `7z`, `pkexec`

Package names:

| Distribution | Packages |
|--------------|----------|
| Arch | `base-devel pkgconf gtk3 glib2 gstreamer gst-plugins-base-libs ffmpegthumbnailer unzip p7zip polkit` |
| Debian/Ubuntu | `build-essential make pkg-config libgtk-3-dev libglib2.0-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev ffmpegthumbnailer unzip p7zip-full policykit-1` |
| Fedora | `gcc make pkgconf-pkg-config gtk3-devel glib2-devel gstreamer1-devel gstreamer1-plugins-base-devel ffmpegthumbnailer unzip p7zip p7zip-plugins polkit` |
| openSUSE | `gcc make pkg-config gtk3-devel glib2-devel gstreamer-devel gstreamer-plugins-base-devel ffmpegthumbnailer unzip p7zip polkit` |

## Usage

```bash
filemanager [directory]
filemanager --set-default [binary-path]
```
