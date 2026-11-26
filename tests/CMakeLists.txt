# Test suite: unit and integration tests for the core, backends and UI wiring.
# 测试套件：覆盖核心层、后端与 UI 连接方式的单元及集成测试。
#
# Every test links one of the layer libraries built by src/ instead of listing
# source files, so adding a source file never requires touching this file.
# 每个测试都链接 src/ 构建的分层静态库，而不再罗列源文件，
# 因此新增源文件无需修改本文件。

# Qt Test is optional; the application itself does not require it.
# Qt Test 是可选依赖；应用本身不需要它。
find_package(Qt6 ${QARCHIVER_MIN_QT_VERSION} QUIET COMPONENTS Core Gui Widgets Concurrent Svg Test Network)
if(NOT TARGET Qt6::Test)
    message(STATUS "Qt6::Test not found: skipping QArchiver tests")
    return()
endif()

# Enable moc/automoc for the standalone test executables.
# 为独立测试可执行文件启用 moc/automoc。
set(CMAKE_AUTOMOC ON)

# Helper: declare a Qt test executable and register it with CTest.
# 辅助函数：声明 Qt 测试可执行文件并注册到 CTest。
#
# Tests that use QTEST_MAIN (i.e. need a QApplication) must also set
# ENVIRONMENT "QT_QPA_PLATFORM=offscreen", otherwise they fail on headless
# CI runners because the xcb plugin cannot connect to a display.
# 使用 QTEST_MAIN（即需要 QApplication）的测试必须同时设置
# ENVIRONMENT "QT_QPA_PLATFORM=offscreen"，否则在无显示器的 CI 上会因
# xcb 插件连不上 display 而失败。
#
#   qarchiver_add_test(tst_name
#       SOURCES  tst_name.cpp
#       LIBS     qarchiver_core          # optional
#       DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test_files   # optional
#   )
function(qarchiver_add_test name)
    cmake_parse_arguments(TEST "" "DATA_DIR" "SOURCES;LIBS" ${ARGN})

    qt_add_executable(${name} ${TEST_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${TEST_LIBS})

    if(TEST_DATA_DIR)
        target_compile_definitions(${name} PRIVATE
            QARCHIVER_TEST_DATA_DIR="${TEST_DATA_DIR}")
    endif()

    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Utility tests: path normalization, traversal checks and -slt parsing.
# 工具测试：路径规范化、路径穿越检查与 -slt 解析。
qarchiver_add_test(tst_utils
    SOURCES tst_utils.cpp
    LIBS qarchiver_core
)

# One-click extraction planning: destination naming, split volumes and state.
# 一键解压规划：目标命名、分卷与状态机。
qarchiver_add_test(tst_oneclick
    SOURCES tst_oneclick.cpp
    LIBS qarchiver_ui
)

# Toolchain discovery: --help validation, system fallback and RAR codec probe.
# 工具链发现：--help 校验、系统回退与 RAR 编解码器探测。
qarchiver_add_test(tst_toolchain
    SOURCES tst_toolchain.cpp
    LIBS qarchiver_utils
)

# Background image helpers: alignment, scaling, target rectangles and painting.
# 背景图片辅助函数：对齐、缩放、目标矩形与绘制。
qarchiver_add_test(tst_background
    SOURCES tst_background.cpp
    LIBS qarchiver_ui
)
set_tests_properties(tst_background PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Backend tests: tar/zip/7z/RAR/stream round-trips and special names.
# 后端测试：tar/zip/7z/RAR/流往返及特殊文件名。
qarchiver_add_test(tst_backends
    SOURCES tst_backends.cpp
    LIBS qarchiver_core
    DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test_files
)

# External-edit write-back: change detection, staging and member replacement.
# 外部编辑写回：修改检测、暂存与成员替换。
qarchiver_add_test(tst_externaledit
    SOURCES tst_externaledit.cpp
    LIBS qarchiver_ui
)
set_tests_properties(tst_externaledit PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Location bar state after a write-through reopen (external-edit write-back).
# 只读后端写回退（外部编辑写回）后的路径框状态。
qarchiver_add_test(tst_navstate
    SOURCES tst_navstate.cpp
    LIBS qarchiver_ui
)
set_tests_properties(tst_navstate PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Continuous image preview: same-directory image selection and batch extraction.
# 连续看图：同目录图片选取与批量解压。
qarchiver_add_test(tst_preview
    SOURCES tst_preview.cpp
    LIBS qarchiver_ui
    DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test_files
)

# ArchiveManager tests: async open/list/extract, fallback and preferences.
# ArchiveManager 测试：异步打开/列表/解压、回退与偏好设置。
qarchiver_add_test(tst_manager
    SOURCES tst_manager.cpp
    LIBS qarchiver_ui
    DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test_files
)

# libarchive backend: only built when the system library was found.
# libarchive 后端：仅当找到系统库时构建。
if(QARCHIVER_HAS_LIBARCHIVE)
    qarchiver_add_test(tst_libarchive
        SOURCES tst_libarchive.cpp
        LIBS qarchiver_core
    )
endif()

# Drag-out staging (DragOutManager): needs the manager, AppSettings and the
# backends, but no widgets - it runs headless.
# 拖出暂存（DragOutManager）：需要管理器、AppSettings 和后端，
# 但不依赖控件，可无界面运行。
qarchiver_add_test(tst_dragout
    SOURCES tst_dragout.cpp
    LIBS qarchiver_ui
)

# Preferences and password dialog tests (offscreen QPA).
# 偏好设置与密码对话框测试（离屏 QPA）。
qarchiver_add_test(tst_preferences
    SOURCES tst_preferences.cpp
    LIBS qarchiver_ui
)
set_tests_properties(tst_preferences PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Immersive title bar and layout switching: drag area in the empty menu-bar
# space, title bar survival across teardown/build, and token-driven colours.
# 沉浸式标题栏与布局切换：菜单栏空白处的拖拽区域、重建后标题栏是否保留、
# 以及基于 token 的配色。
qarchiver_add_test(tst_titlebar
    SOURCES tst_titlebar.cpp
    LIBS qarchiver_ui
)
target_compile_definitions(tst_titlebar PRIVATE
    QARCHIVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_tests_properties(tst_titlebar PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Frameless window resize: eight edge/corner hit zones, z-order above newly
# built UI children, and disabling while maximized.
# 无边框窗口缩放：八个边缘/角落命中区、新界面控件之上保持置顶、最大化时禁用。
qarchiver_add_test(tst_windowresize
    SOURCES tst_windowresize.cpp
    LIBS qarchiver_ui
)
set_tests_properties(tst_windowresize PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Breadcrumb location bar: archive/local segment splitting, middle-level
# hiding via the overflow menu, elision order and the selectable text mode.
# 面包屑路径框：压缩包/本地路径拆分、用 "..." 收纳中间层级、省略顺序以及
# 可选中复制的文本模式。
qarchiver_add_test(tst_breadcrumb
    SOURCES tst_breadcrumb.cpp
    LIBS qarchiver_ui
)
set_tests_properties(tst_breadcrumb PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# System-places sidebar: no duplicate paths, ancestor highlighting for
# subdirectories, and the file-system fallback outside Home.
# 系统位置侧栏：路径不重复、子目录高亮所属位置、Home 之外回退到文件系统。
qarchiver_add_test(tst_folderpane
    SOURCES tst_folderpane.cpp
    LIBS qarchiver_ui
)
set_tests_properties(tst_folderpane PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Make the bundled 7-Zip helper visible to every test that spawns a CLI
# backend.  QARCHIVER_7Z_PATH is the first entry in
# ProcessRunner::resolveToolPath(), so tests use exactly the binary that ships
# with QArchiver even when no system 7z is installed.
# 让每个会启动 CLI 后端的测试都能找到随程序分发的 7-Zip 辅助程序。
# QARCHIVER_7Z_PATH 是 ProcessRunner::resolveToolPath() 的第一优先级，
# 因此即使系统未安装 7z，测试也会使用 QArchiver 自带的二进制。
if(QARCHIVER_BUNDLE_7ZIP)
    set(_qarchiver_test_7z_env "QARCHIVER_7Z_PATH=${QARCHIVER_7ZIP_EXE}")
    foreach(_qarchiver_test IN ITEMS
            tst_utils tst_toolchain tst_background tst_backends tst_preview
            tst_manager tst_libarchive tst_dragout tst_externaledit
            tst_navstate tst_preferences tst_titlebar tst_folderpane)
        if(TEST ${_qarchiver_test})
            set_tests_properties(${_qarchiver_test} PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;${_qarchiver_test_7z_env}")
        endif()
    endforeach()
endif()
