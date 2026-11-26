# Changelog

All notable changes to QArchiver (小Q解压) are documented here.
格式参考 Keep a Changelog；版本号遵循 Semantic Versioning。

## [Unreleased]

作者：shenmo <shenmo@spark-app.store>

### Added / 新增

- 新增 `.iso` 光盘镜像读取（只读）：支持 ISO9660（Joliet、Rock Ridge）与 UDF；
  优先使用进程内 libarchive，libarchive 无法处理的 UDF 镜像自动回退内置 7-Zip。
  格式识别使用偏移 `0x8001` 的 `CD001`/`BEA01` 签名，改错后缀的镜像同样能打开；
  打开对话框、文件关联、桌面条目与文件类型图标同步支持 `.iso`。
- 新增“压缩包内编辑并写回”：双击条目仍用系统默认程序打开，用户保存后
  QArchiver 弹出“保存到压缩包 / 不保存”确认框；确认后 7-Zip 就地替换同名成员
  （`AddFilesOptions::sourceRoot` + 相对路径），取消则不改动压缩包。使用轮询
  + 稳定延迟检测外部修改，内容未变时静默忽略；旧式 GBK/Big5/Shift-JIS 成员名
  按原始字节暂存以保证替换而不是追加副本。仅对可写格式（zip / 7z / 未压缩
  tar）生效，可在偏好设置的预览页关闭（`preview/externalEditWatch`）。
- 经典与现代布局共用 Windows 7 风格面包屑路径框：压缩包根名与内部每一级目录、
  本地盘符/根与每一级文件夹均可点击跳转；层级之间显示跟随主题的箭头分隔符。
- 路径过长时用“...”按钮收纳中间层级，并可在弹出菜单中直接跳转；继续缩小时
  先省略首级、最后省略末级。单击路径框右侧空白区切换为只读完整路径，可选中复制。
- 无边框窗口四边与四角均可拖拽缩放（`WindowResizeBorders`），保留右下角
  `QSizeGrip`。
- 本地文件浏览支持图片缩略图：本地图片直接由 `ThumbnailManager` 异步读取并
  复用压缩包缩略图的持久缓存；现代布局各视图与经典网格同样显示，经典列表/树
  仍保持普通图标。

### Changed / 变更

- Debian 包名改为 `yincolor.qarchiver`（符合星火/UOS 包名规范），桌面入口随之
  安装为 `yincolor.qarchiver.desktop`（规范要求 desktop 文件名与包名一致），
  版本号采用 `2.4.1-1`（source format 改为 `3.0 (quilt)`），并声明
  `Conflicts`/`Replaces: qarchiver` 以便旧安装平滑升级。
- Linux deb 改为在 Debian 12（bookworm）基线构建（Qt 6.4 / glibc 2.36 /
  t64 改名前的库名），产物可装到 Debian 12/13、Ubuntu 24.04、Linux Mint 22
  与 Deepin 23+；CI 同时在 Debian 12 / Debian 13 / Ubuntu 24.04 中验证依赖
  可满足性与安装。
- 经典布局搜索框固定为 280px，与现代布局一致。
- 路径显示由静态省略标签改为可点击的面包屑层级。
- 经典布局不再显示“无压缩包”占位页作为起始页：启动、从现代布局切换过来以及
  选择“文件 → 起始页”时，只要没有打开压缩包就直接进入本地文件浏览；命令行启动
  即带压缩包或外壳任务时不会预先扫描本地目录。现代布局仍以欢迎页为起始页。
- 打开/回退状态机从 `ArchiveManager` 抽成 `OpenController`：管理器只持有最终
  后端，“正在打开”的后端由控制器独占，并在打开、回退、出错与关闭处校验状态
  不变量；打开失败仍保留原压缩包。新增 `tst_manager` 现有用例继续覆盖回退、
  写回重开与失败保留。
- `MainWindow` 继续按职责拆分：`MainWindowMenu.cpp` 负责动作/菜单构建，
  `MainWindowActions.cpp` 只留命令处理与状态更新；布局相关函数归入
  `MainWindowLayout.cpp`；一键解压的目标规划与状态抽成
  `OneClickExtractController`，新增 `tests/tst_oneclick.cpp`。
- 压缩包缩略图按每批最多 64 个成员解压；切换压缩包/目录时会对已运行的批次
  请求取消，不再把旧上下文的图片全部解压完。
- 本地目录扫描期间在状态栏显示“正在读取 …”，超大目录不再看起来像卡住。

### Fixed / 修复

- 修复在压缩包内保存外部修改后，路径框一直显示“正在打开 <压缩包>...”（首次保存
  必现）的问题：为执行写操作而重开只读 libarchive 后端属于内部细节，既不再发出
  `archiveOpening`，列表就绪后两种布局也都会刷新路径框（此前只刷新现代布局，经典
  布局的面包屑会停在占位文本上）。新增 `tests/tst_navstate.cpp` 回归测试。
- 修复 `.svg` 图片在本地浏览和压缩包内都不显示缩略图的问题：缩略图渲染改用
  `QSvgRenderer`（链接 `Qt6::Svg`），不再依赖 `QImageReader` 的 `qsvg` 图片格式
  插件；同时缩放至缩略图框内并保持宽高比。新增 SVG 缩略图回归测试。
- 修复无边框主窗口启动时被窗口管理器放到屏幕右下角等任意位置的问题：首次显示
  时在主屏可用区域内居中，之后保留用户手动移动的位置。

## [2.4.2] - 2026-11-26

作者：shenmo <shenmo@spark-app.store>

### Added / 新增

- 帮助菜单新增“检查更新...”，从上游 GitHub Releases Atom 源检查新版本；
  可显示版本号、Release 说明并打开下载页面。
- 自动识别 `v2.4.2-1` 这类带打包修订号的版本。

## [2.4.1] - 2026-11-24

作者：shenmo <shenmo@spark-app.store>

### Fixed / 修复

- 修复非 UTF-8（GBK/GB18030/Big5）文件名乱码：libarchive 后端不再用系统
  locale 解码原始字节（`fromLocal8Bit`），改为按整个压缩包一次性检测编码后自行
  解码，列表与解压落盘的文件名因此始终一致；同时不再向 libarchive 传
  `hdrcharset`（该选项按 locale 转换，非 UTF-8 locale 下会整包失败）。
- 文件名编码检测改为“可信度排序”：合法 UTF-8 始终最优先，其余按落入该编码
  常用字区的两字节代码单元比例排序，系统 locale 顺序仅用于近似平票裁决。
  Big5 名称不再在大陆/英文/日文机器上被误判成 GB18030，GBK 名称在繁体机器上
  也能正确识别；Shift-JIS 通过假名区域可靠区分。
- 7-Zip / tar CLI 后端的编码投票只采成员名行（`-slt` 的 `Path =`），不再让
  横幅、时间戳和压缩包自身路径干扰判断。
- 检测到的文件名编码通过 `ArchiveBackend::nameEncoding()` 回传给解压与拖出，
  修复 `ListOptions/ExtractOptions.encodingHint` 此前基本无人填写的问题。
- 修复资源管理器右键菜单中“添加到 xx.zip / 压缩到...”所有格式都生成 ZIP
  的问题：命令 ID 改为从 `lpVerb` 读取，不再误用 `lpVerbW`。
- 右键菜单恢复文件类型图标；压缩完成后自动在 QArchiver 主窗口打开生成的
  压缩包。
- 安装器改用带版本号的 `QArchiverShell-<version>.dll`，覆盖升级不再因
  Explorer 占用旧 DLL 而失效；升级时尽量清理旧副本，卸载时清理全部副本。
- 压缩日志过滤空白进度行，并立即显示创建目标和进度，避免日志滚动但内容为空。
- 性能测试线程数改为下拉框；修复日志读取通道；增加运行心跳、完整结果输出
  和结束提示。
- 修复资源管理器右键“压缩到”完成后在 `operationFinished` 中**同步**重入
  `ArchiveManager`（后端清理尚未执行）的时序问题：现在推迟到下一个事件循环再
  打开新压缩包，并补上 `m_lastCompressTarget` 清理与状态栏刷新。
- 右键菜单多选压缩时不再为每个文件各弹一个主窗口（仅单文件时带 `--open-after`）。
- 压缩进度百分比改为在统计行显示，不再把握手当前文件名的标签覆盖成
  “正在压缩... 40%”。
- 性能测试对话框：stdout 与 stderr 分开缓冲（不再把两条流的半行粘在一起）、
  正确处理 `\r` 进度行、心跳改为按间隔触发。
- 安装脚本：Inno Setup 版本号补足四段（`2.4.1` → `2.4.1.0`）；安装 Shell 扩展时
  按解析后的版本号而非文件名字典序选 DLL（否则 `2.4.9` 会排在 `2.4.10` 之前）。
- 修复 `tests/CMakeLists.txt` 中文注释乱码。

## [2.4.0-2] - 2026-11-24

作者：shenmo <shenmo@spark-app.store>

### Added / 新增

- 本地文件管理使用系统位置侧栏（主目录、桌面、文档、下载、图片、音乐、
  视频、文件系统及用户挂载卷），现代布局在“文件管理”中显示，经典布局不显示。
- 网格单元格高度按当前目录最长文件名动态计算，长文件名（包括选中状态）
  完整换行显示，不再被固定单元格高度截断。

### Changed / 变更

- modern 文件类型图标：`file-zip`、`file-rar` 与 `file-7z` 统一改为
  居中格式文字标识，移除拉链/圆点图案，保留各自页眉配色。
- GitHub Actions 的 Linux deb 改为在 Debian 13 容器中构建，并在干净的
  Debian 13 环境中验证安装（动态库依赖、内置 7-Zip 与桌面入口），修复
  Ubuntu 24.04 产物在 Debian/Deepin 上依赖不匹配的问题。
- Linux 安装包补充 `.desktop` 桌面入口与 hicolor 启动器图标，任务栏/启动器
  不再回退到通用图标。

### Fixed / 修复

- 修复点击系统位置后侧栏位置列表被清空的问题；浏览子目录时高亮其所属位置，
  并对标准目录/挂载卷去重。
- 修复缩略图完成时反复全量重算网格高度造成的卡顿：仅显示文本变化触发重算，
  批量模型变化合并为一次扫描，网格隐藏时不再空转。
- 修复“文件 → 起始页”在有压缩包时清空侧栏且无法回到压缩包的问题：现在会先
  关闭当前压缩包再显示起始页。
- 系统位置列表在窗口重新显示或打开主目录等具体位置之外的目录时限频刷新挂载卷，
  避免每次导航都读取（可能较慢的）网络挂载信息。
- 网格单元格高度响应字体、应用字体与屏幕 DPI 变化。

## [2.4.0] - 2026-11-24

作者：shenmo <shenmo@spark-app.store>

### Added / 新增

- 沉浸式无边框标题栏：应用图标、菜单、最小化/最大化/关闭按钮、拖动、
  双击最大化与右下角尺寸调整；现代/经典布局共用。
- 欢迎页新增“文件管理”按钮，启动默认停留在欢迎页；保留左侧文件夹侧栏，
  内容按正常内容区居中。
- 合并上游 yincolor/QArchiver 的 2.3.1 修复、经典样式路径框、文件类型图标
  目录整理、MIME 关联和预览逻辑修复。

### Changed / 变更

- 默认启动行为从“直接进入本地文件浏览”改为“欢迎页”。
- 经典布局菜单集成到沉浸式标题栏中。

## [2.3.1] - 2026-11-24

作者：shenmo <shenmo@spark-app.store>

### Added / 新增

- 支持读取 `.tar.zst` / `.tzst` / `.zst` 归档（内置 7-Zip，只读）。

### Fixed / 修复

- 修复条目大小设置的延迟持久化：拖动滑块/Ctrl+滚轮停止 400ms 后再写入设置，关闭窗口时强制落盘。
- 修复连续预览解压进行中再次点击“Open With...”时，打开模式被先启动的请求覆盖的问题。
- “Open With...”仅在 Windows 显示；Windows 下打开方式启动失败会提示，Linux 不再显示行为不符的菜单项。
- 恢复 `ui/itemSize` 默认值为 96，与现有文档和经典布局紧凑尺寸保持一致。
- 排序按钮改为标准双向排序图标（不再显示单个向上箭头），图标正确居中。
- 条目大小滑块移动到窗口右下角状态栏；Ctrl + 滚轮缩放保留。
- 文件夹图标改为更大、近方形的 256px PNG，并提供 SVG 源文件；图片缩略图
  按原比例居中绘制，不再被拉伸成方形。
- 现代布局默认字体提升到 10.5pt，网格单元收紧到 128x152 级别，图标与
  文字占单元格约 80%；左侧栏改为更深的灰蓝色，与工具栏/内容区区分。
- Windows 安装器语言检测使用系统区域设置，中文系统默认选中简体中文。
- 安装版与便携版继续放在同一个 Release 中（不再拆分发布）。
- 更新 `WINDOWS_ISSUES.md` 的当前状态：Windows 注册表、安装器与便携脚本问题仍未修复，待 Windows 实机验证。

## [2.3.0] - 2026-11-23

作者：shenmo <shenmo@spark-app.store>

### Added / 新增

- 资源管理器右键菜单改为进程内扩展（`QArchiverShell.dll`）：文案动态显示
  “添加到 xx.zip”，压缩包文件不再出现“添加到压缩文件”，格式子菜单动态生成。
- Windows 安装器增加“关联压缩包格式”任务（默认选中）；偏好设置/工具菜单
  新增文件关联管理工具，Windows 写入当前用户 ProgID，Linux 使用 xdg-mime /
  mimeapps.list。
- 从右键菜单启动压缩/解压时只显示对话框（选项/进度），不再弹出主窗口。
- 安装版与便携版在同一个 Release 中发布：同时包含 Windows setup.exe、
  Linux deb 与 Windows 绿色版 ZIP。
- 缩略图保持原始比例（不再裁剪成正方形）；通过 Qt imageformats 模块支持
  WebP/TIFF 等格式。
- 现代布局：排序按钮图标修复；导航栏增加图标大小滑块，Ctrl + 滚轮可缩放；
  默认字体提升到 10pt，网格图标与文字填充更大比例的单元格；左侧栏与内容区
  配色区分更明显。
- 选中条目后点击“一键解压”只解压所选条目；未选择时仍解压整个压缩包。
- Windows 安装器提供简体中文/English 双语界面，并默认显示语言选择。

### Fixed / 修复

- Windows 压缩/解压进度与日志不显示：7-Zip 增加 `-bsp1`，并按 `\r`/`\n`
  解析进度输出。
- 通用 Windows 安装器的关联注册、右键菜单注册与卸载清理。

## [2.2.0] - 2026-11-23

作者：shenmo <shenmo@spark-app.store>

### Added / 新增

- Windows 安装器（Inno Setup），除便携 ZIP 外提供开始菜单、桌面快捷方式、
  卸载入口以及可选的资源管理器集成；安装器与便携脚本使用同一套右键菜单。
- 资源管理器右键菜单：文件和文件夹增加“添加到同名 ZIP 压缩包”，以及
  “压缩到...”二级菜单（7z、ZIP、tar.gz、tar.xz、tar.bz2、tar）和
  “自定义压缩选项”；文件夹空白处可压缩当前文件夹。
- 未打开压缩包时浏览本地目录（Windows 与 Linux）：支持目录导航、前进/后退、
  搜索、网格/列表/树形视图、双击打开文件或进入目录、右键压缩与排序。
- 排序菜单：右键空白处或点击视图旁的小按钮，可按键名/大小/类型/修改时间
  升序或降序排列，并记住上次选择（网格视图也可排序）。
- 命令行新增 `--compress <格式>`、`--output <路径>`、`--compress-dialog`，
  供右键菜单与脚本直接创建压缩包。

### Fixed / 修复

- Windows 下连续看图/图片预览提示“另一个程序正在使用此文件”：预览缓存
  改为按单个文件复用，不再覆盖系统图片查看器已占用的图片；缓存标识加入
  压缩包大小与修改时间；补充 `.jpeg` 等图片后缀；解压移动文件时对 Windows
  临时占用进行短暂重试；退出时不再删除仍被查看器打开的预览缓存。
- Windows 默认字体回退到宋体的问题：优先使用微软雅黑 UI / 微软雅黑，
  中文界面不再以宋体渲染。

## [2.1.1] - 2026-11-23

作者：shenmo <shenmo@spark-app.store>

### Added / 新增

- 现代网格/列表视图中的归档图片显示实际缩略图：后台按需解压到缩略图缓存，
  不阻塞界面；加载完成后自动刷新对应图标。
- 缩略图写入持久缓存（`QStandardPaths::CacheLocation/thumbnails`），再次打开
  同一压缩包时直接复用；加密图片在获得密码后自动重试。
- 双击图片仍可打开系统图片查看器进行连续看图。
- 合并上游项目结构整理：资源、主题、图标、模型、服务与主窗口实现按职责拆分，
  由分层静态库组织；本版本保持 2.1.1 版本号。

### Fixed / 修复

- 缩略图线程使用独立读取后端，不干扰主窗口的列表/解压操作。

## [0.1.2] - 2026-11-23

作者：shenmo <shenmo@spark-app.store>

### Added / 新增

- `resources/svg/modern/`：现代图标的 SVG 源文件，便于重新生成与审阅。
- `tools/update_qrc.py`：按目录结构生成 `icons.qrc`、`themes-aero.qrc`、
  `themes-modern.qrc`。
- `tools/check_resources.py`：校验 qrc 清单、资源前缀与代码中的资源字面量。
- `update_translations` 现在直接调用 `lupdate` 扫描整个 `src/`（此前经
  `qt_add_lupdate()` 只扫描可执行目标，会把所有界面字符串判为过时并删除）。
- `docs/design.zh_CN.md`：按新结构重写的设计文档（根目录的
  `项目设计文档.md` 改为指向它的索引）。
- 现代好压（HaoZip）风格默认界面：扁平渐变头部、大号彩色工具栏、
  文件夹面板、详细信息面板、路径导航与包内搜索、归档状态摘要。
- 现代起始页：拖放区、打开/一键解压入口与最近打开列表。
- 归档内容新增文件管理器式网格视图，并设为默认；导航栏在内容列表上方
  提供网格/列表/树形切换，偏好设置中也可切换。
- 更多常见文件类型图标：字体、磁盘映像、电子书、数据库、种子、设计稿、
  字幕、3D/CAD、安装包、演示文稿等；网格使用 64px 源图保证清晰。
- 压缩格式新增 `.gz`、`.bz2`、`.xz` 单文件压缩流，创建对话框会校验单文件限制。
- 文件选择改用系统原生对话框；压缩源先选择“文件”或“文件夹”，再调用系统对话框。
- 现代外观增加清晰的分区边界：菜单栏/头部/导航/文件夹/内容/详情/状态栏
  使用不同底色与边框；设置、关于按钮与左侧工具按钮统一为大按钮。
- Linux 安装图标提供 32/48/64/128/256/512 多尺寸和 pixmaps 兜底，
  desktop 文件增加 `StartupWMClass`，并通过 `setDesktopFileName` 关联窗口。
- 一键解压首次提示中“解压后关闭”使用强调色并作为默认按钮。
- 现代导航栏的网格/列表/树形切换图标重新绘制并居中；网格视图的文件夹图标居中。
- 网格视图文件名改用多行排版，长文件名可在单元格内完整换行显示。
- 修复并行全新构建中翻译 qrc 可能先于 lrelease 执行、导致嵌入 0 字节 .qm 的问题。
- 一键解压：自动解压到以压缩包命名的独立文件夹，目录冲突时使用
  `名称 (1)` 唯一名称；支持首次提示和“解压完成后关闭程序”。
- 设置中的 7-Zip 性能测试（`7z b`），实时输出、线程数选择、停止与复制结果；
  同时提供“工具 → 7-Zip 性能测试”入口。
- 界面布局切换：现代布局与原有经典布局可在“偏好设置 → 界面”中即时切换。
- Windows 支持：
  - x64 与 ARM64 的 MinGW/MSVC 构建、`.rc` 图标与版本资源、
    Per-Monitor V2 DPI 清单；
  - Win32 文件名编码转换（不再依赖 iconv）；
  - 启动 7z/rar 时隐藏控制台窗口；
  - 跨平台进程存活检测与预览/拖出缓存清理；
  - `windeployqt` 部署规则与 CPack ZIP 打包；ARM64 包内置原生 ARM64 的
    7-Zip 辅助程序（`third_party/7zip/win/arm64/`）；
  - 可选的资源管理器右键菜单安装/卸载 PowerShell 脚本。
- GitHub Actions：
  - `build.yml` 可复用构建矩阵：Linux amd64/arm64 原生 deb、Windows x64
    原生构建与测试、Windows ARM64 交叉构建；
  - `ci.yml` 在 push / PR 上自动构建测试；
  - `release.yml` 在推送 `v*` 标签时构建四种产物、生成 `SHA256SUMS` 并创建
    GitHub Release。
- Debian 打包：`debian/control`、`debian/rules`、changelog、copyright；
  可用 `mk-build-deps` 安装构建依赖。
- 命令行：`QArchiver <archive>`、`--oneclick <archive>`、`--preferences`、
  `--benchmark`、`--lang`。

### Changed / 变更

- 源码按层重构为静态库：`common`（格式表/错误/数据结构）、`utils`、`backends`、
  `core`、`ui`，依赖只能自上而下，消除了 `core → backends → utils → core`
  的循环依赖；`main.cpp` 精简为启动流程，命令行解析与翻译安装移入 `app/`。
- 拆分为多个编译单元以降低单文件体积：`ArchiveManager` 拆出
  `ArchiveBackendFactory`、`ExtractionPreflight`、`CompressOutput`、
  `ArchiveManagerDispatch`；`MainWindow` 按职责拆为动作/布局/导航/解压/拖出/
  公共工具六个实现文件。
- 统一图标与主题资源：全部移到仓库根目录的 `resources/`，按用途分为
  `icons/classic`、`icons/modern`、`icons/file-types`、`themes/aero`、
  `themes/modern`；两套主题的 `light/` 与 `dark/` 使用相同文件名，深浅共用
  的字形放在主题根目录；资源路径只在 `Icons::` 与 `ThemeManager` 中拼接，
  资源路径恒等于文件在 `resources/` 下的路径。
- 格式后缀与类型判断统一到 `FormatRegistry`（`MimeTypeHelper`、`FileTypeHelper`、
  `CompressDialog`、一键解压均改为查表），`backends/zip` 合并进 `backends/sevenzip`，
  `backends/archive` 更名为 `backends/libarchive`。
- 全部静态库 `POSITION_INDEPENDENT_CODE ON`，libarchive 探测移入
  `cmake/QArchiverLibArchive.cmake`；测试统一通过 `qarchiver_add_test()` 注册。
- 图标归一：经典图标工具栏 48px、列表图标 64px；新增 `tools/normalize_classic_icons.py`。
- 偏好设置的“界面”页按当前布局隐藏无效选项：现代布局隐藏“工具栏样式”和
  “显示最近文件侧边栏”，经典布局隐藏“归档内容视图”（经典布局没有视图切换入口，
  固定使用树视图）；“最近文件上限”与背景图片在两种布局下都保留。
  切换布局下拉框时立即更新，无需保存后重开对话框。
- 默认界面由 Aero/Windows 7 风格改为现代扁平风格；经典布局与 Aero 主题仍保留。
- 归档内容默认视图由树形改为网格；已有显式列表/树形设置会保留。
- 文件列表新增“类型”列，现代布局隐藏旧“加密”列；现代布局使用彩色文件类型图标。
- 只读 libarchive 后端上的 zip/7z/未压缩 tar 添加/删除会自动切换到命令行后端重放，
  不再直接禁用“添加/删除”。
- 压缩包内无压缩后大小时显示 `-`，状态栏不再显示误导性的 100% 压缩比。

### Fixed / 修复

- `-DQARCHIVER_USE_LIBARCHIVE=OFF` 时测试编译失败：`ArchiveBackendFactory.h`
  的 include 被放进了 `#ifdef QARCHIVER_HAS_LIBARCHIVE` 内部。
- 资源自检脚本漏检两类问题：`Icons::` 调用字面量与 QSS 中的 `url(:/...)`；
  另新增两套主题 `light/`/`dark/` 文件名一致性与资源路径存在性校验。
- `MimeTypeHelper::resolveStreamFormat()` 改用 `FormatRegistry` 判断复合 tar 后缀，
  不再维护第四份后缀表。
- 修正 tar/7z 在用户跳过危险符号链接时仍可能解出该链接的问题：
  归档根目录 `.` 条目会让 7-Zip 展开整棵树，现已从成员列表中排除。
- 一键解压对 `.7z.001` / `.part1.rar` 等分卷使用正确的目标文件夹名。
- 只读后端写回退期间保留当前列表，取消或失败后导航/摘要不会停留在“正在打开”。
- 修正后台取色/透明度测试在不同渲染后端上的舍入误差。
- 清理 mk-build-deps 遗留文件与 Qt 5 时代文档描述。

[0.1.3]: https://github.com/shenmo7192/QArchiver/releases/tag/v0.1.3
[0.1.2]: https://github.com/shenmo7192/QArchiver/releases/tag/v0.1.2
[2.1.1]: https://github.com/shenmo7192/QArchiver/releases/tag/v2.1.1
[2.2.0]: https://github.com/shenmo7192/QArchiver/releases/tag/v2.2.0
[2.3.0]: https://github.com/shenmo7192/QArchiver/releases/tag/v2.3.0
[2.3.1]: https://github.com/shenmo7192/QArchiver/releases/tag/v2.3.1
[2.4.0-2]: https://github.com/shenmo7192/QArchiver/releases/tag/2.4.0-2
[2.4.0]: https://github.com/shenmo7192/QArchiver/releases/tag/v2.4.0
[2.4.1]: https://github.com/shenmo7192/QArchiver/releases/tag/v2.4.1
[2.4.2]: https://github.com/shenmo7192/QArchiver/releases/tag/v2.4.2
