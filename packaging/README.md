# 打包说明

仓库提供完整的 `debian/` 打包目录（`control`、`rules`、`changelog`、
`copyright`）以及 `yincolor.qarchiver.desktop`。`cmake --install` 会安装桌面入口、
hicolor 图标、文档与内置 7-Zip。本文件列出打包者需要声明的依赖与平台说明。

## 构建期依赖

| 依赖 | Debian/Ubuntu 包 | 最低版本 |
| --- | --- | --- |
| Qt 6 基础库、SVG 与工具 | `qt6-base-dev`, `qt6-svg-dev`, `qt6-tools-dev` | 6.4 |
| 构建工具 | `cmake`（>= 3.20）, `g++`（>= 12）或 `clang`（>= 15） | — |
| libarchive 开发头文件（可选） | `libarchive-dev`（>= 3.4） | 3.4 |

`debian/control` 已声明：

```text
Build-Depends: debhelper-compat (= 13), cmake (>= 3.20),
               g++ (>= 12) | clang (>= 15),
               qt6-base-dev (>= 6.4), qt6-svg-dev (>= 6.4),
               qt6-tools-dev (>= 6.4),
               libarchive-dev (>= 3.4), pkg-config
```

若以 `-DQARCHIVER_USE_LIBARCHIVE=OFF` 构建，可去掉 `libarchive-dev`。

构建依赖可以用 `mk-build-deps` 管理：

```bash
mk-build-deps --install --remove -t 'apt-get -y' debian/control
# 或者生成依赖包后自行安装
mk-build-deps --build-dep -t 'apt-get -y' debian/control
```

打包：

```bash
dpkg-buildpackage -us -uc -b
```

## 运行期依赖

| 能力 | 依赖 | Debian/Ubuntu 包 | 备注 |
| --- | --- | --- | --- |
| 启动 | Qt 6 运行库 | `libqt6core6`, `libqt6gui6`, `libqt6widgets6`, `libqt6concurrent6` | 由 dh_shlibdeps 自动生成，通常不用手写 |
| SVG 缩略图 | Qt SVG 运行库 | `libqt6svg6` | 构建时链接 Qt6::Svg；由 dh_shlibdeps 自动生成 |
| 读取 zip / 明文 7z / tar 系 / gz|bz2|xz | libarchive（可选） | `libarchive13` / `libarchive13t64` | 构建时链接，启用时运行期必须声明 |
| 7z / zip / tar 系读写、纯流、RAR 只读 | 内置 7-Zip | 无（随包安装） | 安装到 `/usr/libexec/qarchiver/7zip/7zzs`，不要额外声明 `7zip` / `tar` / `unrar` 依赖 |
| 标准对话框按钮、文件对话框中文文本（可选） | Qt 基础翻译 | `qt6-translations-l10n` | 缺失时应用自身文本仍为中文，仅 Qt 自带控件文本保持英文 |

注意：

- 若以 `-DQARCHIVER_USE_LIBARCHIVE=OFF` 构建，则不应声明 libarchive 依赖；
  7-Zip 辅助程序仍由 `cmake --install` 自动安装，无需改为系统依赖。
- SVG 缩略图由链接的 `Qt6::Svg`（`libqt6svg6`）通过 `QSvgRenderer` 渲染，
  不再依赖 `imageformats/qsvg.so`；`qt6-image-formats-plugins` 仍建议安装，
  用于 WebP/TIFF 等其他图片格式。
- 打 AppImage / Flatpak 时请确认 `libexec/qarchiver/7zip/7zzs` 被包含进包；
  tar 与 RAR 读取都由该内置程序完成，不需要宿主机安装 `tar` / `unrar`。

## Debian 控制文件片段

```text
Package: yincolor.qarchiver
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}
Recommends: qt6-translations-l10n, qt6-image-formats-plugins
Suggests: rar
Conflicts: qarchiver
Replaces: qarchiver
Description: Qt based archive manager
 小Q解压 is a Qt 6 archive manager for browsing, extracting, creating,
 appending to, deleting from and testing 7z, zip, tar, gz, bz2, xz and rar
 archives. Archives are read in-process through libarchive when possible and
 fall back to the bundled 7-Zip helper (which also handles tar write and
 RAR read) otherwise.
```

`${shlibs:Depends}` 会在链接了 libarchive 时自动带出 `libarchive13` /
`libarchive13t64`，因此不必把它写进 `Recommends`（否则 lintian 会报
`duplicate-in-relation-field`）；若以 `-DQARCHIVER_USE_LIBARCHIVE=OFF` 构建，
依赖列表里也不会出现它。

`Conflicts` / `Replaces: qarchiver` 用于包改名后的平滑升级：旧包与
`yincolor.qarchiver` 安装同一批文件（`/usr/bin/QArchiver` 等），没有
`Replaces` 时 dpkg 会因文件冲突拒绝安装。

文档目录由 `-DQARCHIVER_DOC_DIR=doc/yincolor.qarchiver` 指定，使
README/CHANGELOG/LICENSE 与 7-Zip 许可证落在 `/usr/share/doc/yincolor.qarchiver/`
（Debian 政策要求文档目录与二进制包名一致）。

## 桌面文件

`yincolor.qarchiver.desktop` 已声明 `MimeType`（文件名必须与包名一致），并提供一个
`OneClickExtract` 桌面动作（`QArchiver --oneclick %F`）。安装后需要一次
`update-desktop-database`（debhelper / `desktop-file-utils` 的触发器会处理）。
图标安装为 `share/icons/hicolor/{32x32,48x48,64x64,128x128,256x256,512x512}/apps/qarchiver.png`，
并提供 `share/pixmaps/qarchiver.png` 兜底；`Icon=qarchiver` 与之对应。
desktop 文件设置 `StartupWMClass=QArchiver`，并使用合法的
`Categories=Utility;Archiving;`。

## Windows

Windows 使用 `cmake --install` + `qt_generate_deploy_app_script()` 部署 Qt，
`cpack` 生成 `QArchiver-<version>-windows-<arch>.zip`，再由
`packaging/windows/build-installer.ps1` 调用 Inno Setup（`QArchiver.iss`）
生成 `QArchiver-<version>-windows-<arch>-setup.exe` 安装器。链接 `Qt6::Svg` 后
`windeployqt` 会自动部署 `Qt6Svg.dll`；SVG 缩略图使用 `QSvgRenderer`，不要求
`imageformats\qsvg.dll`。安装器提供开始
菜单、可选桌面快捷方式、卸载入口，并询问是否注册资源管理器右键菜单
（`QArchiverShell.dll`，动态“添加到 xx.zip”文案）与关联压缩包格式
（默认选中）。便携包则通过 `packaging/windows/install-shell-menu.ps1`
为当前用户注册同一套菜单；文件关联也可在“偏好设置 → 工具”中管理
（Windows 注册表 / Linux xdg-mime）。交叉编译与更多细节见
`packaging/windows/README.md`。

## GitHub Actions

`build.yml` 是供 CI/Release 复用的构建矩阵：

- Linux amd64/arm64 在原生 runner 上生成 `.deb`；
- Windows x64 在 `windows-2022` 上原生构建、测试并生成便携 ZIP 与安装器；
- Windows ARM64 使用 Qt `win64_msvc2022_arm64_cross_compiled` 交叉构建，
  内置原生 ARM64 7-Zip，生成 `QArchiver-<version>-windows-arm64.zip`
  及其安装器。

推送 `v*` 标签会触发 `release.yml`，在同一个 Release 中上传安装包
（Windows setup.exe + Linux deb）与 Windows 便携 ZIP，并附带一个
覆盖全部产物的 `SHA256SUMS`。
