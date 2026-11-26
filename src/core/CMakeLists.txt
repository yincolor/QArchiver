# Core layer: settings, error types, archive manager and recent files.
# 核心层：设置、错误类型、压缩包管理器与最近文件。
add_library(qarchiver_core STATIC
    # Controller and settings.
    # 控制器与设置。
    ArchiveManager.cpp
    ArchiveManager.h

    # Opening/fallback state machine (opening backend and libarchive fallback).
    # 打开/回退状态机（正在打开的后端与 libarchive 回退）。
    OpenController.cpp
    OpenController.h

    # Worker-thread dispatch (signals, terminal handling, error/fallback).
    # 工作线程调度（信号连接、终止处理、错误/回退分发）。
    ArchiveManagerDispatch.cpp

    # Backend factory and libarchive read policy. / 后端工厂与 libarchive 读取策略。
    ArchiveBackendFactory.cpp
    ArchiveBackendFactory.h

    # Extraction security preflight. / 解压安全预检。
    ExtractionPreflight.cpp
    ExtractionPreflight.h

    # Compressed output staging. / 压缩输出暂存。
    CompressOutput.cpp
    CompressOutput.h
    AppSettings.cpp
    AppSettings.h
    RecentFilesManager.cpp
    RecentFilesManager.h
)

target_include_directories(qarchiver_core PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

# ArchiveManager runs blocking backend work on a worker thread via QtConcurrent.
# ArchiveManager 通过 QtConcurrent 在工作线程中执行阻塞式后端操作。
target_link_libraries(qarchiver_core
    PUBLIC qarchiver_common qarchiver_backends
    PRIVATE Qt6::Core Qt6::Concurrent
)

set_target_properties(qarchiver_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

qarchiver_set_warnings(qarchiver_core)
