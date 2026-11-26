# QArchiver / 小Q解压

[English](./README_en.md) | 中文

[![CI](https://github.com/yincolor/QArchiver/actions/workflows/ci.yml/badge.svg)](https://github.com/yincolor/QArchiver/actions/workflows/ci.yml)
[![Release](https://github.com/yincolor/QArchiver/actions/workflows/release.yml/badge.svg)](https://github.com/yincolor/QArchiver/releases)

基于 Qt 6 的图形化压缩包管理器：浏览、解压、创建、追加、删除、测试
7z / zip / tar / gz / bz2 / xz / zst / rar 归档，并可读取 ISO 光盘镜像。默认界面采用现代好压（HaoZip）
风格，可在“偏好设置 → 界面”中即时切回经典布局；支持 Linux x86_64/ARM64
与 Windows x86_64/ARM64。

![现代主界面](docs/screenshots/modern-main.png)

## 下载

推送 `v*` 标签后，Release 工作流会在同一个 Release 中发布安装版与便携版：

| 平台 | 产物 |
| --- | --- |
| Linux x86_64 | `yincolor.qarchiver_<version>_amd64.deb` |
| Linux ARM64 | `yincolor.qarchiver_<version>_arm64.deb` |
| Windows x86_64 | `QArchiver-<version>-windows-x64-setup.exe`（安装版） |
| Windows ARM64 | `QArchiver-<version>-windows-arm64-setup.exe`（安装版） |
| Windows x86_64 | `QArchiver-<version>-windows-x64.zip`（便携版） |
| Windows ARM64 | `QArchiver-<version>-windows-arm64.zip`（便携版） |

Windows 安装器会询问是否关联压缩包格式（默认选中），并安装开始菜单/桌面
快捷方式、卸载入口和资源管理器右键菜单（打开、一键解压、动态“添加到
xx.zip”、压缩到 7z/ZIP/tar 等）。安装器提供简体中文 / English 双语界面。
便携 ZIP 解压后运行 `QArchiver.exe` 即可，右键菜单脚本在同一目录。Release
同时提供包含全部产物的 `SHA256SUMS`。

发布新版本：

```bash
git tag 2.4.1
git push origin 2.4.1
```

## 功能

- 沉浸式无边框标题栏：集成菜单、最小化/最大化/关闭，支持拖动、双击最大化与四边四角拖拽缩放；首次启动自动居中
- 现代好压风格界面：扁平渐变头部、大号彩色工具栏、文件夹与详细信息面板、
  Windows 7 风格面包屑路径导航、包内搜索、归档状态摘要
- 起始页：现代布局显示拖放区、最近打开列表、打开/一键解压入口和“文件管理”按钮；
  经典布局没有欢迎页，无压缩包时直接以本地文件浏览作为起始页
- 本地文件管理（现代布局从起始页“文件管理”进入，经典布局启动或切换后自动进入）：
  目录导航、搜索、网格/列表/树形视图、图片缩略图、双击进入目录或打开文件、
  右键压缩；现代布局显示系统位置侧栏（主目录、桌面、文档、下载、图片、音乐、
  视频、文件系统与用户挂载卷），经典布局不显示该侧栏
- 导航栏在压缩包列表上方提供网格/列表/树形切换，主页与归档页均可使用；
  经典布局也可在偏好设置中切换内容视图
- 排序菜单：右键空白处或视图旁的排序按钮，可按名称/大小/类型/修改时间
  升序或降序排列，并记住上次选择（网格视图同样生效）
- 现代布局：导航栏图标大小滑块，Ctrl + 滚轮缩放；10pt 默认字体与更大的
  网格图标；左侧栏与内容区配色区分
- 文件关联工具（设置 → 工具）：Windows 写入当前用户 ProgID，Linux 使用
  xdg-mime / mimeapps.list；安装器默认勾选“关联压缩包格式”
- 资源管理器右键菜单为进程内扩展：动态显示“添加到 xx.zip”，压缩包本身不再
  显示压缩命令；从菜单启动时只弹操作对话框，不显示主窗口
- 一键解压：自动解压到以压缩包命名的独立文件夹；目录冲突时自动使用
  `名称 (1)` 等唯一名称；支持首次提示与“解压完成后关闭程序”；选中条目时
  只解压所选内容
- 7-Zip 性能测试（`7z b`）：实时输出、线程数选择、停止与复制结果
- 现代 / 经典两种布局可在偏好设置中即时切换，经典布局保留原有工具栏、
  最近文件侧栏、背景图片与工具栏样式选项；筛选行提供前进、后退、上一级按钮
  和面包屑路径框，搜索框固定 280px
- 面包屑路径可点击层级跳转；路径过长时自动用“...”收纳中间层级，单击路径框
  右侧空白区可切换为只读完整路径并选中复制
- 浏览归档内容，默认网格视图，可切换网格/列表/树形；支持过滤与排序；
  网格图标居中，长文件名自动换行显示，单元格高度按当前目录最长文件名动态调整
- 解压：全部或选中条目、覆盖策略（询问/覆盖/跳过/重命名）、密码；自动识别
  GBK/GB18030/Big5/Shift-JIS 等非 UTF-8 文件名编码，无需手动指定
- 压缩：7z / zip / tar / tar.gz / tar.bz2 / tar.xz / gz / bz2 / xz；系统安装 `rar` 时还可创建 RAR 及 RAR 分卷
- 读取 `.tar.zst` / `.tzst` / `.zst`（只读，使用内置 7-Zip）
- 读取 `.iso` 光盘镜像（ISO9660/Joliet/Rock Ridge 与 UDF，只读）：优先使用
  进程内 libarchive，UDF 镜像自动回退内置 7-Zip；文件关联与打开过滤器同步支持
- 追加与删除条目（7z / zip / tar 系）
- 压缩包内编辑并写回：双击用系统默认程序打开压缩包内文件，编辑保存后
  QArchiver 询问是否把更改存回压缩包；确认后就地替换原条目（支持旧式
  GBK/Big5/Shift-JIS 文件名），取消则压缩包保持不变
- 完整性测试，解压前可选自动测试
- 归档内单文件预览（文本/图片）；图片会连同同目录图片一起预解压，支持系统查看器连续看图。
  同目录图片超过可配置阈值（默认 200 张，0 表示任意批次）时显示可取消的准备对话框；
  该阈值只决定是否提示，不限制批量解压总量
- 常见文件类型图标：图片、音视频、PDF、Office、代码、字体、磁盘映像、电子书、数据库、种子、设计稿、字幕、3D/CAD、安装包等
- 文件选择使用系统原生对话框；压缩源选择先选择“文件”或“文件夹”，再调用系统对话框
- 图片缩略图（后台读取/解压并持久缓存）：压缩包条目与本地文件都支持；现代
  布局的网格/列表/树均显示，现代列表按行高等比缩放并限制宽度；经典布局只在
  网格视图显示，列表/树保持普通图片图标。双击仍可用系统查看器连续看图
- 拖出：把选中条目直接拖到文件管理器/桌面即完成解压并保留目录结构
  （后台解压到 staging 目录，`Esc` 可取消；同一选中项第二次拖动零延迟）

## 安全与可靠性

- Zip Slip 路径穿越防护；解压先落临时目录再移动，失败立即清理临时内容
- 解压移动阶段若只有部分文件成功，会明确提示“已移动文件未回滚”，不会静默当作完整成功
- 符号链接两层防护：解压前预检并询问“中止/跳过/本任务全部跳过”，移动前复查实际链接；
  目录链接被跳过时连同其子树一起跳过
- libarchive 读取时的硬链接会安全重建；目标缺失、越界或本身是符号链接时中止任务并提示，
  不会静默生成空文件
- 压缩替换原子化：旧压缩包/分卷先备份，新文件全部就位后才替换，失败自动回滚
- 压缩包列表输出流式解析，避免为列表额外保留完整命令行文本；大小累计带溢出检查
- 打开系统查看器或解压目标目录失败时会给出警告，不静默失败
- 外部编辑写回只对可写格式（zip / 7z / 未压缩 tar）生效，iso、rar、分卷与压缩
  tar 流不会出现保存提示；写回失败会明确报错且不会反复追问（见偏好设置中的
  “外部编辑后保存回压缩包前先询问”）

## 界面

默认布局参考好压：顶部大号彩色工具栏、文件夹面板、详细信息面板、
面包屑路径导航与包内搜索、归档状态摘要。现代布局使用全新扁平主题与彩色文件类型图标。

| 现代布局（默认） | 经典布局 |
| --- | --- |
| ![现代归档视图](docs/screenshots/modern-archive.png) | ![经典布局](docs/screenshots/classic-layout.png) |

- 现代 / 经典布局在“偏好设置 → 界面”中即时切换，无需重启；
  有压缩包操作进行时禁止切换，避免打断进度。
- 浅色与深色主题均适用于两种布局；中英文界面。深色主题预览：[modern-dark.png](docs/screenshots/modern-dark.png)。
- 经典布局保留原有 Frutiger Aero 皮肤、可选最近文件侧边栏、工具栏样式，
  以及半透明背景图片（九种对齐、自定义尺寸与透明度）。
- 现代布局不包含广告、捆绑安装或“安全扫描”弹窗。

## 一键解压

点击工具栏的“一键解压”（或起始页按钮，或命令行 `--oneclick`）后：

1. 若尚未打开压缩包，先选择一个压缩包；
2. 默认解压到压缩包所在目录下、以压缩包命名的独立文件夹；
   也可以在设置中改为自定义目录；
3. 目标文件夹已存在时自动生成 `名称 (1)`、`名称 (2)` 等唯一名称，
   不会静默覆盖已有文件；
4. 首次使用会询问解压完成后是否关闭程序；之后可在
   “偏好设置 → 解压 → 一键解压”中修改；
5. 解压成功且设置了“完成后关闭”时，窗口会自动关闭；失败或取消时保持打开。

## 7-Zip 性能测试

“偏好设置 → 工具 → 运行 7-Zip 性能测试...”或“工具 → 7-Zip 性能测试...”
会执行随程序分发的 `7z b`，测量压缩/解压吞吐和 CPU 频率。测试在后台进程运行，
可随时停止、复制输出，不会修改任何压缩包。

## 配置项

背景图片在“偏好设置 → 界面 → 背景图片（经典布局）”中选择；路径为空即关闭。
尺寸留空/0 表示“自动”（宽高只填一个时保持比例），透明度范围为 0–100%。

一键解压相关配置键：

- `extract/oneClickDestMode`：`archive`（压缩包同目录，默认）或 `custom`；
- `extract/oneClickDest`：自定义目标根目录；
- `extract/oneClickCloseAfter`：一键解压成功后是否关闭程序；
- `extract/oneClickCloseAsked`：是否已经显示过首次提示。

其他可配置项（偏好设置）：

- 界面布局：`ui/layoutMode`，`modern`（默认）或 `classic`；
- 内容视图：`ui/contentViewMode`，`grid`（默认）/ `tree` / `list`；
- 图标大小：`ui/itemSize`，默认 96，范围 48–128；经典列表/树固定使用紧凑尺寸；
- 连续看图准备对话框阈值：`preview/continuousImageDialogThreshold`，默认 200，范围 0–9999；
- 最近文件数量上限：`history/recentFilesLimit`，默认 20，范围 1–100，降低时立即裁剪并刷新侧边栏。

## 依赖

构建期（Debian/Ubuntu 包名，Qt 6.4 起可用）：

| 依赖 | 是否必需 | 说明 |
| --- | --- | --- |
| Qt 6.4+（Core Gui Widgets Concurrent） | 必需 | `qt6-base-dev (>= 6.4)` |
| Qt LinguistTools（lupdate/lrelease） | 必需 | `qt6-tools-dev (>= 6.4)`，翻译在构建时编译 |
| CMake 3.20+、GCC 12+/Clang 15+（C++20） | 必需 | |
| libarchive 3.4+（开发头文件） | 可选（默认启用） | `libarchive-dev`；缺失时仅 WARNING，自动退回纯命令行后端 |
| MinGW-w64（可选） | 可选 | 交叉编译 Windows 版本；Windows 原生构建也可用 MSVC 2022 |

运行期：

| 依赖 | 是否必需 | 说明 |
| --- | --- | --- |
| 7-Zip CLI | 已内置 | `third_party/7zip/`（Linux `7zzs`，Windows `7z.exe + 7z.dll`）随程序分发；负责 7z/zip/tar 读写、纯流解压和 RAR 读取 |
| rar 命令 | 可选 | 系统安装后才会启用 RAR 创建与 RAR 分卷创建；可用 `QARCHIVER_RAR_PATH` 指定路径 |
| libarchive13 | 启用 libarchive 时必需 | 程序加载的是构建时探测到的共享库 |
| qt6-translations-l10n | 可选 | 提供 `qtbase_<lang>.qm`，用于本地化标准对话框按钮与文件对话框；缺失时应用自身文本仍会翻译 |

构建时会把当前架构的 7-Zip 辅助程序复制到可执行文件旁的 `7zip/`
目录（Linux 安装到 `libexec/qarchiver/7zip/`）。运行时优先使用这个内置
版本，只有找不到时才回退到系统 PATH 中的 `7z` / `7zzs`。

启动时会检查兜底工具链：

1. 先在程序相关目录（`<可执行文件目录>/7zip`、`libexec/qarchiver/7zip` 等）
   查找 7-Zip 命令行工具，并用 `--help` 确认它确实是 7-Zip；
2. 找不到内置工具时，再查找系统 PATH 中的 7-Zip，并通过 `7z i` 输出的
   `Codecs:` 区段判断是否支持 RAR 解压；
3. 如果正在使用系统 7-Zip，启动后会弹窗一次，显示所用路径及 RAR 支持情况；
   如果完全找不到，会提示下载官方 7-Zip 命令行工具放到 `7zip/` 目录，或
   在 Debian/Ubuntu 上安装系统包（含 `7zip-rar` / `p7zip-rar`）。
   探测在后台线程执行，不阻塞窗口显示。

`QARCHIVER_7Z_PATH` 可显式指定 7-Zip 可执行文件，绕过上述自动探测。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
sudo cmake --install build            # 可选，默认装到 /usr/local
```

若安装后启动器没有立即显示图标，刷新桌面缓存（把 `PREFIX` 换成实际安装前缀，
例如 `/usr/local` 或 `/usr`）：

```bash
sudo update-desktop-database PREFIX/share/applications
sudo gtk-update-icon-cache -f PREFIX/share/icons/hicolor
```

图标已安装 32/48/64/128/256/512 多尺寸，并设置 `StartupWMClass`；使用 `.deb`
安装时由 Debian 的图标缓存触发器自动刷新。

Debian/Ubuntu 可直接用仓库内的 `debian/control` 安装构建依赖：

```bash
mk-build-deps --install --remove -t 'apt-get -y' debian/control
# 或者先构建依赖包，再用 aptss/apt 安装
mk-build-deps -t 'apt-get -y' --build-dep debian/control
```

打包：

```bash
dpkg-buildpackage -us -uc -b
```

常用选项：

| 选项 | 默认 | 说明 |
| --- | --- | --- |
| `-DQARCHIVER_USE_LIBARCHIVE=ON|OFF` | `ON` | 关闭后只编译命令行后端，不链接 libarchive |
| `-DCMAKE_BUILD_TYPE=Release|Debug|RelWithDebInfo` | 空 | |
| `-DBUILD_TESTING=OFF` | `ON` | 是否生成 `tests/` |

若 libarchive 只提供 pkg-config 文件而没有 CMake 配置包，构建脚本会自动
回退到 `pkg_check_modules`。

## 源码结构

源码按层组织，每层一个静态库，依赖只能自上而下
（`ui → core → backends → utils → common`）：

```text
src/
├── app/        命令行解析与翻译安装
├── common/     格式后缀表、错误与基础数据结构（不依赖 Widgets）
├── utils/      子进程、路径、编码、7-Zip 定位、日志
├── backends/   tar / sevenzip / rar / stream / libarchive
├── core/       ArchiveManager、后端工厂、解压预检、压缩输出、设置
└── ui/         MainWindow（按职责拆分）、模型、服务、主题、图标、对话框
resources/      图标、主题字形与样式表（qrc 编译进可执行文件）
cmake/          7-Zip 打包、libarchive 探测、Windows 资源、交叉编译工具链
packaging/      Windows .rc.in / manifest 与资源管理器右键菜单脚本
tests/          Qt Test 用例（统一由 qarchiver_add_test() 注册）
tools/          图标生成、qrc 生成与资源校验脚本
docs/           设计文档（design.zh_CN.md）与截图
```

完整的模块职责、接口与流程见 [`docs/design.zh_CN.md`](docs/design.zh_CN.md)。

图标与样式表位于仓库根目录的 `resources/`，通过 `resources/CMakeLists.txt`
中的 `icons.qrc`、`themes-aero.qrc`、`themes-modern.qrc` 编译进可执行文件，
运行时不需要额外的资源目录。修改图形后重新运行
`python3 tools/update_qrc.py`，再用 `python3 tools/check_resources.py` 自检：

```bash
python3 tools/gen_aero_assets.py        # 经典主题字形（PyQt6）
python3 tools/gen_modern_icons.py       # 现代主题图标（需要 rsvg-convert）
python3 tools/normalize_classic_icons.py
python3 tools/update_qrc.py
python3 tools/check_resources.py
```

## 读取后端与开关

读取有两条路径：

- **libarchive（默认）**：zip（含 ZipCrypto/AES）、明文 7z、tar / tar.gz /
  tar.bz2 / tar.xz、单文件 gz / bz2 / xz 在进程内解析，不启动外部程序。
  无法处理的情形（加密 7z、7z/zip 分卷、rar）会自动回退到命令行后端，
  并把待执行的操作重放一次。
- **命令行**：7z/zip/tar/gz/bz2/xz 由内置 7-Zip 读写，RAR 读取由内置 7-Zip 完成，
  不再依赖系统 `tar` / `unrar`。若系统安装了 `rar`，RAR 创建与分卷创建会使用该命令。
  7-Zip 调用统一使用 `-sccUTF-8`、`--` 和 `-spd`，选中成员通过 UTF-8 或原始代码页
  listfile 传入，避免通配符、`@`、前导 `-` 和旧 ZIP 编码问题。

写入（压缩、追加、删除、分卷）始终由命令行完成，libarchive 后端只读。
对 zip / 7z / 未压缩 tar 执行“添加/删除”时，如果当前是只读的 libarchive 后端，
程序会自动用命令行后端重新打开同一压缩包并重放操作，因此界面上仍可直接使用
“添加/删除”；tar.gz/tar.xz/tar.bz2、RAR 与分卷格式仍不支持追加/删除。

两级开关，均可关闭 libarchive：

1. 设置 → 偏好设置 → “使用 libarchive 读取压缩包”
   （配置键 `backends/useLibArchive`，默认开启）。**更改在下次打开压缩包时生效**，
   不会打断当前已打开归档的会话。
2. 环境变量 `QARCHIVER_NO_LIBARCHIVE=1`：启动即禁用，优先级高于配置项，
   适合对比排查问题。

## Windows

Windows 构建、ZIP 打包、资源管理器右键菜单与交叉编译说明见
[`packaging/windows/README.md`](packaging/windows/README.md)。要点：

- 原生 exe 图标、版本资源、Per-Monitor V2 DPI 清单；
- Windows x64 与 ARM64；ARM64 包使用原生 ARM64 的 7-Zip 辅助程序；
- 通过 Win32 API 转换 GBK/Big5/Shift-JIS 文件名，不依赖 iconv；
- 启动 7z/rar 时隐藏控制台窗口；
- `cmake --install` 使用 `windeployqt` 部署 Qt 运行库，`cpack` 生成便携 ZIP；
  `packaging/windows/QArchiver.iss` 在便携包基础上生成安装器；
- 安装器与 `packaging/windows/install-shell-menu.ps1` 注册资源管理器菜单：
  “用 小Q解压 打开”“一键解压”“添加到同名 ZIP 压缩包”以及
  “压缩到...”二级菜单（7z/ZIP/tar.gz/tar.xz/tar.bz2/tar）；
- Debian/Ubuntu 上可用 `tools/build-windows-cross.sh` 交叉编译并整理便携目录；
- GitHub Actions 在 `windows-2022` 上构建 x64，并用
  `win64_msvc2022_arm64_cross_compiled` 交叉构建 ARM64。

## 命令行

```bash
QArchiver [archive]                 # 打开压缩包
QArchiver --oneclick <archive>      # 一键解压
QArchiver --compress zip <files>    # 直接创建同名 ZIP（右键菜单使用）
QArchiver --compress 7z <files>     # 直接创建 7z
QArchiver --compress tar.gz <dir>   # 直接创建 tar.gz
QArchiver --output <path>           # 配合 --compress 指定目标
QArchiver --compress-dialog <files> # 打开压缩对话框
QArchiver --preferences             # 打开设置
QArchiver --benchmark               # 打开 7-Zip 性能测试
QArchiver --lang en                 # 指定界面语言
```

## 测试与 CI

GitHub Actions：

- `.github/workflows/build.yml`：可复用构建矩阵，Linux amd64/arm64 在
  Debian 12（bookworm，最老支持基线）容器中构建并生成 deb，再在干净的
  Debian 12 / Debian 13 / Ubuntu 24.04 容器中验证依赖可满足性与安装
  （动态库依赖、内置 7-Zip 与桌面入口），Windows x64 原生构建 + 测试，
  Windows ARM64 交叉构建；
- `.github/workflows/ci.yml`：push / pull request 自动构建测试；
- `.github/workflows/release.yml`：推送 `v*` 标签后构建四种产物、生成
  `SHA256SUMS` 并创建 GitHub Release。

本地测试：

```bash
cd build
ctest --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --output-on-failure   # 无显示环境
```

测试覆盖：工具函数（路径规范化/穿越检测/大小溢出/符号链接目标）、CLI 后端、
管理器（分派、取消、回退、打开失败保留当前包、危险符号链接决策）、
libarchive 后端（往返、AES 密码、损坏检测、选择性解压、硬链接重建）、
偏好对话框与配置同步（连续看图阈值、最近文件上限）、RAR/RAR 分卷创建与
旧分卷替换、兜底 7-Zip 的 `--help` 校验与 RAR Codecs 探测、图片同目录
连续预览、列表模式目录索引、背景图片缓存、沉浸式标题栏（布局切换后保留、
菜单右侧空白可拖拽、主题 token 配色）、系统位置侧栏（路径去重、子目录高亮、
文件系统回退）。测试通过
`QARCHIVER_7Z_PATH` 使用内置
7-Zip；RAR 创建用例在系统没有 `rar` 时会自动跳过，也可用
`QARCHIVER_RAR_PATH` 指定待测的 rar 可执行文件。
`tarDotPrefixAndNonAscii` 兼容性用例在系统没有 `tar` 时会自动跳过；
依赖 Unix 符号链接的用例在 Windows 上会自动跳过。测试还覆盖了
“跳过危险符号链接时不得解出该链接”的 tar/7z 成员过滤回归。

## 调试与诊断

| 环境变量 | 作用 |
| --- | --- |
| `SAM_DEBUG=1` | 输出 Debug 级日志 |
| `QARCHIVER_NO_LIBARCHIVE=1` | 启动即禁用 libarchive 后端 |
| `QARCHIVER_7Z_PATH` | 显式指定 7-Zip 命令行工具路径，跳过自动探测 |
| `QARCHIVER_RAR_PATH` | 显式指定 rar 命令路径，用于 RAR 创建 |
| `QARCHIVER_DRAG_DEBUG_NO_EXEC=1` | 拖出时只准备 staging 并打印 URL，不进入阻塞的原生拖拽循环 |

拖放相关的排查工具（X11 下可自动复现/对照的拖拽源与日志化接收端）见
`~/.app/ai_tools/dnd-drag-test/`；拖出失效时先看“目标端有没有来请求数据”，
比看 `QDrag::exec()` 的返回值可靠。

## 已知限制

- 加密 7z 无法用 libarchive 读取，会回退到 7z 命令行（仍需密码）
- libarchive 不支持 7z/zip 分卷；分卷归档直接在打开时走命令行
- RAR 读取由内置 7-Zip 完成；创建 RAR/RAR 分卷需要系统安装 `rar`，
  已创建 RAR 的追加/删除仍不支持
- 危险符号链接默认中止解压；用户选择“跳过”时该链接（目录链接则连同子树）不会解压
- 硬链接仅在 libarchive 读后端中安全重建；目标缺失/越界/为符号链接时任务会中止并提示
- 归档注释的显示/编辑不再提供
- Windows 上由 libarchive 解压符号链接需要开发者模式或管理员权限；
  命令行 7-Zip 后端通常会直接跟随/跳过链接
- 一键解压默认不弹目录选择框；需要每次选择目录时请使用“解压到...”

## 许可证

AGPL-3.0，见 `LICENSE`。程序运行期调用外部工具、并与 libarchive（BSD-2-Clause）
链接；内置的 7-Zip 按 GNU LGPL（含 unRAR 限制）分发，许可证见
`third_party/7zip/License.txt`，安装后位于 `share/doc/qarchiver/`（Debian
包内为 `share/doc/yincolor.qarchiver/`）。
程序不再依赖非自由的 `unrar`；RAR 读取由内置 7-Zip 完成。
RAR 创建是可选的，仅在用户系统已安装 `rar` 命令时启用，程序本身不分发该命令。

## 变更记录

见 [`CHANGELOG.md`](CHANGELOG.md)。

## 打包参考

见 `packaging/README.md` 与 `packaging/windows/README.md`。
