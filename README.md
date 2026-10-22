# MpVideo

A portable, standalone video player for Linux. Written in C++20 with Qt 6 and powered by libmpv. No GTK interface or web runtime.

Open files or stream URLs, drop media onto the window, manage a playback queue, select audio and subtitles, load subtitle files, adjust playback speed, repeat media, and watch in fullscreen. Dialogs stay inside the player window by default, with an optional desktop file chooser. The flat interface uses theme-colored vector controls.

## Build

Dependencies: CMake, a C++20 compiler, pkg-config, Qt 6.7+ (Core, Gui, Widgets, OpenGLWidgets, DBus), and libmpv development files. The desktop file chooser uses Qt DBus and the XDG Desktop Portal FileChooser service. Tests also require Qt Test, FFmpeg, and `dbus-run-session`; headless render tests can use Xvfb. On Arch Linux the relevant packages are `base-devel cmake ninja pkgconf qt6-base qt6-wayland mpv`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/mpvideo movie.mkv
./build/mpvideo https://example.com/video.mp4
```

The application uses native Wayland when available. Video rendering requires an OpenGL-capable Qt platform. Hardware decoding is requested automatically when supported by mpv and your drivers. Pass repeated `--mpv-option NAME=VALUE` arguments to customize libmpv, for example `--mpv-option hwdec=no` for software decoding. The video output remains embedded in the application.

## Install

```bash
bash scripts/install-desktop.sh --dry-run
bash scripts/install-desktop.sh
```

This installs the executable and a desktop application entry for the current user. Run the installer with `--help` for available options. Standard CMake installation is also supported with `cmake --install build --prefix ~/.local`.

## Live colors

Colors are stored in `${XDG_CONFIG_HOME:-~/.config}/mpvideo/theme.json`. CLI changes are saved atomically and running windows reload them automatically. Invalid edits keep the last valid palette. Theme commands run without opening a window.

```bash
mpvideo theme show
mpvideo theme set background='#e00e1e36' accent='#a8c7fa'
mpvideo theme set 'background=Qt.rgba(14 / 255, 30 / 255, 54 / 255, 0.88)'
mpvideo theme import /path/to/palette.json
mpvideo theme reset
```

Use `mpvideo --help` for the complete command syntax, configurable color roles, source following, custom config directories and dry-run support. Theme files use Qt's `#AARRGGBB` convention: `#e00e1e36` has RGB `#0e1e36` and alpha `0xe0`. Alpha is retained in saved colors; opaque rendering surfaces may discard alpha while preserving RGB.

## Controls

| Key | Action |
| --- | --- |
| Ctrl+O | Open a file, directory or URL |
| Space | Play / pause |
| Left / Right | Seek five seconds |
| Up / Down | Adjust volume |
| M | Mute |
| F | Fullscreen |
| P | Toggle queue |
| Escape | Close a dialog / leave fullscreen |
| Ctrl+Q | Quit |

In **Settings**, leave **Use built-in file picker** checked to select files inside MpVideo’s embedded modal, including URL and queue options. Uncheck it to use the desktop’s configured file chooser through XDG Desktop Portal. This preference also applies to subtitle files and is saved in `settings.ini` beside `theme.json` (including when using `--config-dir`). The desktop picker supports selecting multiple media files. If the desktop chooser is unavailable, MpVideo shows an error instead of substituting a separate Qt dialog.

The built-in Open dialog includes an embedded directory browser. Select a file and press Open, or activate it directly. Check **Add to queue** to append a file. Press Delete on a queue item to remove it. Directory activation navigates; URLs can be pasted into the location field. Playback errors appear below the controls.

## Implementation

MpVideo embeds libmpv through its client and OpenGL render APIs. Its implementation is independent; [Celluloid's integration](https://github.com/celluloid-player/celluloid/blob/master/src/celluloid-mpv.c) and [mpv's embedding documentation](https://mpv.io/manual/master/#embedding-into-other-programs-libmpv) were references.

For an instrumented development build, configure with `-DMPVIDEO_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug`.
