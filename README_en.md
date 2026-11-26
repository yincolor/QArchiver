# QArchiver / 小Q解压

English | [中文](./README.md)

[![CI](https://github.com/yincolor/QArchiver/actions/workflows/ci.yml/badge.svg)](https://github.com/yincolor/QArchiver/actions/workflows/ci.yml)
[![Release](https://github.com/yincolor/QArchiver/actions/workflows/release.yml/badge.svg)](https://github.com/yincolor/QArchiver/releases)

A Qt 6–based graphical archive manager: browse, extract, create, append, delete, and test
7z / zip / tar / gz / bz2 / xz / zst / rar archives, and read ISO disc images. The default interface follows the modern
HaoZip style and can be switched instantly back to the classic layout in
"Preferences → Interface"; Linux x86_64/ARM64 and Windows x86_64/ARM64 are supported.

![Modern main window](docs/screenshots/modern-main.png)

## Downloads

Pushing a `v*` tag makes the release workflow publish one release containing
both the installers and the portable Windows ZIP:

| Platform | Artifact |
| --- | --- |
| Linux x86_64 | `yincolor.qarchiver_<version>_amd64.deb` |
| Linux ARM64 | `yincolor.qarchiver_<version>_arm64.deb` |
| Windows x86_64 | `QArchiver-<version>-windows-x64-setup.exe` (installer) |
| Windows ARM64 | `QArchiver-<version>-windows-arm64-setup.exe` (installer) |
| Windows x86_64 | `QArchiver-<version>-windows-x64.zip` (portable) |
| Windows ARM64 | `QArchiver-<version>-windows-arm64.zip` (portable) |

The Windows installer asks whether to associate archive formats (checked by
default), adds Start Menu/desktop shortcuts, an uninstall entry and the Explorer
context menus (open, one-click extract, dynamic "add to <name>.zip", compress
to 7z/ZIP/tar...). The installer is bilingual (Simplified Chinese/English).
The portable ZIP needs no installation: unzip and run `QArchiver.exe`; the
context-menu scripts are in the same directory. The release includes one
`SHA256SUMS` for all assets.

Publish a release:

```bash
git tag 2.4.1
git push origin 2.4.1
```

## Features

- Immersive frameless title bar with integrated menus, minimize/maximize/close, drag, double-click maximize and edge/corner resizing; the window starts centred
- Modern HaoZip-style interface: flat gradient header, large colorful toolbar, folder and details panes,
  Windows 7 style breadcrumb navigation, in-archive search, and an archive status summary
- Start page: the modern layout shows a welcome page with drop area, recent files, Open /
  One-click Extract entries and a File Manager button; the classic layout has no welcome page and
  uses the local file browser as its start page when no archive is open
- Local file manager (opened from the modern welcome page's File Manager button, or entered
  automatically by the classic layout on startup/layout switch): navigation, search,
  grid/list/tree views, image thumbnails, double-click to enter a folder or open a file, and
  right-click compression; the modern layout shows a system-places sidebar (Home, Desktop,
  Documents, Downloads, Pictures, Music, Videos, File System and user-mounted volumes), while
  the classic layout does not
- The navigation bar above the archive list provides grid/list/tree switching on both the welcome and archive pages
- Sort menu in the context menu and next to the view switcher: name/size/type/modified,
  ascending or descending, remembered across sessions (also applies to the grid view)
- Modern layout: icon-size slider and Ctrl+wheel zoom, 10pt default font and
  larger grid items, and clearer sidebar/content contrast
- File-association manager (Preferences → Tools): Windows per-user ProgIDs,
  Linux xdg-mime / mimeapps.list; the installer asks to associate formats
- Explorer context menu is an in-process extension: dynamic "add to <name>.zip"
  labels, no compress commands for archive files, and shell launches show only
  the operation dialogs (no main window)
- One-click extraction: automatically extracts into a separate folder named after the archive;
  on directory conflicts it automatically uses unique names such as `name (1)`; supports a first-use
  prompt and "close the program after extraction"; extracts only the selection when items are selected
- 7-Zip benchmark (`7z b`): live output, thread count selection, stop, and copy results
- Modern and classic layouts can be switched instantly in Preferences; the classic layout keeps the
  original toolbar, recent-files sidebar, background image, and toolbar style options. Its filter row
  hosts Forward, Back and Up buttons plus a breadcrumb bar, with a fixed 280px search field
- Breadcrumb levels are clickable; long paths collapse middle levels behind a "..." menu, and
  clicking the empty area on the right shows the read-only full path ready to select and copy
- Browse archive contents in a grid by default, switchable between grid, list, and tree views; filtering and sorting supported;
  grid icons are centred and long filenames wrap across multiple lines, with the cell height following the longest name in the current folder
- Extraction: all or selected entries, overwrite strategies (ask/overwrite/skip/rename), password;
  non-UTF-8 member names (GBK/GB18030/Big5/Shift-JIS) are detected automatically
- Compression: 7z / zip / tar / tar.gz / tar.bz2 / tar.xz / gz / bz2 / xz; if `rar` is installed on the system, RAR and RAR volumes can also be created
- Read `.tar.zst` / `.tzst` / `.zst` (read-only, using the bundled 7-Zip)
- Read `.iso` disc images (ISO9660/Joliet/Rock Ridge and UDF, read-only): the in-process
  libarchive reader is preferred and UDF images fall back to the bundled 7-Zip
  automatically; file associations and the open dialog accept `.iso` too
- Append and delete entries (7z / zip / tar families)
- Edit a member and save it back: double-click opens the file with the system default
  application, and once you save there QArchiver asks whether the change should be stored
  in the archive. Confirming replaces the member in place (legacy GBK/Big5/Shift-JIS member
  names included); declining leaves the archive untouched
- Integrity testing, with optional automatic test before extraction
- In-archive single-file preview (text/images); images are pre-extracted together with other images in the same directory, supporting continuous viewing in the system viewer.
  When the number of images in the same directory exceeds a configurable threshold (default 200, 0 means any batch), a cancellable preparation dialog is shown;
  this threshold only controls whether the prompt appears and does not limit the total batch extraction size
- Icons for common file kinds: images, audio/video, PDF, Office, code, fonts, disk images, e-books, databases, torrents, design files, subtitles, 3D/CAD, packages, and more
- Native system file dialogs; compression sources ask for files or a folder, then use the native dialog
- Images in archives and local folders show real thumbnails (read/extracted in the background and
  persistently cached), including PNG/JPEG/WebP/SVG: modern grid/list/tree views all show them,
  with modern list thumbnails fitted to the row height and a bounded width; the classic layout
  only shows thumbnails in the grid view, while its list/tree keep the regular image icon.
  Double-click still opens the system viewer for continuous browsing
- Drag-out: drag selected entries directly to a file manager/desktop to extract them while preserving directory structure
  (extraction happens in the background to a staging directory, cancellable with `Esc`; a second drag of the same selection is zero-latency)

## Security and Reliability

- Zip Slip path traversal protection; extraction first goes to a temporary directory and is then moved, with immediate cleanup of temporary content on failure
- If only some files succeed during the move phase of extraction, the user is explicitly warned that "moved files were not rolled back" rather than silently treating it as complete success
- Two layers of symlink protection: pre-check before extraction and prompt "abort/skip/skip all for this task"; re-check actual links before moving;
  when a directory link is skipped, its entire subtree is skipped as well
- Hard links read by libarchive are safely recreated; if the target is missing, out of bounds, or itself a symlink, the task aborts with a prompt
  and does not silently create an empty file
- Atomic archive replacement: old archives/volumes are backed up first, and only after all new files are in place are they replaced; failures roll back automatically
- Archive list output is parsed as a stream, avoiding retention of the full command-line text just for listing; size accumulation includes overflow checks
- Warnings are shown when opening the system viewer or extraction target directory fails, rather than failing silently
- External-edit write-back only applies to writable formats (zip / 7z / uncompressed tar);
  iso, rar, split volumes and compressed tar streams never prompt. A failed write-back is
  reported clearly and is not retried in a loop (toggle it in Preferences, "Ask before saving
  externally edited files back into the archive")

## Interface

The default layout is modeled on HaoZip: a large colorful toolbar at the top, a folder pane, a details pane,
breadcrumb path navigation and in-archive search, and an archive status summary. The modern layout uses a completely
new flat theme and colorful file-type icons.

| Modern layout (default) | Classic layout |
| --- | --- |
| ![Modern archive view](docs/screenshots/modern-archive.png) | ![Classic layout](docs/screenshots/classic-layout.png) |

- Modern and classic layouts switch instantly in "Preferences → Interface" without a restart;
  switching is disabled while an archive operation is in progress so it does not interrupt progress.
- Both light and dark themes work with both layouts; Chinese and English UI. Dark theme preview: [modern-dark.png](docs/screenshots/modern-dark.png).
- The classic layout keeps the original Frutiger Aero skin, the optional recent-files sidebar, the toolbar style,
  and the semi-transparent background image (nine alignments, custom size and opacity).
- The modern layout contains no ads, bundled installers, or "security scan" pop-ups.

## One-click Extraction

After clicking "One-click Extract" in the toolbar (or the button on the welcome page, or the command-line `--oneclick`):

1. If no archive is open yet, first choose an archive;
2. By default it extracts into a separate folder named after the archive in the archive's directory;
   this can also be changed to a custom directory in settings;
3. If the target folder already exists, unique names such as `name (1)` and `name (2)` are generated automatically,
   and existing files are never silently overwritten;
4. On first use it asks whether to close the program after extraction; this can later be changed in
   "Preferences → Extraction → One-click extraction";
5. If extraction succeeds and "close after completion" is set, the window closes automatically; on failure or cancellation it stays open.

## 7-Zip Benchmark

"Preferences → Tools → Run 7-Zip benchmark..." or "Tools → 7-Zip Benchmark..." executes the bundled
`7z b` to measure compression/decompression throughput and CPU frequency. The test runs in a background
process, can be stopped at any time, and its output can be copied. It does not modify any archive.

## Configuration

The background image is selected in "Preferences → Interface → Background image (classic layout)"; an empty path disables it.
Leaving size blank/0 means "automatic" (when only one of width/height is filled, the aspect ratio is preserved),
and opacity ranges from 0–100%.

One-click extraction configuration keys:

- `extract/oneClickDestMode`: `archive` (same directory as the archive, default) or `custom`;
- `extract/oneClickDest`: custom destination root directory;
- `extract/oneClickCloseAfter`: whether to close the program after successful one-click extraction;
- `extract/oneClickCloseAsked`: whether the first-use prompt has already been shown.

Other configurable items (Preferences):

- Interface layout: `ui/layoutMode`, `modern` (default) or `classic`;
- Content view: `ui/contentViewMode`, `grid` (default) / `tree` / `list`;
- Icon size: `ui/itemSize`, default 96, range 48–128; classic list/tree use a fixed compact size;
- Continuous image viewing preparation dialog threshold: `preview/continuousImageDialogThreshold`, default 200, range 0–9999;
- Recent files limit: `history/recentFilesLimit`, default 20, range 1–100; lowering it trims immediately and refreshes the sidebar.

## Dependencies

Build-time (Debian/Ubuntu package names, available from Qt 6.4):

| Dependency | Required | Notes |
| --- | --- | --- |
| Qt 6.4+ (Core / Gui / Widgets / Concurrent / Svg) | Required | `qt6-base-dev (>= 6.4)`, `qt6-svg-dev (>= 6.4)`; `Svg` provides `QSvgRenderer` for SVG thumbnails |
| Qt LinguistTools (lupdate/lrelease) | Required | `qt6-tools-dev (>= 6.4)`, translations are compiled at build time |
| CMake 3.20+, GCC 12+/Clang 15+ (C++20) | Required | |
| libarchive 3.4+ (development headers) | Optional (enabled by default) | `libarchive-dev`; if missing, only a WARNING is shown and it automatically falls back to the pure command-line backend |
| MinGW-w64 | Optional | Cross-compile the Windows version; native Windows builds can also use MSVC 2022 |

Runtime:

| Dependency | Required | Notes |
| --- | --- | --- |
| 7-Zip CLI | Bundled | `third_party/7zip/` (Linux `7zzs`, Windows `7z.exe + 7z.dll`) distributed with the program; handles 7z/zip/tar read/write, pure stream extraction, and RAR reading |
| rar command | Optional | Enables RAR creation and RAR volume creation only when installed on the system; `QARCHIVER_RAR_PATH` can specify the path |
| libarchive13 | Required when libarchive is enabled | The program loads the shared library detected at build time |
| Qt SVG runtime | Required | `libqt6svg6`, used to render SVG thumbnails; `${shlibs:Depends}` declares it automatically when linked. On Windows, `windeployqt` deploys `Qt6Svg.dll` |
| qt6-translations-l10n | Optional | Provides `qtbase_<lang>.qm` for localizing standard dialog buttons and file dialogs; if missing, the application's own text is still translated |

At build time, the 7-Zip helper for the current architecture is copied next to the executable into the `7zip/`
directory (on Linux installed to `libexec/qarchiver/7zip/`). At runtime this bundled
version is preferred, and only if it cannot be found does it fall back to `7z` / `7zzs` in the system PATH.

At startup the fallback toolchain is checked:

1. First look for the 7-Zip command-line tool in program-related directories (`<executable dir>/7zip`, `libexec/qarchiver/7zip`, etc.)
   and confirm via `--help` that it is indeed 7-Zip;
2. If the bundled tool is not found, then look for 7-Zip in the system PATH and determine whether it supports RAR extraction
   via the `Codecs:` section of the `7z i` output;
3. If a system 7-Zip is being used, a one-time popup is shown after startup with the path used and RAR support status;
   if none is found at all, the user is prompted to download the official 7-Zip command-line tool into the `7zip/` directory, or
   on Debian/Ubuntu to install the system package (including `7zip-rar` / `p7zip-rar`).
   Detection runs in a background thread and does not block window display.

`QARCHIVER_7Z_PATH` can explicitly specify the 7-Zip executable, bypassing the automatic detection above.

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
sudo cmake --install build            # optional, installs to /usr/local by default
```

If the launcher does not show the icon immediately, refresh the desktop caches
(replace `PREFIX` with the install prefix, e.g. `/usr/local` or `/usr`):

```bash
sudo update-desktop-database PREFIX/share/applications
sudo gtk-update-icon-cache -f PREFIX/share/icons/hicolor
```

Icons are installed at 32/48/64/128/256/512 px and the desktop entry sets
`StartupWMClass`; `.deb` installs refresh the caches through Debian triggers.

On Debian/Ubuntu, the build dependencies can be installed directly from the repository's `debian/control`:

```bash
mk-build-deps --install --remove -t 'apt-get -y' debian/control
# or build the dependency package first, then install it with aptss/apt
mk-build-deps -t 'apt-get -y' --build-dep debian/control
```

Packaging:

```bash
dpkg-buildpackage -us -uc -b
```

Common options:

| Option | Default | Notes |
| --- | --- | --- |
| `-DQARCHIVER_USE_LIBARCHIVE=ON|OFF` | `ON` | When off, only the command-line backend is compiled and libarchive is not linked |
| `-DCMAKE_BUILD_TYPE=Release|Debug|RelWithDebInfo` | empty | |
| `-DBUILD_TESTING=OFF` | `ON` | Whether to generate `tests/` |
| `-DQARCHIVER_ENABLE_SANITIZERS=ON` | `OFF` | Build with ASan + UBSan (GCC/Clang only; combine with Debug and `BUILD_TESTING=ON`) |

If libarchive only provides a pkg-config file and no CMake config package, the build script automatically
falls back to `pkg_check_modules`.

## Source layout

The sources are organised in layers, one static library each; dependencies only
point downwards (`ui -> core -> backends -> utils -> common`):

```text
src/
├── app/        command line parsing and translation setup
├── common/     format suffix table, errors, basic data types (no Widgets)
├── utils/      subprocess, paths, encoding, 7-Zip discovery, logging
├── backends/   tar / sevenzip / rar / stream / libarchive
├── core/       ArchiveManager, backend factory, extraction preflight, settings
└── ui/         MainWindow (split by concern), models, services, theme, icons
resources/      icons, theme glyphs and style sheets (compiled into the binary)
cmake/          7-Zip bundling, libarchive probing, Windows resources, toolchains
packaging/      Windows .rc.in / manifest and Explorer integration scripts
tests/          Qt Test cases, all registered through qarchiver_add_test()
tools/          icon generation, qrc generation and resource validation
docs/           design document (design.zh_CN.md) and screenshots
```

See [`docs/design.zh_CN.md`](docs/design.zh_CN.md) for modules, interfaces and
flows (Chinese).

Icons and style sheets live in `resources/` at the repository root and are
compiled into the executable through `icons.qrc`, `themes-aero.qrc` and
`themes-modern.qrc`, so no asset directory is needed at runtime. After changing
artwork, regenerate the manifests and validate them:

```bash
python3 tools/gen_aero_assets.py        # classic glyphs (PyQt6)
python3 tools/gen_modern_icons.py       # modern icons (needs rsvg-convert)
python3 tools/normalize_classic_icons.py
python3 tools/update_qrc.py
python3 tools/check_resources.py
```

## Read Backends and Switches

There are two read paths:

- **libarchive (default)**: zip (including ZipCrypto/AES), unencrypted 7z, tar / tar.gz /
  tar.bz2 / tar.xz, and single-file gz / bz2 / xz are parsed in-process without launching external programs.
  Cases it cannot handle (encrypted 7z, 7z/zip volumes, rar) automatically fall back to the command-line backend
  and replay the pending operation once.
- **Command line**: 7z/zip/tar/gz/bz2/xz are read/written by the bundled 7-Zip, and RAR reading is done by the bundled 7-Zip,
  with no further dependency on system `tar` / `unrar`. If `rar` is installed on the system, RAR creation and volume creation use that command.
  7-Zip calls uniformly use `-sccUTF-8`, `--`, and `-spd`; selected members are passed via UTF-8 or a raw-codepage
  listfile, avoiding wildcards, `@`, leading `-`, and legacy ZIP encoding issues.

Writing (compression, appending, deletion, volumes) is always done by the command line; the libarchive backend is read-only.
When adding/deleting in zip / 7z / uncompressed tar while the current backend is the read-only libarchive backend,
the program automatically reopens the same archive with the command-line backend and replays the operation, so
"Add" and "Delete" remain directly usable in the UI; tar.gz/tar.xz/tar.bz2, RAR, and volume formats still do not
support append/delete.

Two-level switch, either of which can disable libarchive:

1. Settings → Preferences → "Use libarchive for reading archives"
   (config key `backends/useLibArchive`, enabled by default). **Changes take effect the next time an archive is opened**
   and do not interrupt the currently open archive session.
2. Environment variable `QARCHIVER_NO_LIBARCHIVE=1`: disables it at startup with higher priority than the config item,
   suitable for comparative troubleshooting.

## Windows

See [`packaging/windows/README.md`](packaging/windows/README.md) for Windows build, ZIP packaging, Explorer context
menu, and cross-compilation instructions. Key points:

- Native exe icon, version resource, and Per-Monitor V2 DPI manifest;
- GBK/Big5/Shift-JIS filenames are converted through the Win32 API without depending on iconv;
- The console window is hidden when launching 7z/rar;
- Windows x64 and ARM64; the ARM64 package uses the native ARM64 7-Zip helper;
- `cmake --install` uses `windeployqt` to deploy the Qt runtime libraries, and `cpack` generates a portable ZIP;
  `packaging/windows/QArchiver.iss` builds an installer on top of that ZIP;
- The installer and `packaging/windows/install-shell-menu.ps1` register "Open with QArchiver", "One-click extract",
  "Add to <name>.zip" for files and folders, and a "Compress to..." submenu (7z/ZIP/tar.gz/tar.xz/tar.bz2/tar);
- On Debian/Ubuntu, `tools/build-windows-cross.sh` can cross-compile and assemble a portable directory;
- GitHub Actions builds x64 on `windows-2022` and ARM64 with
  `win64_msvc2022_arm64_cross_compiled`.

## Command Line

```bash
QArchiver [archive]                 # open an archive
QArchiver --oneclick <archive>      # one-click extraction
QArchiver --compress zip <files>    # create <name>.zip next to the sources
QArchiver --compress 7z <files>     # create a 7z archive
QArchiver --compress tar.gz <dir>   # create a tar.gz archive
QArchiver --output <path>           # target path for --compress
QArchiver --compress-dialog <files> # open the compress dialog
QArchiver --preferences             # open Preferences
QArchiver --benchmark               # open the 7-Zip benchmark
QArchiver --lang en                 # specify the UI language
```

## Testing and CI

GitHub Actions:

- `.github/workflows/build.yml`: reusable matrix that builds Linux amd64/arm64
  inside Debian 12 (bookworm, the oldest supported baseline) containers and
  produces .deb packages, then verifies dependency resolution and installation
  in clean Debian 12 / Debian 13 / Ubuntu 24.04 containers (shared-library
  dependencies, bundled 7-Zip and desktop entry), builds and tests Windows x64
  natively, and cross-builds Windows ARM64;
- `.github/workflows/ci.yml`: build and test on every push and pull request;
- `.github/workflows/release.yml`: on a `v*` tag, build all four artifacts,
  generate `SHA256SUMS`, and create a GitHub Release.

Local testing:

```bash
cd build
ctest --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --output-on-failure   # headless environment
```

Test coverage includes: utility functions (path normalization/traversal detection/size overflow/symlink targets), CLI backend,
manager (dispatch, cancellation, fallback, keeping the current archive on open failure, dangerous symlink decisions),
libarchive backend (round-trip, AES password, corruption detection, selective extraction, hard link reconstruction),
preferences dialog and config sync (continuous image threshold, recent files limit), RAR/RAR volume creation and
old volume replacement, fallback 7-Zip `--help` validation and RAR Codecs detection, continuous viewing of images in the same directory,
one-click extraction destination planning (compound suffixes, split first volume, existing-directory avoidance),
SVG thumbnails (local and archive entries, rendered with `QSvgRenderer`), list-mode directory indexing,
background image caching, the immersive title bar (kept across
layout switches, draggable menu-bar gap, token-driven colours), and the system-places sidebar
(no duplicate paths, subdirectory ancestor highlighting, file-system fallback). Tests use the bundled 7-Zip via `QARCHIVER_7Z_PATH`;
RAR creation test cases are automatically skipped when the system has no `rar`, and `QARCHIVER_RAR_PATH` can be used to
specify the rar executable under test.
The `tarDotPrefixAndNonAscii` compatibility test case is automatically skipped when the system has no `tar`;
test cases that depend on Unix symlinks are automatically skipped on Windows.

## Debugging and Diagnostics

| Environment Variable | Purpose |
| --- | --- |
| `QARCHIVER_DEBUG=1` | Output Debug-level logs (legacy `SAM_DEBUG` is still accepted) |
| `QARCHIVER_NO_LIBARCHIVE=1` | Disable the libarchive backend at startup |
| `QARCHIVER_7Z_PATH` | Explicitly specify the 7-Zip command-line tool path, skipping automatic detection |
| `QARCHIVER_RAR_PATH` | Explicitly specify the rar command path, used for RAR creation |
| `QARCHIVER_DRAG_DEBUG_NO_EXEC=1` | When dragging out, only prepare staging and print URLs, without entering the blocking native drag loop |

For drag-and-drop troubleshooting tools (a drag source that can automatically reproduce/compare under X11 and a logging receiver), see
`~/.app/ai_tools/dnd-drag-test/`; when drag-out fails, first check "whether the target side requests data",
which is more reliable than checking the return value of `QDrag::exec()`.

## Known Limitations

- Encrypted 7z cannot be read with libarchive and falls back to the 7z command line (still requires a password)
- libarchive does not support 7z/zip volumes; volume archives go directly through the command line when opened
- RAR reading is done by the bundled 7-Zip; creating RAR/RAR volumes requires `rar` installed on the system,
  and appending/deleting in existing RARs is still unsupported
- Dangerous symlinks abort extraction by default; when the user chooses "skip", that link (or the entire subtree for directory links) is not extracted
- Hard links are safely recreated only in the libarchive read backend; if the target is missing/out of bounds/is a symlink, the task aborts with a prompt
- Display/editing of archive comments is no longer provided
- On Windows, extracting symlinks through libarchive requires Developer Mode or administrator privileges;
  the command-line 7-Zip backend usually follows/skips links directly
- One-click extraction does not show a directory picker by default; use "Extract to..." when a directory must be chosen each time

## License

AGPL-3.0, see `LICENSE`. At runtime the program invokes external tools and links with libarchive (BSD-2-Clause);
the bundled 7-Zip is distributed under GNU LGPL (including the unRAR restriction), see
`third_party/7zip/License.txt`, installed to `share/doc/qarchiver/`
(`share/doc/yincolor.qarchiver/` inside the Debian package).
The program no longer depends on non-free `unrar`; RAR reading is done by the bundled 7-Zip.
RAR creation is optional and enabled only when the user's system already has the `rar` command installed; the program itself does not distribute that command.

## Changelog

See [`CHANGELOG.md`](CHANGELOG.md).

## Packaging Reference

See [`packaging/README.md`](packaging/README.md) and [`packaging/windows/README.md`](packaging/windows/README.md).
