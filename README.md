# HyprChange

A QSettings-style desktop GUI to tune your Hyprland dotfiles without editing Lua by hand.

It reads your existing `hyprland.lua`-style config, merges your changes into
`custom/*.lua` files (which your config already loads **after** the defaults),
and hot-reloads Hyprland so edits apply immediately.

## Features

- Tabbed editor for window rules, environment variables, autostart apps, idles
  and Hyprland keybinds, including the dotfiles' own scripts and settings.
- Configuration panel directly fed by AppData catalogs (`.desktop` entries), so
  apps appear with launch commands; keybinds appear exactly like Hyprland is
  told about them.
- Remap / duplicate / reset keybinds with a record-a-key dialog and live
  conflict highlighting.
- Apply and see exactly which settings changed, in a diff popup.
- Built-in auto-backups: git-style snapshots stored under
  `hyprchange-backups/` in your config dir, so any change can be reverted even
  if you made it a while ago.
- Raw text editors for the custom files, plus a Lua syntax validator (luac).
- Theme & icon pickers, including a built-in **Dark** theme that works from the
  AppImage too. GTK / KDE (Breeze) entries follow your system Qt integration
  and are best with a pacman/system install.
- `--dump` and `--export` CLI helpers for scripts and monitoring.

## Install

Install (or refresh) the app and add it to your start menu with one command:

```bash
curl -fsSL https://raw.githubusercontent.com/Polaricito/HyprChange/master/install.sh | bash
```

That either picks up an existing `hyprchange` binary already installed via
pacman, or downloads the AppImage from the latest build. If FUSE is not
installed, `sudo pacman -S fuse2` lets the AppImage run, or give the installer
a hand and it will just extract itself.

The installer also sets up the icon and the `HyprChange` entry in your start
menu. To update later, run the same command again.

## Manual / AppImage

Every push to `master` is built by CI and published as a rolling release:
https://github.com/Polaricito/HyprChange/releases/tag/continuous — grab the
`HyprChange-*-x86_64.AppImage` there, `chmod +x` it and run it.

## Building from source

Requires Qt 6 + CMake. On Arch:

```bash
sudo pacman -S qt6-base cmake lua
mkdir build && cd build
cmake .. && make            # or: cmake --build . -j
./hyprchange
```

Other distros: install the equivalent `qt6-base-dev` / `vs-qt6`, `cmake`,
`lua5.4` packages, then the same steps.

## What it writes

Only these files in your Hyprland config directory (usually
`.config/hypr/`):

- `custom/*.lua` — keybind remaps/duplicates, variables, autostart, window rules
- `hypridle.conf` — idle / lock / under-power timeouts
- `hyprchange-preferences.conf` — theme & icon theme chosen in the app

Everything else stays untouched, and manual edits to the custom files are kept.

## License

MIT