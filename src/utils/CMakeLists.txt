# Utility layer: process execution, paths, encoding and logging.
# 工具层：进程执行、路径、编码与日志。
#
# No widgets, no icons and no archive knowledge: the UI helpers live in ui/.
# 不含控件、图标与压缩包知识；界面相关的辅助代码位于 ui/。
add_library(qarchiver_utils STATIC
    # External process execution and archive tool discovery.
    # 外部进程执行与压缩工具查找。
    ProcessRunner.cpp
    ProcessRunner.h
    PlatformProcess.h
    SevenZipTool.cpp
    SevenZipTool.h

    # Filesystem and archive path helpers.
    # 文件系统与压缩包路径辅助函数。
    FileUtils.cpp
    FileUtils.h

    # Filename encoding detection and conversion.
    # 文件名编码检测与转换。
    EncodingDetector.cpp
    EncodingDetector.h

    # Line splitting for tool output shown in log views.
    # 日志视图中工具输出的行切分。
    LogLineBuffer.h

    # Per-user archive file associations (Windows registry / Linux mimeapps).
    # 当前用户的压缩包格式关联（Windows 注册表 / Linux mimeapps）。
    FileAssociations.cpp
    FileAssociations.h
    MimeAppsList.cpp
    MimeAppsList.h

    # MIME/archive format detection.
    # MIME/压缩格式检测。
    MimeTypeHelper.cpp
    MimeTypeHelper.h

    # Logging.
    # 日志。
    Logger.cpp
    Logger.h

    # Abnormal-exit detection: signal logging and session markers.
    # 异常退出检测：信号日志与会话标记。
    ExitGuard.cpp
    ExitGuard.h
)

# Layering: every consumer includes headers as "utils/Header.h", so the single
# include root is src/ for all QArchiver targets.
# 分层约定：所有使用方都以 "utils/Header.h" 形式包含头文件，
# 因此所有 QArchiver 目标的唯一包含根是 src/。
target_include_directories(qarchiver_utils PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(qarchiver_utils PUBLIC
    qarchiver_common
    Qt6::Core
)

if(WIN32)
    # RegCreateKeyEx / RegGetValueW and SHChangeNotify.
    # RegCreateKeyEx / RegGetValueW 与 SHChangeNotify。
    target_link_libraries(qarchiver_utils PRIVATE advapi32 shell32)
endif()

# Static libraries are linked into the (position independent) executable.
# 静态库会被链接进（位置无关的）可执行文件。
set_target_properties(qarchiver_utils PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

qarchiver_set_warnings(qarchiver_utils)
