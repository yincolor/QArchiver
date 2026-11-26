# Qt runtime dependencies for the application target.
# 应用目标所需的 Qt 运行时依赖。
find_package(Qt6 ${QARCHIVER_MIN_QT_VERSION} REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Svg)

# GNUInstallDirs provides the standard bin/lib/share install destinations.
# GNUInstallDirs 提供标准的 bin/lib/share 安装目录变量。
include(GNUInstallDirs)

# Standard Qt project defaults (AUTOMOC, AUTOUIC, output directories, ...).
# Qt 项目标准默认设置（AUTOMOC、AUTOUIC、输出目录等）。
qt_standard_project_setup(REQUIRES ${QARCHIVER_MIN_QT_VERSION})

# ---------------------------------------------------------------------------
# Target layout / 目标结构
#
# The code is built as small static libraries that mirror the source layout, so
# the dependency direction is visible in CMake and the test executables can link
# a library instead of re-listing source files:
# 代码按源码层次拆成静态库，使依赖方向在 CMake 中可见，测试可链接静态库，
# 而不必重复列出源文件：
#
#   qarchiver_common    common/     shared value types and error codes
#   qarchiver_utils     utils/      logging, process, paths, encoding (no widgets)
#   qarchiver_backends  backends/   archive backends (CLI + optional libarchive)
#   qarchiver_core      core/       manager, settings, recent files
#   qarchiver_ui        ui/         widgets, dialogs, models, themes
#
# Dependency direction: common <- utils <- backends <- core <- ui.
# 依赖方向：common <- utils <- backends <- core <- ui。
#
# The final executable only adds main.cpp and the Qt resources.
# 最终可执行文件只额外加入 main.cpp 与 Qt 资源。
# ---------------------------------------------------------------------------

qt_add_executable(QArchiver
    main.cpp
    app/CommandLine.cpp
    app/CommandLine.h
    app/Translations.cpp
    app/Translations.h
)

target_compile_definitions(QArchiver PRIVATE
    QARCHIVER_VERSION="${QARCHIVER_RELEASE_VERSION}"
)

# Windows icon, version resource, manifest and deployment rules.
# Windows 图标、版本资源、清单与部署规则。
qarchiver_add_windows_resources(QArchiver)

add_subdirectory(common)
add_subdirectory(utils)
add_subdirectory(backends)
add_subdirectory(core)
add_subdirectory(ui)

target_link_libraries(QArchiver PRIVATE qarchiver_ui)

# Include root for the application target: all library targets publish their
# own include directory, so only src/ is needed here for main.cpp.
# 应用目标的包含根：各库目标自行发布包含目录，这里仅为 main.cpp 需要 src/。
target_include_directories(QArchiver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

qarchiver_set_warnings(QArchiver)
qarchiver_bundle_7zip(QArchiver)

install(TARGETS QArchiver
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Linux desktop entry and launcher icons are installed by packaging/ so the
# installed desktop file keeps the Debian package name.
# Linux 桌面入口与启动器图标由 packaging/ 安装，使安装后的 desktop 文件名
# 与 Debian 包名保持一致。

# Explorer context-menu extension (dynamic menu labels / archive exclusion).
# 资源管理器右键菜单扩展（动态文案 / 排除压缩包）。
if(WIN32)
    add_library(QArchiverShell SHARED
        windows/QArchiverShell.cpp
        windows/QArchiverShell.def
    )
    target_compile_definitions(QArchiverShell PRIVATE
        UNICODE _UNICODE _WIN32_WINNT=0x0601 WINVER=0x0601
    )
    target_link_libraries(QArchiverShell PRIVATE
        ole32 shell32 shlwapi advapi32 uuid
    )
    set_target_properties(QArchiverShell PROPERTIES
        PREFIX ""
        OUTPUT_NAME "QArchiverShell"
    )
    if(MSVC)
        # Menu labels contain Chinese text; tell MSVC the source is UTF-8.
        # 菜单文案包含中文；告知 MSVC 源文件使用 UTF-8。
        target_compile_options(QArchiverShell PRIVATE /utf-8)
    endif()
    if(MINGW)
        # Explorer loads the in-proc server from its own process; do not depend
        # on MinGW runtime DLLs that live next to QArchiver.exe.
        # 资源管理器在自己的进程中加载该 DLL，不能依赖 QArchiver.exe 旁的
        # MinGW 运行库。
        target_link_options(QArchiverShell PRIVATE
            -static -static-libgcc -static-libstdc++
        )
    endif()
    qarchiver_set_warnings(QArchiverShell)
    install(TARGETS QArchiverShell
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(FILES
    "${PROJECT_SOURCE_DIR}/README.md"
    "${PROJECT_SOURCE_DIR}/CHANGELOG.md"
    "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/${QARCHIVER_DOC_DIR}
)

qarchiver_configure_windows_deploy(QArchiver)

# Install the bundled 7-Zip binaries and their license next to the app target.
# 安装随程序分发的 7-Zip 二进制及其许可证。
qarchiver_install_7zip()
