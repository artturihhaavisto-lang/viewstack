# Viewstack

Viewstack is a sharp GTK3/C file manager with media-focused previews, compact list/grid browsing, sidebar drives, and Linux desktop integration.

It is a companion project to [imgview](https://github.com/artturihhaavisto-lang/imgview), sharing the same black, crisp, low-padding visual direction.

## Build

```sh
make
./build/filemanager
```

## Install

```sh
make install PREFIX="$HOME/.local"
filemanager --set-default "$HOME/.local/bin/filemanager"
```

`--set-default` registers `filemanager.desktop` as the default handler for folders. Browser upload dialogs are controlled by the desktop file chooser portal/toolkit and require an xdg-desktop-portal FileChooser backend to replace globally.
