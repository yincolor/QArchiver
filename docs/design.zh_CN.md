# QArchiver（小Q解压）设计文档

> 适用版本：2.4.1
> 文档位置：`docs/design.zh_CN.md`
> 技术栈：C++20 + Qt 6.4+ Widgets
> 平台：Linux / Windows
> 外部工具：内置 7-Zip（Linux `7zzs` / Windows x64/ARM64 `7z.exe + 7z.dll`，负责 7z/zip/tar 等读写、纯流解压、RAR/zstd 读取；zstd 仅只读）；系统 `rar` 可选，用于创建 RAR/RAR 分卷

## 1. 项目概述

QArchiver 是一个基于 Qt 6 的压缩/解压工具，核心思路是：

- 用统一的 `ArchiveBackend` 接口封装不同格式。
- 通过外部命令行工具完成实际的压缩/解压。
- UI 只负责交互和展示，不在 GUI 线程执行阻塞操作。
- 统一处理密码、进度、取消、路径安全、编码和错误映射。

主要功能：

- 现代好压（HaoZip）风格默认界面：扁平渐变头部、大号彩色工具栏、文件夹面板、
  详细信息面板、路径导航、包内搜索与归档状态摘要；可在偏好设置中即时切换回经典布局。
- 起始页：现代布局显示拖放区、最近打开、打开/一键解压入口和“文件管理”按钮，
  启动默认停留在欢迎页。经典布局没有欢迎页与“文件管理”按钮，没有打开压缩包时
  启动、从现代布局切换过来或选择“文件 → 起始页”都会直接进入本地目录浏览；
  命令行启动时已带压缩包/外壳任务则不预先扫描本地目录。本地浏览（Windows/Linux）
  在现代布局显示系统位置侧栏（主目录、桌面、文档、下载、图片、音乐、视频、
  文件系统及用户挂载卷），经典布局不显示该侧栏；支持导航、搜索、网格/列表/树形
  视图、图片缩略图与右键压缩。
- 排序：右键空白处或视图旁的排序按钮可按键名/大小/类型/修改时间升降序排列，
  设置持久化（`ui/sortColumn`、`ui/sortOrder`），网格视图同样生效。
- 一键解压：自动解压到以压缩包命名的独立文件夹，冲突目录自动改名；
  首次提示并可设置解压完成后关闭程序。
- 7-Zip 性能测试（`7z b`）：实时输出、线程数、停止与复制结果。
- Windows 支持：x64/ARM64 的 MinGW/MSVC 构建、图标/版本资源、DPI 清单、
  Win32 编码转换、隐藏控制台、windeployqt 部署、CPack ZIP、Inno Setup
  安装器（开始菜单/桌面快捷方式/卸载入口）与资源管理器右键菜单
  （打开、一键解压、添加到同名 ZIP、压缩到 7z/ZIP/tar 等）；ARM64 包内置
  原生 ARM64 的 7-Zip。
- 打开、浏览、预览压缩包。
- 解压全部或选中条目。
- 创建 zip、7z、tar、tar.gz、tar.xz、tar.bz2 以及单文件 gz/bz2/xz 压缩流；
  系统安装 `rar` 时可创建 RAR 及 RAR 分卷。
- 读取 tar.zst / tzst / 单文件 zst（内置 7-Zip 只读支持；创建 zstd 暂不支持）。
- 向 zip/7z 或未压缩 tar 追加文件。
- 从 zip/7z 或未压缩 tar 删除条目。
- 完整性测试。
- 网格/列表/树形三种内容视图，默认网格。
- 分卷压缩包识别与解压。
- 最近文件、主题、工具栏样式、中文/英文界面。

## 2. 技术栈与构建

### 2.1 依赖

| 类别 | 依赖 |
|---|---|
| 语言/框架 | C++20、Qt 6.4+（设计目标 6.11.2） |
| Qt 模块 | Core、Gui、Widgets、Concurrent |
| 可选模块 | LinguistTools（翻译）、Test（测试） |
| 库依赖 | libarchive ≥ 3.4（读路径，构建期 `libarchive-dev`，运行期 `libarchive13`） |
| 外部命令 | 内置 7-Zip（`third_party/7zip/`，随程序分发；7z/zip/tar/纯流读写、RAR 读取）；系统 `rar` 可选（创建 RAR/分卷） |
| Windows 工具链 | 可选：MinGW-w64（Debian 交叉编译）或 MSVC 2022；Qt 6.8 MinGW/MSVC 包（含 `win64_msvc2022_arm64_cross_compiled`） |

7-Zip 辅助程序由 `cmake/QArchiverBundled7zip.cmake` 按目标平台/架构选择，构建后复制到 `<可执行文件目录>/7zip/`；Linux 安装到 `libexec/qarchiver/7zip/`，Windows 安装到 `bindir/7zip/`。运行时 `ProcessRunner::resolveToolPath()` 优先使用内置版本，找不到才回退系统 PATH。Windows 必须同时分发 `7z.exe` 和同版本 `7z.dll`；Windows ARM64 使用 `third_party/7zip/win/arm64/` 的原生二进制，而不是 x64 模拟。

### 2.2 工具链探测与启动检查

`SevenZipTool` 在首次需要 7-Zip 时探测并缓存结果：

1. `QARCHIVER_7Z_PATH` 显式覆盖（测试/打包用途）优先；
2. 按 `ProcessRunner::bundledToolPaths("7z")` 的顺序检查程序相关目录，
   对每个候选执行 `--help`，输出包含 `7-Zip` 或 `Usage: 7z...` 才接受；
3. 否则检查系统 PATH 候选，同样校验 `--help`；
4. 系统 7-Zip 执行 `7z i`，只解析 `Codecs:` 区段中的 `Rar` 关键词，
   判断是否支持 RAR 解压。

`MainWindow` 在窗口显示后（`QTimer::singleShot(0, ...)`）检查一次：

- 使用程序自带 7-Zip：不提示；
- 回退到系统 7-Zip：弹窗显示系统路径，并说明 RAR 解压是否受支持；
- 完全找不到：弹窗提示下载官方 7-Zip 命令行工具放到 `<可执行文件目录>/7zip/`，
  或通过 Debian/Ubuntu 的 `apt` 安装 `7zip` 与 `7zip-rar`（旧版 `p7zip-full` /
  `p7zip-rar`）。

弹窗每次启动最多出现一次；探测在后台线程执行，不阻塞窗口创建。

### 2.3 可选读后端与构建开关

libarchive 是可选的：找不到时 CMake 会告警并退回“纯命令行”构建（定义 `QARCHIVER_USE_LIBARCHIVE=OFF` 可显式关闭）。运行期有两级开关：偏好设置中的“使用 libarchive 读取压缩包”（`backends/useLibArchive`，默认开启），以及排查用的环境变量 `QARCHIVER_NO_LIBARCHIVE=1`（优先于设置项，强制全部走命令行后端；设置此变量时偏好项会显示为禁用并置灰）。

Qt 搜索路径：默认使用系统安装的 Qt 6（最低 6.4）。若需指定其他 Qt，
可在配置时传入 `-DCMAKE_PREFIX_PATH=/path/to/qt6`，例如：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/lib/qt6.11
```

### 2.4 目录结构与 CMake 目标

源码按"层"组织，每一层是一个静态库，依赖方向只能单向向下，避免历史遗留的
`core → backends → utils → core` 循环依赖：

```text
src/
├── CMakeLists.txt
├── main.cpp                    仅启动：命令行 → 翻译 → 主窗口
├── app/                        命令行解析与翻译安装
│   ├── CommandLine.{h,cpp}
│   └── Translations.{h,cpp}
├── common/                     无 Widgets 依赖的共享层
│   ├── ArchiveConstants.h  ArchiveEntry.h  ArchiveOptions.h
│   ├── ArchiveError.{h,cpp}  FormatRegistry.{h,cpp}
├── utils/                      ProcessRunner、FileUtils、EncodingDetector、
│                               SevenZipTool、MimeTypeHelper、Logger、
│                               FileAssociations、MimeAppsList
├── backends/                   ArchiveBackend 及各格式实现
│   ├── tar/  sevenzip/  rar/  stream/  libarchive/
├── core/                       ArchiveManager 与操作编排
│   ├── ArchiveManager*.{h,cpp}  ArchiveBackendFactory  ExtractionPreflight
│   ├── CompressOutput  AppSettings  RecentFilesManager
└── ui/                         全部 Qt Widgets 代码
    ├── MainWindow*.{h,cpp}     按职责拆分的多个实现文件
    ├── models/  services/  theme/  icons/  background/
    ├── dialogs/  widgets/
```

CMake 目标与依赖：

| 目标 | 内容 | 公开依赖 |
|---|---|---|
| `qarchiver_common` | `common/` | Qt6::Core |
| `qarchiver_utils` | `utils/` | `qarchiver_common` |
| `qarchiver_backends` | `backends/`（libarchive 后端按开关编译） | `qarchiver_utils` |
| `qarchiver_core` | `core/` | `qarchiver_common`、`qarchiver_backends` |
| `qarchiver_ui` | `ui/` | `qarchiver_core`、Qt6::Widgets、Qt6::Gui |
| `QArchiver` | `main.cpp` + `app/` | `qarchiver_ui` |

约定：

- 依赖只能自上而下：`ui → core → backends → utils → common`，不允许反向引用。
- 唯一 include 根是 `src/`，头文件一律写 `#include "core/ArchiveManager.h"`；
  全部源文件名唯一，查找与排序不依赖目录。
- 所有静态库 `POSITION_INDEPENDENT_CODE ON`，警告规则由
  `qarchiver_set_warnings()` 统一施加。
- `QARCHIVER_HAS_LIBARCHIVE` 只由 `qarchiver_backends` 公开导出，上层无需再判断
  CMake 变量。
- 图标/主题资源清单挂在可执行目标上（见第 12 节）。
- libarchive 探测集中在 `cmake/QArchiverLibArchive.cmake`，由根 `CMakeLists.txt`
  在 `add_subdirectory(src)` 之前调用，结果是 `QARCHIVER_HAS_LIBARCHIVE`、
  `QARCHIVER_LIBARCHIVE_TARGET` 与导入目标 `LibArchive::LibArchive`。
- 内置 7-Zip 由 `cmake/QArchiverBundled7zip.cmake` 选择并安装；Windows 图标、
  版本资源与部署由 `cmake/QArchiverWindows.cmake` 负责。

仓库根目录其他内容：

```text
resources/              图标、主题字形、样式表与 qrc 清单（见第 12 节）
cmake/                  7-Zip 打包、libarchive 探测、Windows 资源与交叉编译工具链
packaging/windows/      Windows .rc.in / manifest / 资源管理器右键菜单脚本
debian/                 Debian 打包（control、rules、changelog、copyright）
tests/                  Qt Test 用例，统一由 tests/CMakeLists.txt 的
                        qarchiver_add_test() 注册
tools/                  图标生成、qrc 生成与资源校验脚本
translations/           zh_CN.ts / en.ts
docs/                   设计文档与截图
third_party/7zip/       按平台/架构存放的内置 7-Zip（不改动）
tmp/                    本地临时目录（已 gitignore）
```

### 2.5 构建

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Debian/Ubuntu 构建依赖：

```bash
mk-build-deps --install --remove -t 'apt-get -y' debian/control
dpkg-buildpackage -us -uc -b
```

Windows x64 原生构建（MSVC 2022）：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_PREFIX_PATH=C:\Qt\6.8.2\msvc2022_64 ^
      -DQARCHIVER_USE_LIBARCHIVE=OFF
cmake --build build --config Release
cpack --config build/CPackConfig.cmake -C Release
```

Windows ARM64 在 x64 主机上交叉构建：

```powershell
cmake -S . -B build-arm64 -G "Visual Studio 17 2022" -A ARM64 ^
      -DCMAKE_PREFIX_PATH=C:\Qt\6.8.2\msvc2022_arm64_cross_compiled ^
      -DQARCHIVER_USE_LIBARCHIVE=OFF -DBUILD_TESTING=OFF
cmake --build build-arm64 --config Release
cpack --config build-arm64/CPackConfig.cmake -C Release
```

Linux 交叉编译 Windows x64（MinGW-w64 + 目标 Qt MinGW 包 + 主机 Qt）：

```bash
QT_WIN=/path/to/6.8.2/mingw_64 QT_HOST=debian \
    tools/build-windows-cross.sh
```

交叉构建关闭 `QARCHIVER_WINDOWS_DEPLOY`（`windeployqt` 是 Windows 程序），
由脚本手动整理 Qt DLL、插件与 MinGW 运行库；CI 使用 MSVC 时由
`qt_generate_deploy_app_script()` 自动部署。

### 2.6 CI 与发布

- `.github/workflows/build.yml`：可复用矩阵。
  - `ubuntu-24.04` / `ubuntu-24.04-arm` 上运行 Debian 12（bookworm）容器，
    构建并生成 amd64/arm64 deb（Qt 6.4 + glibc 2.36 + t64 改名前的库名，
    是支持的最老基线），再在干净的 Debian 12 / Debian 13 / Ubuntu 24.04
    容器中验证依赖可满足性与安装（动态库依赖、内置 7-Zip 辅助程序与桌面入口）；
  - `windows-latest` 用 MSVC 2022 构建 x64 并运行测试；
  - `windows-latest` 加 `-A ARM64` + Qt `win64_msvc2022_arm64_cross_compiled`
    交叉构建 arm64 ZIP（不运行测试）。
- `.github/workflows/ci.yml`：在 push 与 pull request 上调用 build.yml。
- `.github/workflows/release.yml`：推送 `v*` 标签时调用 build.yml，下载四个
  产物，生成 `SHA256SUMS`，用 `softprops/action-gh-release` 创建 Release。
- Windows 架构由 `QARCHIVER_WINDOWS_ARCH` 统一：`cmake/QArchiverBundled7zip.cmake`
  据此选择 x64/arm64 的 7-Zip，CPack 文件名也使用该变量。

## 3. 总体架构

### 3.1 运行时结构

```text
┌─────────────────────────────────────────────┐
│ GUI 线程                                     │
│ MainWindow / 对话框 / FileListModel          │
└───────────────┬─────────────────────────────┘
                │ signals / slots
┌───────────────▼─────────────────────────────┐
│ ArchiveManager（单例，GUI 线程）              │
│ - 格式检测调度                               │
│ - 操作串行化                                 │
│ - 密码重试、取消、临时压缩包管理              │
└───────────────┬─────────────────────────────┘
                │ QMetaObject::invokeMethod(Queued)
┌───────────────▼─────────────────────────────┐
│ 专用 QThread（所有阻塞归档操作）              │
│ ArchiveBackend                               │
│   ├─ LibArchiveBackend（读优先）             │
│   ├─ TarBackend                              │
│   ├─ SevenZipBackend（含 zip）               │
│   ├─ RarBackend                              │
│   └─ StreamBackend                           │
│        │                                     │
│        ├─ libarchive（进程内）               │
│        └─ ProcessRunner → 内置 7-Zip         │
└─────────────────────────────────────────────┘

QtConcurrent：MimeTypeHelper 格式检测、PreviewManager 预览解压
```

关键约定：

- 后端对象创建后 `moveToThread(&m_workerThread)`。
- 同一时间只执行一个归档操作；`ArchiveManager::m_busy` 控制。
- 后端通过信号把进度、错误、完成结果发回 GUI 线程。
- 取消通过 `std::atomic_bool` 协作完成，不直接终止线程。
- 格式检测不阻塞 GUI；检测期间显示加载提示。

### 3.2 源码分层

底层不感知上层，依赖只能自上而下：

```text
ui/ ─────────────► core/ ─────────► backends/ ─────► utils/ ─────► common/
MainWindow*        ArchiveManager   ArchiveBackend   ProcessRunner   FormatRegistry
对话框/模型/委托    Factory/Preflight  Tar/7-Zip/RAR   FileUtils       ArchiveError
                   CompressOutput    Stream/libarchive Encoding       ArchiveEntry
                   AppSettings                                          Options
```

各层职责：

| 层 | 只负责 | 明确不做 |
|---|---|---|
| `common/` | 数据结构、格式后缀真值表、错误码 | 不含任何 Qt Widgets 头文件 |
| `utils/` | 子进程、路径、编码、7-Zip 定位、日志 | 不创建后端、不弹窗 |
| `backends/` | 单个格式的读写与命令拼装 | 不持有 UI 状态，不决定用户交互 |
| `core/` | 编排：选后端、串行化、重试、临时目录 | 不直接绘制界面 |
| `ui/` | 交互与展示 | 不拼命令行、不做格式判断 |
| `app/` | 启动参数与翻译 | 不承载业务逻辑 |

### 3.3 `ArchiveManager` 与 `MainWindow` 的拆分

两者原本是 1400/2700 行的巨型实现，现按职责拆分，`ArchiveManager` 对外接口不变：

`core/`：

| 文件 | 职责 |
|---|---|
| `ArchiveManager.{h,cpp}` | 对外的薄封装：打开/解压/压缩/追加/删除/测试入口 |
| `ArchiveManagerDispatch.cpp` | 后端创建、信号连接、终态处理、libarchive 回退、错误映射 |
| `ArchiveBackendFactory.{h,cpp}` | 按格式和环境选择后端（含 libarchive 开关判断） |
| `ExtractionPreflight.{h,cpp}` | 解压前检查：路径逃逸、符号链接决策、磁盘序号 |
| `CompressOutput.{h,cpp}` | 输出路径命名、覆盖策略、临时文件提升与清理 |

`ui/`：

| 文件 | 职责 |
|---|---|
| `MainWindow.cpp` | 构造/析构、整体布局装配、页面切换、后端信号处理、事件 |
| `MainWindowActions.cpp` | QAction/工具栏/菜单的创建与状态更新 |
| `MainWindowLayout.cpp` | 现代与经典布局的控件树构建 |
| `MainWindowNavigation.cpp` | 路径导航、历史、搜索、视图模式 |
| `MainWindowExtraction.cpp` | 解压/压缩/一键解压的交互流程 |
| `MainWindowDragOut.cpp` | 拖出（staging）与 `LoggingMimeData` |
| `MainWindowHelpers.{h,cpp}` | 文件对话框过滤器、体积格式化等纯函数 |

拆分原则：成员变量仍集中在 `MainWindow.h`，实现文件只包含自己需要的头文件；
公共小工具放 `MainWindowHelpers`，不放匿名命名空间以外的静态副本。

## 4. 核心数据结构

### 4.1 格式与条目

`ArchiveFormat`：

```text
Unknown, Tar, TarGz, TarXz, TarBz2,
Zip, SevenZip, Rar4, Rar5, Gzip, Bzip2, Xz,
TarZst, Zstd  // 新增只读格式追加在末尾，保持持久化整数值稳定
```

`ArchiveEntry`：

| 字段 | 含义 |
|---|---|
| `path` | 压缩包内部原始路径 |
| `size` | 解压后大小 |
| `packedSize` | 压缩后大小 |
| `mtime` | 修改时间 |
| `isDir` / `isEncrypted` / `isSymLink` | 条目属性 |
| `linkTarget` | 符号链接目标（列表后端解析） |
| `isHardLink` / `hardLinkTarget` | 硬链接标记与目标路径 |

### 4.2 选项结构

| 结构 | 主要字段 |
|---|---|
| `ListOptions` | `password`, `encodingHint` |
| `TestOptions` | `password` |
| `ExtractOptions` | `destDir`, `selectedFiles`, `preservePaths`, `overwrite`, `password`, `encodingHint`, `openDestAfterFinish`, `testBeforeExtract`, `skippedSymlinks`, `symlinkPreflightDone` |
| `CompressOptions` | `archivePath`, `sourceFiles`, `compressionLevel`, `password`, `encryptHeaders`, `volumeSize`, `solidMode`, `excludePatterns`, `followSymlinks`, `storeFullPath` |
| `AddFilesOptions` | `archivePath`, `sourceFiles`, `password`, `compressionLevel`, `followSymlinks`, `storeFullPath` |
| `DeleteFilesOptions` | `archivePath`, `targetEntries`, `password` |

`OverwritePolicy`：

- `Ask`：由 UI 询问，实际执行为“全部覆盖”或“全部跳过”。
- `Always`：覆盖。
- `Never`：跳过已存在文件。
- `Rename`：自动生成 `name (1).ext` 形式的新文件名。

### 4.3 错误体系

`ArchiveErrorCode` 覆盖：

```text
Unknown, OperationNotSupported, UnsupportedFormat,
PasswordRequired, WrongPassword, CorruptedArchive,
InsufficientDiskSpace, PathTraversalBlocked, VolumeMissing,
ToolNotFound, FileNotFound, PermissionDenied, PathTooLong,
EncodingError, PartialExtraction, HardLinkUnsupported, Cancelled
```

`ArchiveException` 及其子类携带错误码和消息。后端通常通过 `errorOccurred(code, msg)` 报告错误，由 `ArchiveManager` 统一转发给 UI。

## 5. 核心接口

### 5.1 `ArchiveBackend`

所有后端实现同一接口：

```cpp
openArchive(path)
listContents(ListOptions)
extract(ExtractOptions)
compress(CompressOptions)
addFiles(AddFilesOptions)
deleteFiles(DeleteFilesOptions)
testArchive(TestOptions)
closeArchive()
```

能力查询：

```cpp
format()
isReadOnly()
supportsEncryption()
supportsVolume()
supportsAppend()
```

信号：

```text
archiveOpened, fileListReady, progressChanged, outputReady,
passwordRequested, errorOccurred, operationFinished
```

取消：

- `requestCancel()` 设置原子标志。
- `clearCancel()` 在每次新操作前清除。
- 后端把标志传给 `ProcessRunner`，由运行器轮询并终止子进程。

### 5.2 `ArchiveManager`

职责：

- 打开/关闭压缩包。
- 异步格式检测。
- 创建后端并移动到工作线程。
- 串行化操作，防止多个后端操作并发。
- 缓存条目列表，用于解压前路径/磁盘空间检查。
- 密码重试（最多 3 次）。
- 取消当前操作或待重试操作。
- 压缩时管理临时目标，成功后在备份旧文件集的前提下原子替换，失败时回滚。
- 打开新归档时先保留旧归档；新后端成功 `archiveOpened` 后才销毁旧后端，检测/打开失败不影响当前归档。
- 待重试状态只在操作派发成功后建立，派发失败立即清除。
- 解压前通过 UI 回调处理危险符号链接（中止 / 跳过 / 本任务全部跳过）。
- 维护 generation，忽略已关闭归档的迟到信号。

实现拆分（对外接口不变）：

| 文件 | 职责 |
|---|---|
| `core/ArchiveManager.cpp` | 状态机与对外接口的薄封装：校验参数、派发操作、维护 generation |
| `core/ArchiveManagerDispatch.cpp` | 后端创建与信号连接、终态处理、libarchive 回退重放、错误映射 |
| `core/ArchiveBackendFactory.cpp` | 格式 → 后端的选择规则（含 libarchive 可用性与开关判断） |
| `core/ExtractionPreflight.cpp` | 解压前的路径逃逸、符号链接决策与磁盘检查 |
| `core/CompressOutput.cpp` | 输出命名、覆盖策略、临时压缩包的提升与清理 |

`ArchiveManager` 不再自己判断后端类型，一律通过 `ArchiveBackendFactory`；
`SymlinkDecision` 等类型在 `ExtractionPreflight.h` 定义，`ArchiveManager.h` 以别名导出。

主要信号：

```text
archiveOpening(filePath)     // 检测开始，UI 显示加载状态
archiveOpened(filePath)
fileListReady(entries)
progressChanged(...)
outputReady(line)
passwordRequested()
errorOccurred(code, msg)
operationFinished(success, msg)
```

后端工厂：

| 格式 | 后端 |
|---|---|
| Tar / TarGz / TarXz / TarBz2 | `LibArchiveBackend` → 回退 `TarBackend` |
| TarZst / Zstd | 直接用 `TarBackend` / `StreamBackend`（内置 7-Zip，只读） |
| Zip | `LibArchiveBackend` → 回退 `SevenZipBackend`（zip 为它的薄子类） |
| 7z | `LibArchiveBackend`（仅未加密）→ 回退 `SevenZipBackend` |
| Rar4 / Rar5 | `RarBackend`（读取用内置 7-Zip；系统有 `rar` 时可创建） |
| Gzip / Bzip2 / Xz | `LibArchiveBackend` → 回退 `StreamBackend` |
| 任意分卷（`.001`/`.part1.rar`） | 直接走命令行后端 |

写入（压缩、追加、删除、分卷写）一律使用命令行后端，libarchive 后端不实现写操作。

## 6. 格式检测

`MimeTypeHelper::detect()` 按三级流程：

1. **Magic Number**
   - 7z：`37 7A BC AF 27 1C`
   - RAR5：`52 61 72 21 1A 07 01 00`
   - RAR4：`52 61 72 21 1A 07 00`
   - ZIP：`PK\x03\x04`、空包/分卷签名
   - gzip：`1F 8B`
   - bzip2：`42 5A 68`
   - xz：`FD 37 7A 58 5A 00`
   - zstd：`28 B5 2F FD`
   - tar：偏移 257 处的 `ustar`
   - iso：偏移 0x8001（第 16 扇区）的 `CD001`（ISO9660 主卷描述符）或
     `BEA01`（UDF 卷识别序列）。该偏移远超其它格式的 512 字节采样窗口，
     因此仅在前两级都未命中时做一次定长探测，普通压缩包不为此付出开销。

2. **流格式进一步探测**
   - gzip/bzip2/xz/zstd 可能是 tar 压缩流，也可能是纯压缩流。
   - 后缀明确为 `.tar.gz` / `.tar.zst` 等时直接判定 tar 系列。
   - 否则用内置 7-Zip `x -so` 解出前 512 字节，检查 tar 头（`ustar` 或校验和）；
     成功则为 tar 系列，否则为纯流。只读取首块，不完整解压大文件。

3. **后缀回退**
   - 用于 magic 不明显或特殊分卷文件。
   - 支持 `.7z.001`、`.zip.001`、`.partNN.rar`、`.rNN` 等。

分卷处理：

- `isSplitVolume()` 识别 `.7z.001`、`.zip.001`、`.partNN.rar`、`.rNN`。
- `resolveFirstVolume()` 从任意分卷找到首卷，并保留磁盘上的实际大小写。
- 7z/zip 按数字排序分卷；RAR5 按 `partN` 排序；RAR4 找 `.rar`。

## 7. 后端实现

### 7.1 Tar 系列

工具：内置 7-Zip（`-ttar` / `-tgzip` / `-tbzip2` / `-txz`；zstd 只读解压）。

未压缩 `.tar` 直接操作；`.tar.gz/.xz/.bz2/.zst` 是单流格式，列表/解压/测试
分两段：先处理外层压缩流，再对临时 `.tar` 执行 7z 操作。zstd 只读取，
7-Zip 不能创建 zstd，因此 `.tar.zst` 不提供创建/追加/删除。

| 操作 | 命令要点 |
|---|---|
| 列表 | 压缩流先 `7z x -o<temp> ... -- archive` 取出 inner.tar，再 `7z l -slt -sccUTF-8 -- inner.tar` |
| 解压 | 同上取出 inner.tar，再 `7z x -o<temp> -y -spd -sccUTF-8 [-scs... -i@members] -- inner.tar` |
| 压缩 | 先 `7z a -ttar ... -- <temp>/inner.tar`，再 `7z a -tgzip|-txz|-tbzip2 [-mx=...] -- target inner.tar`（`.tar.zst` 不支持创建） |
| 追加 | `7z a -ttar -spd [-spf] [-snl] -scsUTF-8 -i@sources -- archive`（仅未压缩 `.tar`） |
| 删除 | `7z d -ttar -spd -scsUTF-8 -i@members -- archive`（仅未压缩 `.tar`） |
| 测试 | 压缩流先取出 inner.tar，再 `7z t -sccUTF-8 -- inner.tar` |

选项映射：

- 压缩级别：`-mx=1..9`（普通 `.tar` 忽略；tar.gz/tar.bz2 范围 1–9）。
- `followSymlinks` → 7-Zip 默认跟随；关闭时加 `-snl` 按链接存储。
- `storeFullPath` → `-spf` 并传绝对路径；否则取源文件公共父目录，用相对路径 + 工作目录。
- `excludePatterns` → `-x!pattern`；有排除模式时不加 `-spd`，避免 `-spd` 同时禁用排除通配符。
- 选中成员/源文件通过 UTF-8 listfile (`-scsUTF-8 -i@...`) 传入。
- 用户跳过危险符号链接时，成员列表会排除归档根目录的 `.` 条目；否则
  7-Zip 会把 `.` 展开为整棵树，导致被跳过的链接仍被解出。
- 中文/非 ASCII 文件名：7-Zip 输出统一 `-sccUTF-8`；旧代码页名称在解压后按 `EncodingDetector` 重命名。
- 加密：不支持。

限制：

- 不支持加密、分卷。
- `.tar.gz/.xz/.bz2` 不支持追加/删除，只有未压缩 `.tar` 支持。
- 压缩流需要临时解出 inner tar，磁盘占用约为未压缩 tar 大小；操作结束自动清理。

### 7.2 7z / Zip

工具：`7z`。zip 不再有独立实现：`backends/sevenzip/ZipBackend.h` 是
`SevenZipBackend` 的薄子类，只改格式开关，命令拼装完全复用：

```text
7z  → 默认
zip → -tzip
```

| 操作 | 命令要点 |
|---|---|
| 列表 | `7z l -slt -sccUTF-8 -p<pw> -- archive` |
| 解压 | `7z x -o<temp> -y -spd -sccUTF-8 -p<pw> [-scsUTF-8/-scs936 -i@members] -- archive` |
| 压缩 | `7z a -mx=N -mmt=on -sccUTF-8 [-spd] [-scsUTF-8 -i@sources] [选项] -- archive` |
| 追加 | `7z a ... -sccUTF-8 -spd -scsUTF-8 -i@sources -- archive` |
| 删除 | `7z d -spd -sccUTF-8 [-scs... -i@members] -- archive` |
| 测试 | `7z t -sccUTF-8 -p<pw> -- archive` |

命令行要点：

- `-sccUTF-8` 强制控制台输出 UTF-8，Windows 上尤其重要。
- `--` 停止开关/`@listfile` 解析，保护以 `-` 或 `@` 开头的归档名/成员名。
- `-spd` 禁用文件名通配符，保护 `*`、`?` 字面量；它也会禁用 `-x!` 模式，因此压缩带排除模式时不加。
- 选中成员和源文件通过临时 listfile 传入；UTF-8 归档用 `-scsUTF-8`，旧代码页 ZIP 用 `-scs936`/`-scs950`/`-scs932`。
- 读取操作始终传 `-p`（可为空），避免 7-Zip 在加密头归档上等待 stdin 密码。
- Linux 上 7-Zip 按原始字节写出旧代码页文件名，解压成功后由后端自底向上重命名为 UTF-8。

压缩选项：

- `-mx=N`：压缩级别 0–9。
- `-ms=on`：固实压缩，仅 7z。
- `-p...`：密码。
- `-mhe=on`：加密文件名，仅 7z。
- `-v...b`：分卷大小。
- `-snl`：把符号链接按链接存储（不加时 7-Zip 默认跟随链接）。
- `-spf`：保存完整路径。
- `-x!pattern`：排除模式。

能力：

- 读写、加密、分卷、追加/删除均支持。
- 分卷包不支持追加/删除。

### 7.3 RAR

读取工具：内置 7-Zip。`RarBackend` 继承 `SevenZipBackend`，把格式标记为
Rar4/Rar5，列表/解压/测试复用 7z 后端。

创建工具：系统 `rar` 命令，运行时通过 `ProcessRunner::resolveToolPath("rar")`
检测（支持 `QARCHIVER_RAR_PATH` 覆盖）。未安装时 `RarBackend::isCreateAvailable()`
返回 false，压缩对话框和偏好设置不显示 RAR 选项。

| 操作 | 命令要点 |
|---|---|
| 列表 | `7z l -slt -sccUTF-8 -p... -- archive` |
| 解压 | `7z x -o<temp> -y -spd -sccUTF-8 [-scs... -i@members] -p... -- archive` |
| 测试 | `7z t -sccUTF-8 -p... -- archive` |
| 创建 | `rar a -y -r -idq -ma4|-ma5 -m<0..5> [-p...|-hp...] [-v<bytes>b] [-s] [-ol] [-ep1|-ep2] [-x...] -scfl -- archive @sources` |

说明：

- 支持 RAR4/RAR5 分卷读取；`MimeTypeHelper::resolveFirstVolume()` 自动解析首卷。
- 系统有 `rar` 时可创建 RAR5 及 `.part1.rar` 分卷；界面沿用 0–9 压缩等级，
  后端映射为 RAR 的 0–5。分卷目录中同时存在旧分卷集时，先写临时目录，成功后再整体替换。
- 创建时源文件通过 UTF-8 listfile 传入；默认 `-ep1` 与 7z 后端的成员路径布局一致。
- 密码使用 `-p`，加密文件名使用 `-hp`；关闭“跟随符号链接”时加 `-ol`，固实压缩加 `-s`。
- 支持读取加密包；已创建 RAR 的追加/删除仍不支持。
- 不再依赖 `unrar`；内置 7-Zip 的 unRAR 代码负责读取。

### 7.4 纯压缩流

工具：`7z`；读取支持 `.gz/.bz2/.xz/.zst`，单文件创建支持 `.gz/.bz2/.xz`。

适用：`.gz`、`.bz2`、`.xz`、`.zst`。其中 `.zst` 当前只支持读取。

| 操作 | 命令要点 |
|---|---|
| 列表 | `7z l -slt -sccUTF-8 -- archive`，用于获取解压后大小 |
| 解压 | `7z x -o<temp> -y -sccUTF-8 -- archive` |
| 测试 | `7z t -sccUTF-8 -- archive` |
| 压缩 | `7z a -tgzip|-tbzip2|-txz -mx=N -y -sccUTF-8 -- archive file`（仅一个普通文件） |

条目名由压缩包文件名去掉 `.gz/.bz2/.xz/.zst` 后缀得到。解压后把临时目录中的单个文件移动到目标目录，并应用覆盖策略。zstd 纯流使用内置 7-Zip 读取，暂不支持创建。

### 7.5 libarchive 读后端与命令行回退

`LibArchiveBackend`（`src/backends/libarchive/`）在进程内直接调用系统 libarchive，覆盖所有**读**操作：列表、解压、预览、校验。写操作（压缩/追加/删除/分卷）不实现，直接返回 `OperationNotSupported`，仍由命令行后端完成。

能力对照（实测 libarchive 3.7）：

| 输入 | libarchive 读 | 说明 |
|---|---|---|
| zip（明文 / ZipCrypto / AES-128 / AES-256） | ✓ | 密码走 `archive_read_add_passphrase` |
| 7z（未加密，含 PPMd） | ✓ | 加密 7z 不支持，必须回退命令行 |
| tar / tar.gz / tar.bz2 / tar.xz | ✓ | 替代 `TarBackend`（7-Zip 子进程） |
| tar.zst / zst | — | 固定走内置 7-Zip（`TarBackend` / `StreamBackend`，只读） |
| gz / bz2 / xz 纯流 | ✓ | 用 raw 格式读，条目名由文件名推断 |
| iso（ISO9660 + Rock Ridge / Joliet） | ✓ | 光盘镜像读取见 7.6 |
| iso（UDF） | ✗ | libarchive 没有 UDF 支持，打开失败后自动回退内置 7-Zip |
| zip 分卷 / 7z 分卷 | ✗ | 直接由 `FormatRegistry::isSplitVolume()` 分流到命令行 |
| rar | — | 回退命令行（`RarBackend`，读取用内置 7-Zip；创建用系统 `rar`） |

分派规则：

- `ArchiveBackendFactory::createForRead()` 在“格式支持且非分卷且未关闭偏好开关（`backends/useLibArchive`）且未设置 `QARCHIVER_NO_LIBARCHIVE`”时返回 `LibArchiveBackend`，否则返回对应命令行后端。`ArchiveBackendFactory::isLibArchiveAvailable()` 暴露编译期能力，供设置对话框禁用开关。
- 运行期失败（`UnsupportedFormat` / `CorruptedArchive` / `WrongPassword` 等）时，`ArchiveManagerDispatch.cpp` 中的 `tryLibArchiveFallback()` 重建命令行后端、重新 `openArchive` 并**重放当前待处理操作**（列表 / 解压 / 校验，含“解压前校验”链路）；每个归档只回退一次。取消、路径穿越、磁盘空间等错误不回退。
- 回退时不再向 UI 重复发 `archiveOpened`，只在日志与输出面板记录一条提示。若无法创建命令行后端，会清除 busy 与待重试状态并报告原错误，不会卡在忙碌状态。

实现要点：

- 解压先落到 `FileUtils::createTempExtractDir()` 的临时目录，成功后统一 `moveExtractedContents()`：失败时删临时目录，不会留下半个包，也使回退是安全的。
- 每条目写入前 `normalizeEntryPath()` + `isPathTraversal()`。
- 符号链接目标在列表阶段写入 `ArchiveEntry::linkTarget`；`ArchiveManager` 解压前预检，命令行后端通过 listfile 排除用户选择跳过的链接及其子树；`FileUtils::unsafeExtractedSymlinks()` 在移动前复查实际链接（含 `..` 逃逸与链接链的规范化目标）。
- libarchive 硬链接使用平台硬链接 API 创建；目标必须位于临时目录内、存在且不是符号链接，目标稍后出现时延迟创建。失败/不安全时返回 `HardLinkUnsupported` 并中止，而不是留下空文件。
- 进度按已写入字节数（无条目大小时按条目计数），`archive_read_data_block()` 循环里检查 `isCancelled()`，可在解压过程中取消。
- 不使用 libarchive 的 `hdrcharset`：它按当前 locale 转换名称，非 UTF-8 locale（如 `LC_ALL=C`）下会直接导致打开失败。条目名优先 `archive_entry_pathname_utf8()`（带 UTF-8 标记时），否则取出 `archive_entry_pathname()` 的原始字节，由后端按“整个压缩包检测出的编码”自行解码，结果与进程 locale 无关。
- 整个压缩包只检测一次编码：遍历时暂存原始名称字节，遍历结束后统一投票再逐条解码，避免逐条猜测在同一包内 UTF-8/GB18030 之间跳变。列表得到的编码存入 `m_nameEncoding`，`extract()` 复用，保证列表与落盘文件名一致；符号链接/硬链接目标同样按该编码解码（合法 UTF-8 的目标原样保留）。`encodingHint` 非空时优先于检测结果。
- 拖拽预解压（`DragOutManager`）走同一个读路径工厂 `ArchiveManager::createReadBackend()`，并在私有 backend 内对 `UnsupportedFormat`/`EncodingError`/`CorruptedArchive` 自行做一次命令行回退；解压前先 `listContents()` 以填充进度总量。
- 写操作不使用 libarchive：追加/删除遇到只读后端时，按 8.4 的写回退流程用 CLI 后端重开；分卷、RAR、压缩 tar 流仍不支持追加/删除。

### 7.6 光盘镜像（iso）

`IsoBackend`（`src/backends/iso/`）是 `SevenZipBackend` 的只读特化：`l -slt` / `x` / `t` 与 7z/zip 完全共用（7-Zip 自动识别 `Iso`/`Udf`），`compress()` / `addFiles()` / `deleteFiles()` 直接返回 `OperationNotSupported`。

- 读取路径遵循 7.5 的分派规则：libarchive 可用时由进程内后端的 iso9660 读取器处理（支持 Rock Ridge 与 Joliet）；UDF 镜像在 libarchive 中打开失败，由 `tryLibArchiveFallback()` 自动改用内置 7-Zip 重试，无需额外代码。
- 格式识别见第 6 节：ISO9660 的 `CD001` 与 UDF 的 `BEA01` 位于第 16 个扇区（偏移 0x8001），超出常规 512 字节魔数窗口，因此 `MimeTypeHelper` 在前两级都未命中时做一次定长探测；改错后缀的镜像同样能识别。
- 只读语义由后端能力查询体现：`isReadOnly()` / `supportsAppend()` / `supportsVolume()` 均为否，`ArchiveManager::canModifyCurrentArchive()` 因此返回 false，界面的“添加/删除”自动禁用，`canReopenForWrite()` 也不会把 ISO 重开到 CLI 后端。
- 7-Zip 没有 ISO9660/UDF 写入器，格式不出现在压缩对话框；`FileTypeHelper` 在通用压缩包判定之前返回 `Kind::Iso`，保持磁盘映像专用图标。

### 7.7 能力矩阵

| 格式 | 读 | 写/压缩 | 追加/删除 | 加密 | 分卷 |
|---|---|---|---|---|---|
| tar | ✓ | ✓ | 仅未压缩 tar | ✗ | ✗ |
| tar.gz / tar.xz / tar.bz2 | ✓ | ✓ | ✗ | ✗ | ✗ |
| tar.zst / tzst | ✓ | ✗ | ✗ | ✗ | ✗ |
| zip | ✓ | ✓ | ✓（非分卷） | ✓ | ✓ |
| 7z | ✓ | ✓ | ✓（非分卷） | ✓ | ✓ |
| rar | ✓ | ✓（系统有 `rar` 时） | ✗ | ✓ | ✓ |
| gz / bz2 / xz | ✓ | ✗ | ✗ | ✗ | ✗ |
| zst | ✓ | ✗ | ✗ | ✗ | ✗ |
| iso（ISO9660 / UDF） | ✓ | ✗ | ✗ | ✗ | ✗ |

## 8. 关键流程

### 8.1 打开与列表

```text
UI 选择文件
  → ArchiveManager::openArchive
  → 先验证新路径存在且可读；保留旧归档，generation++
  → 发射 archiveOpening，进入 busy 状态
  → QtConcurrent 检测格式
  → 回到 GUI：finishOpen
  → 创建“待打开后端”，moveToThread(worker)
  → 连接信号并队列调用 backend->openArchive
  → backend 发射 archiveOpened
  → 销毁旧后端，切换 currentPath/format
  → UI 调用 listContents
  → backend 发射 fileListReady
```

检测失败、后端创建失败或打开失败时只清理待打开后端并报告错误；当前已打开归档保持可用。

### 8.2 解压

1. `ArchiveManager::validateExtraction`
   - 目标目录已指定。
   - 选中的条目或全部条目通过 Zip Slip 检查。
   - 符号链接预检：绝对目标、`..` 逃逸或空目标由 UI 询问“中止 / 跳过 / 本任务全部跳过”；目录链接被跳过时整个子树都不解压。
   - 用 `qint64` 做溢出检查的大小累计；异常/过大时以 `InsufficientDiskSpace` 拒绝，估算磁盘剩余空间。
2. 创建目标目录。
3. 如需“解压前测试”，先执行 `testArchive`，成功后再继续解压。
4. 后端解压到目标目录下的临时目录：

   ```text
   <dest>/.tmp_extract_<uuid>/
   ```

5. 移动前再次调用 `FileUtils::unsafeExtractedSymlinks()` 复查实际树；不安全则清理临时目录并中止。
6. `FileUtils::moveExtractedContents` 把临时内容移动到目标目录：
   - 保留目录结构或扁平化。
   - 处理 `Always/Never/Rename` 覆盖策略。
   - 保留权限、时间戳和符号链接。
7. 移动阶段任一条目失败时立即清理剩余临时目录；已移动到目标位置的文件不回滚，并通过 `PartialExtraction` 使用警告图标提示用户。
8. 清理临时目录，发射 `operationFinished`。
9. 如启用 `openDestAfterFinish`，UI 用 `QDesktopServices::openUrl()` 打开目标目录；返回 `false` 时显示“无法打开”警告，不声称具体的系统错误原因。

### 8.3 压缩

为避免覆盖已有压缩包时失败导致数据丢失：

1. 如果目标文件不存在，直接写入目标。
2. 如果目标已存在：
   - 在目标目录创建 `.qarchiver_compress_XXXXXX` 临时目录。
   - 后端把压缩包写到临时目录。
   - 在目标目录创建 `.qarchiver_backup_XXXXXX`，把旧目标及旧分卷重命名进去（不先删除）。
   - 全部新文件就位后才让备份目录析构删除旧文件；任一步失败则删除已安装的新文件并恢复旧文件。
   - 失败时删除临时产物，保留原目标。
3. 分卷文件按目标 basename 匹配并一起替换。

### 8.4 追加 / 删除 / 测试

- 先检查 `supportsAppend()` 或 `canModifyCurrentArchive()`。
- 仅 zip/7z（非分卷）和未压缩 tar 支持追加/删除。
- 当前是只读 libarchive 后端且格式支持写回退时，`ArchiveManager` 通过
  `ArchiveBackendFactory::create()` 重新打开同一归档（`m_forceCliForOpen`），在
  `archiveOpened` 后直接派发原写操作；成功后 UI 重新 `listContents`。
  这个重开属于内部实现细节：它**不发出** `archiveOpening`，也不发出
  `archiveOpened`（写操作通过 `operationFinished` 汇报），否则界面会停在
  “正在打开 …”这类只由 `archiveOpened` 收尾的状态上。
- 操作前对目标条目做路径安全检查。
- 成功后 UI 重新 `listContents`，保留当前目录（列表模式）。

### 8.5 密码流程

```text
加密包且密码为空
  → passwordRequested
  → ArchiveManager 暂停 busy，保存待重试操作
  → UI 弹出 PasswordDialog
  → retryWithPassword(password)
  → 最多重试 3 次

已提供密码但验证失败
  → WrongPassword（直接提示，不再弹出空密码请求）
```

列表、解压、测试、追加、删除都遵循这一规则。取消密码框、达到最大重试次数、或操作派发失败时，会同时清除待重试操作和“解压前测试”的中间状态。

密码只保存在内存中：

- 当前归档密码：`m_currentPassword`。
- 可选会话密码：`m_sessionPassword`，不写入磁盘。
- 关闭归档时清除当前密码；会话密码由“记住密码”设置控制。
- 密码弹窗的“本次会话记住密码”初始状态来自偏好设置；用户可在单次弹窗中取消选中，仅本次不记住，不回写偏好设置。在偏好设置中关闭“记住密码”会同时清除已保存的会话密码。

### 8.6 取消

```text
ProgressDialog::cancelled
  → ArchiveManager::cancelCurrentOperation
  → backend->requestCancel
  → ProcessRunner 轮询 atomic_bool
  → 终止子进程
  → 后端清理临时目录并报告 Cancelled
```

GUI 不会被阻塞；进度对话框保持到操作真正结束。

### 8.7 预览

`PreviewManager` 不调用当前主后端，而是：

1. 检查点击的文件大小是否超过 `preview/maxSize`。
2. 对图片额外选择**同一压缩包目录**下的图片条目：
   - 后缀：`.jpg`、`.png`、`.svg`、`.webp`、`.gif`、`.bmp`，大小写不敏感；
   - 跳过目录、子目录和符号链接；
   - 目录由规范化后的条目路径确定，不递归。
3. 在 `QtConcurrent` 线程中创建私有后端。
4. 将“点击图片 + 同目录图片”一起解压到缓存目录：

   ```text
   <Cache>/QArchiver/preview/<pid>/<archiveHash>/<internalPath>
   ```

5. 用 `QDesktopServices` 打开点击的图片。支持目录浏览的系统图片查看器
   可以直接用方向键查看同目录其它图片。
6. 同一目录已有批量解压任务时，后续双击等待该任务并打开最新目标，
   避免重复解压。
7. 同目录图片**总数**（不仅是尚未缓存的图片）超过 `preview/continuousImageDialogThreshold`（默认 200，0 表示任意批次）且并非全部已缓存时，延迟约 250 ms 显示模态准备对话框；对话框默认不确定进度，收到进度后显示实际计数，取消会终止后台解压但保留已缓存图片，完成/失败会先关闭对话框。阈值只控制是否显示准备提示，不限制批量解压总量。
8. 用 `QDesktopServices::openUrl()` 打开查看器/目标目录；返回 `false` 时显示“无法打开”的警告（不声称具体系统错误）。
9. 退出时清理当前 PID 的缓存；启动时清理已不存在 PID 的目录。

图片连续查看依赖系统查看器，不在应用内实现上一张/下一张窗口。

### 8.8 一键解压

主窗口工具栏、起始页按钮和命令行 `--oneclick` 共用同一流程：

1. 若当前没有已打开归档，先弹出文件选择框，选中后打开；
2. 归档列表就绪后，确定目标根目录：
   - `extract/oneClickDestMode=archive`（默认）：压缩包所在目录；
   - `custom`：`extract/oneClickDest`，为空时回退到“文档”目录；
3. 目标目录名为压缩包基本名称（去掉 `.tar.gz` 等复合后缀）；
   目标已存在时用 `FileUtils::uniqueDestinationPath()` 生成 `名称 (1)`；
4. 首次使用弹出“解压完成后是否关闭”提示；“解压后关闭”为默认/强调按钮，
   结果写入 `extract/oneClickCloseAfter` 与 `extract/oneClickCloseAsked`；
5. 使用 `OverwritePolicy::Always`（目标为新目录，无覆盖风险），显示进度对话框；
6. 成功后若 `oneClickCloseAfter` 为真，延迟 650 ms 调用 `close()`；
   失败/取消/密码取消不关闭。未关闭时按 `extract/openDestAfterFinish` 决定是否打开目录。

安全：一键解压不做覆盖询问，但通过“新目录或唯一名称”避免覆盖已有文件；
密码、符号链接预检、路径穿越检查与普通解压完全一致。

### 8.9 7-Zip 性能测试

`BenchmarkDialog` 从“偏好设置 → 工具”或“工具”菜单打开：

- 使用 `SevenZipTool::resolvePath()` 定位内置/系统 7-Zip；
- 命令为 `7z b [-mmt=N] -sccUTF-8`；
- `QProcess` 异步运行，合并 stdout/stderr，按完整行追加到只读文本框；
- 线程数 0 表示自动；支持停止（terminate，1.5 s 后 kill）、复制结果；
- 完成后从输出中提取 `Tot:`、`MB/s`、`Freq:` 行作为摘要；
- Windows 上通过 `qarchiverHideConsoleWindow()` 隐藏控制台窗口；
- 测试不读取或修改任何压缩包。

### 8.10 外部编辑与写回

双击条目使用系统默认程序打开后，用户在外部的修改可以存回压缩包：

```text
双击条目 → PreviewManager 解压到 <Cache>/preview/<pid>/<hash>/<内部路径>
        → openExtractedPath() 成功
        → ExternalEditManager::track(localPath, 缓存根, 条目路径, 归档路径, 密码, 名称编码)
外部程序保存 → 轮询发现 size/mtime 变化
        → 稳定延迟（默认 1.5 s，两轮一致）后询问“保存到压缩包 / 不保存”
        → 保存：AddFilesOptions{sourceRoot=缓存根, sourceFiles={条目路径}, nameEncoding}
                → ArchiveManager::addFiles()（7z a 就地替换同名成员）
                → 写回成功后重新列出内容并刷新基线
```

关键点：

- 预览缓存目录本身**镜像压缩包目录结构**，被打开文件相对缓存根目录的路径就是压缩包内成员路径，因此写回不需要重新解压整包。
- 使用轮询（默认 1.5 s）而不是 `QFileSystemWatcher`：编辑器通常以“临时文件 + 重命名”方式原子保存，会替换 inode 并静默失效旧文件的监视器。连续两轮观察到相同的 size/mtime 且已过稳定延迟才认为写入结束，避免把分块写入当成保存完成。
- 误报抑制：记录跟踪时的内容哈希（不超过 16 MiB 的文件），若编辑器只是重写相同内容则静默刷新基线、不弹窗。
- 旧代码页成员名（GBK/Big5/Shift-JIS）：`FileUtils::stageFileForArchiving()` 把编辑后的文件以**原始字节名称**复制到临时暂存目录，`AddFilesOptions::nameEncoding` 让 7-Zip 用同一代码页的列表文件查找该文件，从而替换原成员而不是追加一个 UTF-8 名称的副本。暂存目录用 `FileUtils::removeDirectoryRecursivelyRaw()` 清理（Qt 无法删除无法解码的文件名）。
- 只读场景不跟踪：`PreviewManager` 仅在归档已打开且 `canModifyCurrentArchive()` 为真时登记；iso/rar/分卷/压缩 tar 流/纯流因此不会弹出保存提示（`preview/externalEditWatch` 可整体关闭）。
- 写回经 `ArchiveManager` 的普通 add 通道，因此密码重试（`retryWithPassword`）、libarchive → CLI 重开、`UiOp::Add` 完成后的重新列目录都自动复用。归档在写回前被关闭时，主窗口会先重新打开它，再在 `fileListReady` 之后延后一拍派发写回（避免在管理器仍在清理上一次操作时重入）。
- 失败（只读文件系统、磁盘满、密码错误、归档重开失败）会提示错误并刷新基线，避免每 1.5 s 反复询问；用户可再次编辑重试。

## 9. 安全与可靠性

- **路径穿越**：`FileUtils::isPathTraversal` 按路径组件拒绝 `..`，允许 `foo..bar`；解压前检查选中条目或全部条目。
- **临时解压**：所有后端先解压到目标目录内的临时目录，再移动到最终位置，便于清理失败产物；失败时立即清理剩余临时目录，已移动文件不回滚并提示 `PartialExtraction`。
- **符号链接两层防护**：解压前依据条目列表预检并让用户决定；命令行后端从选择列表排除被跳过的链接/子树；移动前再检查实际解压树，捕获链接链与 `..` 逃逸。
- **硬链接**：仅当目标在当前临时树内、存在且不是符号链接时创建；否则中止并报告 `HardLinkUnsupported`，不生成空文件。
- **大小溢出**：解压与拖出预检使用 `FileUtils::addSizeChecked` / `totalUnpackedSizeChecked`，超过 `qint64` 或出现负值时拒绝操作；拖出的预计算以 `-1` 表示异常并转为 `InsufficientDiskSpace`。磁盘空间需求计算也先检查乘法溢出。
- **覆盖策略**：`Ask` 在 UI 层收敛为全部覆盖/全部跳过；`Rename` 使用唯一文件名。
- **进程管道**：`ProcessRunner` 同时读取 stdout/stderr，避免详细输出塞满管道导致死锁。
- **取消与超时**：统一由 `ProcessRunner` 处理，后端不直接操作 `QProcess`。
- **编码**：`EncodingDetector` 使用 iconv 探测/转换 GB18030、Big5、Shift-JIS 等。检测顺序为“合法 UTF-8 最优先 → 其余按可信度打分”：先用 iconv 校验可解码，再扫描源字节的两字节代码单元（两字节编码往返不变，位置即码表位置），统计落在该编码常用字区的比例（GB18030 lead B0–F7 / trail A1–FE；Big5 lead A4–C6；Shift-JIS lead 82–83 假名 / 88–98 一级汉字，单字节 A1–DF 半角假名单独计为宽字符），得分最高者胜出；最高与次高分差 < 0.05 时按系统 locale 顺序（zh_TW/zh_HK/zh_Hant → Big5、ja → Shift-JIS、其余 GB18030）敲定。因此 Big5 归档在任意 locale 下都能正确识别，而不是“首个能解码的编码胜出”。CLI 后端的编码投票只采 `-slt` 的 `Path =` 行（`SevenZipUtils::sampleNameBytes`），横幅、时间戳与压缩包自身路径不参与。7z 输出按同一检测逐行解码，旧代码页 ZIP/TAR 的选中解压使用对应 `-scs{id}` listfile，Linux 解压后自动把旧编码文件名重命名为 UTF-8。
- **日志**：日志中密码参数替换为 `-p***`。
- **压缩原子性**：目标已存在时先写临时文件，再把旧文件集备份、安装新文件，失败回滚，避免删除旧包后失败导致数据丢失。
- **流式列表**：7z/tar 的 `-slt` 输出通过 `ProcessRunner` 行回调交给 `SevenZipListParser`，不再保留完整命令行文本；解析出的结构化条目最终一次性交给 UI。仅保留有限 stdout/原始字节样本用于错误与编码检测。普通压缩/解压仅保留有界 stdout 日志，stderr 错误完整保留；解析成员名时只去掉 `=` 后的一个分隔空格，保留名称首尾空格。
- **外部打开**：所有 `QDesktopServices::openUrl()` 调用都检查返回值；失败时显示警告并记录路径，不把“无关联程序/权限/桌面会话”等可能性表述为确定的系统错误。
- **启动**：7-Zip `--help`/RAR 探测在后台线程执行；主题优先使用 Qt 平台颜色提示，不在启动路径上同步查询 DBus。
- **大列表与过滤**：`FilterProxyModel` 缓存已编译的通配符正则，`MimeTypeHelper` 的识别正则静态复用；拖出路径先排序再线性剪枝，重叠选择按祖先前缀 O(深度) 统计，避免 O(n²) 扫描。
- **线程安全**：后端只在工作线程执行；UI 不直接调用后端阻塞方法；generation 防止迟到信号污染新归档。
- **Windows 进程**：`ProcessRunner`、`MimeTypeHelper` 的流探测和性能测试都通过
  `PlatformProcess.h` 设置 `CREATE_NO_WINDOW`，避免 GUI 启动 7z/rar 时闪出控制台。
- **Windows 编码**：`EncodingDetector` 在 Windows 上使用
  `MultiByteToWideChar`/`WideCharToMultiByte`（CP936/CP950/CP932/CP54936），
  不再依赖 iconv；非 Windows 平台继续使用 libc iconv。
- **Windows 清理**：预览与拖出暂存目录使用 `FileUtils::isProcessRunning()`
  （Windows `OpenProcess`，Unix `/proc`）判断 PID 是否存活，跨平台清理崩溃残留。
- **libarchive 写回退**：zip/7z/未压缩 tar 的添加/删除遇到只读 libarchive 后端时，
  自动用 CLI 后端重新打开同一归档并重放操作；重开失败或格式不支持时回退为原错误。

日志查看：

- 启动时 `src/app/` 的初始化流程将日志写入 `QStandardPaths::CacheLocation` 下的 `app.log`。
- 在 Linux 上通常为 `~/.cache/QArchiver/QArchiver/app.log`（受 `XDG_CACHE_HOME` 影响）。
- 需要查看更多调试信息时，可用环境变量 `SAM_DEBUG=1` 启动以开启 Debug 级别日志。
- 界面不提供“日志”按钮；查看日志时直接打开上述 `app.log` 文件即可。

已知限制：

- 7z/zip/tar 的旧代码页成员名已通过 listfile + 解压后重命名处理；纯流格式的条目名仍由压缩包文件名推断。
- 文件名包含换行符时，基于行输出的 `-slt`/listfile 解析仍不可靠。
- 密码通过命令行参数传给外部工具，同用户的其他进程可能看到参数。
- RAR 读取用内置 7-Zip，创建需系统 `rar`；已创建 RAR 的追加/删除不支持。纯流格式只读。
- 硬链接重建只在 libarchive 读后端中完成；命令行后端不保证重建硬链接。
- 连续看图依赖系统查看器支持在同一目录内切换图片；应用内不提供上一张/下一张窗口。
- 解压移动阶段失败时，已经移动到目标的文件不会回滚，只会明确提示。
- Windows 上由 libarchive 解压符号链接需要开发者模式或管理员权限；命令行 7-Zip 后端通常直接跟随/跳过链接。
- 一键解压默认不弹目录选择框；需要每次选择目录时使用“解压到...”。
- 使用沉浸式无边框标题栏 `ImmersiveTitleBar`，集成菜单、窗口按钮、拖动、双击最大化与四边四角缩放；欢迎页与归档页共用。

## 10. UI 设计

### 10.1 主窗口

```text
┌──────────────────────────────────────────────┐
│ 工具栏：打开/新建/解压/添加/删除/关闭…       │
├──────────────────────────────────────────────┤
│ 导航按钮 + 面包屑路径栏 + 搜索框（280px）   │
├──────────────────────────────────────────────┤
│ 内容视图（网格 / 列表 / 树形）               │
│ 网格默认；列表/树形显示 Name/Size/... 表头   │
├──────────────────────────────────────────────┤
│ 状态栏：状态文本 + 忙碌指示器                │
└──────────────────────────────────────────────┘
左侧 Dock：最近文件（默认隐藏，在偏好设置中开启）
```

主要交互：

- 工具栏按钮默认“图标在上，文本在下”，可在偏好设置切换；现代工具栏的
  “设置/关于”与左侧功能按钮使用同一大按钮样式。
- “最近文件”侧边栏默认隐藏，在偏好设置中勾选“显示最近文件侧边栏”后显示。
- 现代布局在“文件管理”本地浏览时，左侧面板切换为系统位置侧栏（主目录、桌面、
  文档、下载、图片、音乐、视频、文件系统及 `/media`、`/mnt`、`/run/media` 下的
  用户挂载卷）。位置按清理后的绝对路径去重；浏览子目录时高亮其所属位置（取最
  深前缀），不再残留上一个选中项。位置列表在窗口重新显示，或打开只落在文件系统
  根下的目录时限频刷新（默认 5s），避免每次导航都调用 `QStorageInfo::mountedVolumes()`
  （网络挂载可能较慢）。经典布局不提供系统位置侧栏。
- 本版本不提供压缩包属性对话框：该信息与文件管理器的“文件属性”弹窗重复，故工具栏“属性”按钮与右键菜单“属性”项均已移除。
- 双击文件：预览。图片会静默预解压同目录图片，交给系统查看器连续浏览。
- 图片缩略图由 `ThumbnailManager` 按需生成：压缩包内的图片由独立读取后端在小型线程池中解压单张，本地文件则直接读取；两者都缩放到 128px 内并写入持久缓存，缓存键包含文件大小与修改时间。模型收到完成信号后只刷新对应图标。现代布局各视图都启用缩略图，列表视图按行高（约 22px）等比例缩放、宽度上限 96px；经典布局只在网格视图启用，列表/树保持普通图片图标。
- 网格模式双击目录：进入目录；不显示 `..`，通过“上一级”按钮返回。网格使用
  `ArchiveGridView`（`QListView::IconMode`）和自定义 delegate，支持多选、
  右键菜单、拖出与详细信息联动。delegate 的 `sizeHint()` 返回完整网格单元，
  图标在单元内居中；文件名用 `QTextLayout` 按任意字符换行并逐行居中，
  长文件名可显示多行。单元格高度按当前目录最长换行文件名动态计算：只有
  `DisplayRole` 变化才触发重算（缩略图等只改图标的变化忽略），模型批量变化
  合并为一次零延时扫描，视图隐藏时跳过，重新显示或字体/DPI 变化时补算。
- 树状模式双击目录：展开/折叠。
- 列表模式双击目录：进入目录；双击 `..`：返回上一级。
- 现代导航栏在内容列表上方提供网格/列表/树形切换，主页与归档页均可用；
  偏好设置中也可切换。设置持久化为 `ui/contentViewMode`，默认 `grid`。
- 经典布局在筛选框左侧提供面包屑路径框和导航按钮，按“前进、后退、上一级”
  排列（快捷键 `Alt+Right` / `Alt+Left` / `Alt+Up`），复用与现代导航栏相同的
  历史和目录状态。搜索框固定宽度 280px，与现代导航栏一致。经典列表/树使用
  16px 图标与 24px 行高，避免图标重叠；现代视图尺寸不受影响。
- 路径导航使用 Windows 7 风格面包屑 `BreadcrumbPathBar`，现代/经典布局共用：
  压缩包根名与内部每一级目录、或本地盘符/根与每一级文件夹都是可点击按钮；
  宽度不足时中间层级收进“...”按钮的菜单，继续缩小时先省略首级、最后省略末级；
  层级之间显示跟随主题的箭头分隔符。路径框右侧保留空白区，单击切换为只读单行
  完整路径（自动全选，可复制），`Esc`/回车/失焦后恢复面包屑。
- 右键菜单：解压选中、添加文件、删除选中、测试、排序方式（本地浏览时还提供
  打开、添加到同名 ZIP、压缩到 7z/ZIP/tar 等、打开所在文件夹与刷新）。
- 拖入文件：打开压缩包，或向当前可写归档追加文件。
- 拖出条目：把选中条目预解压到缓存 staging（`~/.cache/QArchiver/drag/<pid>/<uuid>/`），再用标准 `text/uri-list` 拖拽交给目标程序；落地位置与同名冲突由文件管理器处理。准备在按下鼠标的手势内完成，因此**不用弹窗**（弹窗会打断拖拽手势），只用忙碌光标 + 状态栏进度，Esc 可取消；准备结果会被缓存，选中项已准备好时再次拖动可立即起拖。若准备完成时鼠标已松开，状态栏提示“再次拖动即可”。staging 保留到应用退出。拖回 QArchiver 自身会被拒绝并提示。
- 打开/检测/压缩/解压期间状态栏显示忙碌提示；长操作显示进度对话框。`ArchiveManager` 忙碌时工具栏和右键菜单动作禁用；拖出准备也会拒绝启动。
- 列表成功后清空 `m_currentOp`，避免后续完成信号把旧操作误当成当前操作。

### 10.2 内容模型

`FileListModel` 继承 `QAbstractItemModel`，列：

```text
Name, Size, Packed, Modified, Encrypted
```

角色：

| 角色 | 含义 |
|---|---|
| `PathRole` | 压缩包内部原始路径 |
| `IsDirRole` | 是否目录 |
| `IsParentRole` | 列表模式的合成 `..` 行 |
| `IsThumbnailRole` | 当前图标是否为图片缩略图（现代布局或经典网格） |

树状模式：

- 根据条目路径构建层级。
- 自动补充缺失的合成目录节点。
- 支持排序、过滤、选中、双击。

列表模式：

- 只显示当前目录的直接子项。
- 列表模式非根目录时第 0 行为 `..`，不参与排序；根目录不显示 `..`。
- 网格模式不显示 `..`，通过“上一级”动作返回；树模式本身不使用该行。
- 显示名称为 basename，完整路径放在 tooltip/角色中。
- `enterDirectory()` / `goUp()` 维护当前目录。
- 排序由 `FileListModel::sortFlat()` 完成，代理不参与 `..` 排序。
- `setEntries()` 时建立“目录 → 直接子项”缓存和目录路径集合，`directoryExists()` 为 O(1)，导航不重新扫描完整条目列表；缓存保留原始成员路径、隐式父目录和重复条目。

`FilterProxyModel`：

- 树状模式递归过滤。
- 列表模式委托源模型排序，保证 `..` 始终在第一行。
- 普通文本自动转换为 `*text*` 子串匹配；`*`、`?` 作为通配符。
- 支持路径前缀、隐藏加密条目、大小写敏感选项。

### 10.3 对话框

| 对话框 | 主要内容 |
|---|---|
| `ExtractDialog` | 目标目录、保留目录结构、解压前测试、完成后打开、覆盖策略 |
| `CompressDialog` | 格式、级别、密码、加密文件名、分卷、排除模式、符号链接、固实压缩、完整路径 |
| `PasswordDialog` | 密码输入、本次会话记住 |
| `ProgressDialog` | 进度、当前文件、输出日志、取消；连续看图准备时复用为模态对话框（隐藏输出区、默认不确定进度） |
| `PreferencesDialog` | 四个标签页：常规（格式/级别/覆盖策略/行为/libarchive）、解压（一键解压目标/完成后关闭/预览阈值）、界面（布局、主题、内容视图、工具栏样式、最近文件、背景图片）、工具（7-Zip 性能测试入口与辅助程序路径） |
| `BenchmarkDialog` | 线程数、实时输出、开始/停止/复制/关闭、耗时与结果摘要 |

压缩选项联动：

- 密码、分卷：zip/7z，或系统已安装 `rar` 时的 RAR 可用。
- 加密文件名：7z/RAR 且已输入密码时可用。
- 固实压缩：7z 或系统已安装 `rar` 时的 RAR 可用。
- 压缩级别：普通 `.tar` 禁用；tar.gz/tar.bz2/gz/bz2/xz 范围 1–9。
- 纯压缩流（`.gz/.bz2/.xz`）只允许一个普通文件，`StreamBackend::compress()`
  调用 `7z a -tgzip|-tbzip2|-txz` 创建；`.zst` 只读，不显示创建入口。
  创建对话框负责校验并禁用加密/分卷/排除等不适用选项。
- 其余格式按能力禁用不适用选项。

### 10.4 图标、工具栏与主题

- 资源位于仓库根目录的 `resources/`（原 `src/resources/` 已移除，见第 12 节），
  由三个 qrc 清单以固定前缀暴露：

| 前缀 | 内容 |
|---|---|
| `:/icons/classic/` | 经典主题工具栏/文件图标 |
| `:/icons/modern/` | 现代主题工具栏与导航图标 |
| `:/icons/file-types/classic/` | 经典布局文件类型图标 |
| `:/icons/file-types/modern/` | 现代布局文件类型图标 |
| `:/themes/aero/` | `aero.qss` 与 `light/`、`dark/` 字形 |
| `:/themes/modern/` | `modern.qss` 与 `light/`、`dark/` 字形 |

- 所有资源路径只在两处拼接：`src/ui/icons/IconUtils.h` 的 `Icons::` 命名空间
  与 `src/ui/theme/ThemeManager.cpp` 的 token 表，其他地方禁止硬编码 `:/...`。
- 工具栏样式设置：

| 设置值 | 效果 |
|---|---|
| `textUnderIcon` | 图标在上，文本在下（默认） |
| `textBesideIcon` | 图标在左，文本在右 |
| `textOnly` | 纯文本 |
| `iconOnly` | 纯图标，使用 tooltip 显示文本 |

外观分为现代与经典两套主题，由 `AppSettings::uiLayoutMode()` 选择：

- 基础样式为 Fusion，由 `AeroProxyStyle` 覆盖 `SH_DialogButtonLayout`，统一使用 Windows 按钮顺序。
- 文件选择使用系统原生文件对话框。压缩源需要文件/文件夹二选一：先显示选择类型
  的提示，再调用原生“多选文件”或“选择文件夹”对话框，不再强制 Qt 控件对话框。
- `QGuiApplication::setDesktopFileName("yincolor.qarchiver")` 让桌面把窗口与
  `yincolor.qarchiver.desktop` 关联，启动器/任务栏可显示安装的应用图标。
- `ThemeManager` 根据布局选择样式表，并按浅色 / 深色 token 表替换 `@token@` 后应用到 `QApplication`：
  - **现代**：`:/themes/modern/modern.qss`，扁平白底、蓝色强调色、圆角输入框与按钮、
    渐变头部、无边框表头；QSS 小图标（箭头、复选、分支、排序、滚动条等）位于
    `resources/themes/modern/light|dark/`，由 `tools/gen_modern_icons.py` 生成。
  - **经典**：`:/themes/aero/aero.qss`，Frutiger Aero / Windows 7 蓝玻璃质感与 Aero Dark；
    单色小图标位于 `resources/themes/aero/light|dark/`，由 `tools/gen_aero_assets.py` 生成，
    复选框 / 单选框保留 Win7 彩色位图。
  - 两套主题的 `light/` 与 `dark/` 使用**相同文件名**，切换主题只换目录，不换名字
    （`tools/check_resources.py` 会校验两侧文件名集合一致）；
    `Icons::themed()` 是唯一的路径构造点。
  - 深浅主题完全一致、无需变体的字形放在主题根目录（`themes/aero/` 的
    Win7 复选框/单选框位图、`themes/modern/` 的 `check`/`minus`/`radio-dot`），
    QSS 直接用 `url(:/themes/<theme>/<file>.png)` 引用。
- 经典图标尺寸规则：工具栏/对话框图标 48px，列表图标（`file`、`folder`、`home`）64px，
  `.ico` 为多尺寸容器，由 `tools/normalize_classic_icons.py` 维持。
- 现代文件列表 delegate 使用扁平行选中/悬停配色，不绘制底部分隔线和虚线焦点框；
  经典 delegate 与 `ArchiveTreeView::drawBranches()` 共同绘制 Aero 渐变整行高亮。
- 现代进度条为蓝色渐变；经典进度条为 Windows 7 绿色渐变。
- 窗口使用沉浸式无边框标题栏 `ImmersiveTitleBar`，放在 `QMainWindow::setMenuWidget()`
  槽位：集成应用图标、菜单、最小化/最大化/关闭、拖动与双击最大化及尺寸调整
  （`WindowResizeBorders` 提供四边/四角命中区，右下角保留 `QSizeGrip`），
  现代/经典布局共用，菜单集中到标题栏。菜单栏只占菜单项自身宽度，右侧空白归
  标题栏以支持拖动；标题栏背景/文字使用独立的 `titleBarTop`/`titleBarMid`/
  `titleBarBottom`/`titleBarText` 主题 token，随布局与浅深主题切换。
- 无边框窗口首次显示时在主屏可用区域内居中；窗口大于可用区域时贴左上角，
  避免标题栏落到屏幕外。之后的显示与布局切换保留用户手动移动的位置。
  Wayland 下顶层位置由合成器决定，客户端无法强制居中。
- `MessageBoxUtils::showWarning()` 使用 `:/icons/classic/warning.ico`；连续看图的准备进度
  对话框不使用该警告图标。

### 10.5 背景图片 API（经典布局）

压缩包内容视图的可视区域（`QTreeView::viewport()`）可显示一张半透明背景图片
（PNG 透明通道会保留）。相关代码在 `src/ui/background/BackgroundImage.*`。

- `BackgroundImageOptions`：`path`、`alignment`、`size`、`opacity`、`enabled`。
- `BackgroundImage` 静态辅助函数：
  - `alignmentToString()` / `alignmentFromString()`：`topLeft`、`top`、`topRight`、`left`、`center`、`right`、`bottomLeft`、`bottom`、`bottomRight`。
  - `scaledSize()`：宽高都为 0 用原始尺寸；只有一个为 0 时保持宽高比；都为正数时精确缩放。
  - `targetRect()`：按对齐方式计算目标矩形。
  - `loadPixmap()`：用 `QImageReader` 读取并保留 alpha；按绝对路径 + mtime + 大小缓存，最多保留 8 张，`ArchiveTreeView` 与 `BackgroundWidget` 共用。
  - `clearPixmapCache()`：测试或需要强制重新解码时清空缓存。
  - `paint()`：按尺寸、对齐和透明度绘制。
- `AppSettings::setBackgroundImageOptions()` 一次性写入路径、对齐、尺寸和透明度，只 `sync()` 一次并发出一次 `backgroundImageChanged()`。
- `ArchiveTreeView` 持有一个堆叠在 `viewport()` 下方的透明图片层：
  - 只覆盖可视内容区，自动排除表头、边框和横/纵滚动条；
  - 绘制在行内容之后，滚动时保持固定；
  - 通过 `setBackgroundImage()` 设置，`reloadBackgroundFromSettings()` 从
    `AppSettings` 读取；偏好设置保存后经 `AppSettings::backgroundImageChanged()`
    实时重载。
- 经典布局的占位页（命令行启动等待压缩包打开或打开失败时）使用中央
  `BackgroundWidget` 绘制同一张图；页面切换时自动启用/禁用该中央图片，避免与
  归档页 viewport 图片重叠。经典布局正常启动与“文件 → 起始页”直接进入本地
  文件浏览，背景图由归档视图 viewport 绘制。现代布局的欢迎页不绘制背景图；从
  “文件管理”进入本地目录浏览后，背景图同样由归档视图 viewport 绘制。

### 10.6 工具栏与图标

- 现代布局图标位于 `resources/icons/modern/`（工具栏/导航 64px，导航小图标 32px，
  深色变体 `*-light.png`）；文件类型图标按布局分为
  `resources/icons/file-types/modern/` 与 `resources/icons/file-types/classic/`，
  两套使用相同文件名，`Icons::fileType()` 按当前布局选择目录。
- 生成脚本 `tools/gen_modern_icons.py`：SVG 源同时落到 `resources/svg/modern/`，
  应用图标写 `resources/app-icons/qarchiver-<size>.png` 与 `qarchiver.ico`。
- 文件类型覆盖图片、音视频、PDF、Office（文档/表格/演示）、代码、字体、
  磁盘映像、电子书、数据库、种子、设计稿、字幕、3D/CAD、安装包等；
  后缀到图标的映射由 `src/common/FormatRegistry` 统一提供，`FileTypeHelper` 只做查表。
- 应用图标 `resources/app-icons/qarchiver.ico` 供 Windows `.rc` 使用；
  Linux 安装 32/48/64/128/256/512 多尺寸 hicolor 图标和 pixmaps 兜底，
  desktop 文件设置 `StartupWMClass=QArchiver`。
- 经典布局使用 `resources/icons/classic/` 与 `resources/themes/aero/` 的 Aero 资源。

## 11. 设置项

`AppSettings` 使用 `QSettings`（INI，UserScope）。普通设置写入后立即 `sync()`；背景图片四项通过 `setBackgroundImageOptions()` 合并为一次写入、一次同步和一次变更信号。

### 11.1 与界面布局相关的设置

偏好设置的“界面”页只显示对当前布局有效的选项：**单项无效的整行直接隐藏**，
而不是置灰留白（`PreferencesDialog::updateLayoutVisibility()`），切换布局下拉框时
立即生效，不需要保存后重开对话框。

| 设置 | 现代布局 | 经典布局 | 说明 |
|---|---|---|---|
| 界面布局 / 主题 | ✔ | ✔ | 两种布局都有效 |
| 归档内容视图（网格/列表/树） | ✔ | ✔ | 两种布局都生效；经典布局通过偏好设置切换 |
| 工具栏样式 | ✘（隐藏） | ✔ | 只作用于经典工具栏（`m_mainToolBar`） |
| 显示最近文件侧边栏 | ✘（隐藏） | ✔ | 侧边栏只在经典布局中创建 |
| 最近文件上限 | ✔ | ✔ | 现代欢迎页与经典侧边栏都受它限制 |
| 背景图片 | ✔ | ✔ | 绘制在列表/树视图之后；网格视图不显示（界面页有说明） |

两种布局共用 `ui/contentViewMode`。经典布局没有工具栏/菜单切换入口，用户可在
偏好设置的“界面”页选择网格、列表或树；`MainWindow::contentViewMode()` 不再因
布局模式固定返回 `tree`，切换布局后仍使用同一个内容视图设置。

| Key | 含义 |
|---|---|
| `compress/defaultFormat` | 默认压缩格式 |
| `compress/defaultLevel` | 默认压缩级别 |
| `compress/followSymlinks` | 默认跟随符号链接 |
| `extract/overwritePolicy` | 默认覆盖策略 |
| `extract/preservePaths` | 默认保留目录结构 |
| `extract/openDestAfterFinish` | 解压后打开目标目录 |
| `preview/maxSize` | 单文件预览大小上限（字节，默认 50 MiB） |
| `preview/continuousImageDialogThreshold` | 连续看图准备对话框阈值，默认 200，范围 0–9999，0 表示任意批次 |
| `preview/externalEditWatch` | 是否监视外部编辑并在写回压缩包前询问，默认 `true`（见 8.10） |
| `backends/useLibArchive` | 是否用 libarchive 读取压缩包（默认 `true`；构建无 libarchive 时忽略） |
| `security/rememberPasswords` | 是否在会话内记住密码 |
| `history/recentFiles` | 最近文件列表 |
| `history/recentFilesLimit` | 最近文件上限，默认 20，范围 1–100；降低时立即裁剪并刷新侧边栏 |
| `ui/layoutMode` | `modern`（默认）/ `classic` |
| `extract/oneClickDestMode` | `archive`（默认）/ `custom` |
| `extract/oneClickDest` | 一键解压自定义目标根目录 |
| `extract/oneClickCloseAfter` | 一键解压成功后关闭程序 |
| `extract/oneClickCloseAsked` | 是否已显示过首次提示 |
| `ui/theme` | `system` / `light` / `dark` |
| `ui/contentViewMode` | `grid`（默认）/ `tree` / `list` |
| `ui/itemSize` | 图标大小，默认 96，范围 48–128；经典列表/树固定 16px，不受该值影响 |
| `ui/toolButtonStyle` | 工具栏按钮样式 |
| `ui/showRecentFiles` | 是否显示最近文件侧边栏（默认关闭） |
| `ui/backgroundImage` | 背景图片路径；为空表示不显示 |
| `ui/backgroundImageAlignment` | 背景图片对齐：`topLeft` / `top` / `topRight` / `left` / `center` / `right` / `bottomLeft` / `bottom` / `bottomRight` |
| `ui/backgroundImageWidth` | 背景图片宽度（逻辑像素；0 表示自动） |
| `ui/backgroundImageHeight` | 背景图片高度（逻辑像素；0 表示自动） |
| `ui/backgroundImageOpacity` | 背景图片透明度（0.0–1.0） |

主题由 `ThemeManager` 解析；布局由 `AppSettings::uiLayoutMode()` 决定，
布局变化会重新应用对应样式表（现代 `modern.qss` / 经典 `aero.qss`）。

- `system` 优先使用 Qt 的平台颜色提示（Qt 6.5+），Qt 6.4 使用调色板明度；
  不在启动路径上同步执行 DBus 查询。
- `light` / `dark` 分别对应 Aero 浅色与 Aero Dark；
  调色板、字体（Segoe UI / Microsoft YaHei UI / Noto Sans CJK 回退）、
  `aeroDark` / `aeroAccent` 应用属性和 `:/themes/aero/aero.qss` 一次性应用。
- 偏好设置中切换主题会立即调用 `ThemeManager::applyTheme()`，无需重启。

## 12. 国际化与资源

### 12.1 翻译

- 翻译源文件：`translations/zh_CN.ts`、`translations/en.ts`（仅提交 `.ts`）。
- 构建期由 `qt_add_lrelease()` 调用 `lrelease`（依赖 `qt6-tools-dev`）生成 `.qm`，
  再由 `qt_add_resources()` 以固定前缀 `/translations` 嵌入可执行文件；
  仓库内不保存 `.qm`。
- `update_translations` 目标直接调用 `lupdate` 扫描整个 `src/`，而不是使用
  `qt_add_lupdate()`：界面代码在各静态库中，只扫描可执行目标会把所有界面字符串
  判为过时并删除。更新后用 `ctest` 或手动检查 `.ts` 中不应残留 `unfinished`。
- 使用 `qt_add_resources()` 显式控制资源别名，而不是依赖
  `qt_add_translations()` 的隐式资源布局（Qt 6.4 与新版 Qt 的布局不同），
  因此运行时路径在所有支持的 Qt 版本上都固定为：

  ```text
  :/translations/zh_CN.qm
  :/translations/en.qm
  ```

- 翻译安装集中在 `src/app/Translations.{h,cpp}`：
  - `--lang zh_CN` 显式指定语言；
  - 系统 locale 自动匹配，支持 BCP-47 名称转换（如 `zh-CN` → `zh_CN`）；
  - 额外可选加载 Qt 自身的 `qtbase_<lang>.qm`
    （`QLibraryInfo::TranslationsPath`，Debian/Ubuntu 包名为
    `qt6-translations-l10n`），用于本地化标准对话框按钮和标准 Qt 对话框；
    缺失时不影响应用自身翻译。
- 所有用户可见文本使用 `tr()`；Qt 自带控件文本由 `qtbase_<lang>.qm` 提供。

### 12.2 资源目录

资源位于仓库根目录 `resources/`，按用途分层：

```text
resources/
├── app-icons/                 安装用应用图标（png/ico，不进资源包）
├── icons/
│   ├── classic/               经典主题 48px 字形 + 64px 文件/文件夹图标
│   ├── modern/                工具栏与导航图标、app.png、app-icon.png
│   └── file-types/            文件类型图标
│       ├── classic/           经典布局图标
│       └── modern/            现代布局图标
├── themes/
│   ├── aero/    aero.qss + 深浅共用位图 + light/ + dark/
│   └── modern/  modern.qss + 深浅共用字形 + light/ + dark/
├── svg/modern/                现代图标的 SVG 源文件（提交）
├── icons.qrc
├── themes-aero.qrc
└── themes-modern.qrc
```

- 三个清单由 `tools/update_qrc.py` 生成；**资源路径恒等于文件在 `resources/`
  下的目录路径**（例如 `resources/icons/file-types/modern/file-pdf.png` →
  `:/icons/file-types/modern/file-pdf.png`），不额外维护别名映射。
- 清单在 `resources/CMakeLists.txt` 中通过 `target_sources(QArchiver ...)`
  挂到可执行文件并设置 `AUTORCC ON`，因此**图标与样式表全部内置进可执行文件**，
  运行期不需要资源目录。可用 `nm build/src/QArchiver | grep qInitResources` 确认
  `qInitResources_icons`、`qInitResources_themes_aero`、`qInitResources_themes_modern`
  三个符号存在。
- 只有 `resources/app-icons/` 不进入资源包：它们用于安装规则、desktop 入口与
  Windows `.rc`（路径由 `cmake/QArchiverWindows.cmake` 与 `packaging/CMakeLists.txt` 引用）。
- 生成脚本：

| 目录 | 脚本 | 说明 |
|---|---|---|
| `themes/aero/light`、`themes/aero/dark` | `tools/gen_aero_assets.py` | PyQt6 绘制；`dark/` 由 `light/` 重新着色 |
| `icons/modern*`、`themes/modern/*`、`app-icons/*`、`svg/modern/*` | `tools/gen_modern_icons.py` | 需要 `rsvg-convert` |
| `icons/classic/*.png` 尺寸 | `tools/normalize_classic_icons.py` | 48/64px 归一 |
| 三个 `.qrc` | `tools/update_qrc.py` | 增删改名资源后重跑 |

- 资源自检：`python3 tools/check_resources.py` 校验清单条目存在、前缀齐全、
  `src/` 与 `resources/` 中所有 `":/..."`（含 QSS 的 `url(:/...)`）都能解析、
  所有 `Icons::aero/modern/fileType("...")` 字面量都有对应文件，以及两套主题的
  `light/` 与 `dark/` 文件名集合一致。

## 13. 测试

当前测试：

| 测试 | 覆盖范围 |
|---|---|
| `tst_utils` | 路径规范化、Zip Slip 判断、7-Zip `-slt` 流式解析、成员名空格、大小溢出检查、符号链接目标与解压树复查、旧编码检测与代码页映射、`LogLineBuffer` 的 CR/CRLF/跨块切行 |
| `tst_toolchain` | 7-Zip `--help` 校验、系统 PATH 回退、RAR Codecs 解析、`QARCHIVER_7Z_PATH` 覆盖 |
| `tst_background` | 背景图片九种对齐的字符串往返、尺寸缩放规则、目标矩形、透明度绘制、路径+mtime+大小缓存（offscreen 平台运行） |
| `tst_backends` | tar/tar.gz/tar.xz/tar.bz2/tar.zst/zip/7z/gzip 往返、纯 gz/bz2/xz 创建往返、tar.zst/zst 读取、tar 追加删除、纯 `.gz` 的 tar 探测、`.` 前缀与非 ASCII 文件名、旧编码 ZIP 的检测与选中解压（`-slt` 编码投票只采 `Path =` 行）、RAR 读取/分卷/创建能力、危险符号链接失败后的临时目录清理 |
| `tst_preview` | 连续看图的后缀筛选、同目录选取、大小写不敏感、跳过符号链接/子目录、准备对话框阈值、批量解压样例 |
| `tst_manager` | 打开/列表/解压流程、打开失败保留当前包、危险符号链接跳过/中止、覆盖已有目标压缩、RAR 分卷替换、加密 7z 回退命令行、图片同目录连续预览、后端选择开关、只读 libarchive 后端的添加/删除写回退 |
| `tst_libarchive` | zip / tar.gz / tar.xz / gz 往返、AES-256 zip 密码、加密 7z 报不支持、损坏包检测、选择性解压、目录选中、硬链接重建、不带 UTF-8 标志的 GB18030 与 Big5 名称自动解码（含落盘文件名）、显式 `encodingHint` 覆盖检测 |
| `tst_preferences` | 偏好设置中 libarchive 开关与 `AppSettings` 的双向联动、连续看图阈值与最近文件上限裁剪、布局/一键解压设置往返、性能测试对话框构建、列表模式目录索引/重复项、网格模式隐藏 `..`、压缩包/本地缩略图上下文与异步生成、缩略图开关与等比缩放、分卷图标（offscreen 平台运行） |
| `tst_titlebar` | 沉浸式标题栏与布局切换：菜单栏不拉伸（空白区可拖拽）、空白处拖拽/双击最大化、`teardownUi()`/`buildUi()` 后标题栏仍挂在 menuWidget 槽位、误调 `menuBar()` 后可自愈、标题栏颜色使用主题 token（offscreen 平台运行） |
| `tst_windowresize` | 无边框窗口缩放：八个边缘/角落命中区、窗口重建后保持置顶、最大化时禁用（offscreen 平台运行） |
| `tst_breadcrumb` | 面包屑路径框：压缩包/本地路径拆分、用“...”收纳中间层级、省略顺序、只读完整路径文本、主题化箭头分隔符（offscreen 平台运行） |
| `tst_folderpane` | 系统位置侧栏：位置路径不重复、进入子目录后仍高亮所属位置、Home 之外的目录回退到“文件系统”（offscreen 平台运行） |
| `tst_dragout` | 拖出 staging：路径剪枝、体积统计（含负值/溢出拒绝）、staging 映射、单文件/目录/嵌套选择、恶意路径/危险符号链接拦截、纯 gz 流、加密 7z 回退命令行、`QARCHIVER_NO_LIBARCHIVE` 下同样通过、准备结果缓存复用与失效 |

测试目标统一由 `tests/CMakeLists.txt` 的 `qarchiver_add_test(<name>
SOURCES ... LIBS ... DATA_DIR ...)` 注册：链接分层静态库而不是复制源文件清单，
测试数据目录通过编译定义 `QARCHIVER_TEST_DATA_DIR` 传入，`QT_QPA_PLATFORM=offscreen`
与 `QARCHIVER_7Z_PATH` 由同一个循环统一设置。新增测试只需一行注册。

各测试链接的库：

| 测试 | 链接 |
|---|---|
| `tst_toolchain` | `qarchiver_utils` |
| `tst_utils`、`tst_backends`、`tst_libarchive` | `qarchiver_core` |
| `tst_background`、`tst_preview`、`tst_manager`、`tst_dragout`、`tst_preferences`、`tst_titlebar`、`tst_windowresize`、`tst_breadcrumb`、`tst_folderpane` | `qarchiver_ui` |

`tst_libarchive` 只在找到 libarchive 时构建，因此 `-DQARCHIVER_USE_LIBARCHIVE=OFF`
的构建会运行 12 个测试而不是 13 个；其余测试在两种配置下都必须通过。

运行：

```bash
ctest --test-dir build --output-on-failure
```

`tst_preferences` 和 `tst_background` 需要 QApplication，构建脚本已为它们设置 `QT_QPA_PLATFORM=offscreen`，无显示环境也能跑。RAR 创建用例在系统没有 `rar` 时自动跳过，也可用 `QARCHIVER_RAR_PATH` 指定；tar 兼容性用例在系统没有 `tar` 时自动跳过。测试通过 `QARCHIVER_7Z_PATH` 使用内置 7-Zip。

除 CTest 外还有资源自检脚本，用于确认 qrc 清单、前缀与代码中的资源字面量一致
（不依赖 Qt 构建，可在 CI 前置运行）：

```bash
python3 tools/check_resources.py
```

`tests/test_files/` 保留中英文混排的样例文件名，这是刻意为之：用来暴露编码、
目录遍历与文件名排序问题，不要“整理”成统一命名。非 UTF-8 名称的压缩包夹具不进
仓库：相关测试在 `QTemporaryDir` 里现场拼装（`tst_libarchive` 内置极简 stored ZIP
写入器），测试结束随临时目录一起删除。

## 14. 扩展点

Windows 扩展点：

1. `cmake/QArchiverWindows.cmake` 负责图标/版本资源、部署与 MinGW 运行库；
2. `packaging/windows/` 提供右键菜单脚本、Inno Setup 安装器脚本
   （`QArchiver.iss`）与安装器构建脚本 `build-installer.ps1`；
3. `tools/build-windows-cross.sh` 演示 Debian 交叉构建与便携目录整理；
4. 新增 Windows 特有代码时优先使用 `Q_OS_WIN` 分支，并保持 Linux 构建与测试不变。

新增格式时：

1. 在 `ArchiveConstants.h` 增加 `ArchiveFormat` 和默认后缀。
2. 在 `src/common/FormatRegistry` 增加后缀/分卷/规范化规则（唯一后缀真值表）。
3. 在 `MimeTypeHelper` 补充 magic 识别。
4. 实现新的 `ArchiveBackend` 子类。
5. 在 `ArchiveBackendFactory::create()` / `createForRead()` 注册。
6. 实现能力查询和命令映射。
7. 在 `CompressDialog` 中增加格式项和选项联动。
8. 若新增后缀映射到文件类型图标，更新 `FileTypeHelper` 的查表并补充资源。
9. 补充翻译和测试（在 `tests/CMakeLists.txt` 中用 `qarchiver_add_test()` 注册）。

新增选项时：

1. 修改对应 Options 结构（`AddFilesOptions::sourceRoot` / `nameEncoding` 是
   “外部编辑写回”专用的相对路径模式，见 8.10；为其他用途扩展 Options 时
   注意不要破坏这两个字段的语义）。
2. 在对话框读取/保存。
3. 在相关后端映射为命令行参数。
4. 更新偏好设置和翻译。

核心原则：

- 所有阻塞操作留在工作线程；UI 回调只在 GUI 线程执行。
- 所有外部命令统一走 `ProcessRunner`。
- 所有解压先落到临时目录，再安全移动；移动前复核实际符号链接树。
- 所有用户路径先做安全检查；大小累计使用带溢出检查的 `qint64` 路径。
- UI 只通过 `ArchiveManager` 和后端信号交互；忙碌时禁用会改变归档状态的动作。
- 外部打开（系统查看器/文件管理器）必须检查返回值并给出可理解的失败提示。
