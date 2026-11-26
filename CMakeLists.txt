cmake_minimum_required(VERSION 3.20)

# QArchiver (小Q解压) is a Qt 6 desktop archive manager.
# QArchiver（小Q解压）是一个基于 Qt 6 的桌面压缩包管理工具。
project(QArchiver
    VERSION 2.4.2
    LANGUAGES CXX
)

# Package/release revision shown to users and used for artifact names.  The
# CMake project version stays numeric (2.4.1) for compiler/resource metadata.
# 包/发行修订号用于产物命名与界面版本；CMake 项目版本保持数字形式 2.4.1，
# 供编译器与 Windows 资源使用。
set(QARCHIVER_RELEASE_VERSION "2.4.2")

# Directory below ${CMAKE_INSTALL_DATADIR} that receives README/CHANGELOG/LICENSE
# and the bundled 7-Zip license.  Debian packaging overrides this with the
# binary package name so the files land in /usr/share/doc/<package>, as Debian
# policy requires.
# 存放 README/CHANGELOG/LICENSE 与内置 7-Zip 许可证的目录（相对
# ${CMAKE_INSTALL_DATADIR}）。Debian 打包会覆盖为二进制包名，使文档位于
# /usr/share/doc/<package>，符合 Debian 政策要求。
set(QARCHIVER_DOC_DIR "doc/qarchiver" CACHE STRING
    "Documentation directory below CMAKE_INSTALL_DATADIR")

# The shared core/backends/utils layers use C++20; Qt 6.4 supports it.
# core/backends/utils 等公共层使用 C++20；Qt 6.4 支持该标准。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Minimum supported Qt version.  All find_package() calls use this value so
# Qt 6.4+ distributions (Debian 12 / Ubuntu 22.04+) can build QArchiver.
# 最低支持的 Qt 版本。所有 find_package() 都使用该值，使 Qt 6.4+ 的发行版
# （Debian 12 / Ubuntu 22.04+）也能构建 QArchiver。
set(QARCHIVER_MIN_QT_VERSION 6.4)

# Normalize the Windows target architecture.  The Visual Studio generator
# records it in CMAKE_GENERATOR_PLATFORM (-A ARM64 / x64 / Win32), while
# MinGW toolchains provide it through CMAKE_SYSTEM_PROCESSOR.
# 归一化 Windows 目标架构。Visual Studio 生成器把它记录在
# CMAKE_GENERATOR_PLATFORM（-A ARM64 / x64 / Win32），MinGW 工具链则通过
# CMAKE_SYSTEM_PROCESSOR 提供。
if(WIN32)
    if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64"
       OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
        set(QARCHIVER_WINDOWS_ARCH "arm64")
    elseif(CMAKE_GENERATOR_PLATFORM MATCHES "Win32"
           OR CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(QARCHIVER_WINDOWS_ARCH "x86")
    else()
        set(QARCHIVER_WINDOWS_ARCH "x64")
    endif()
    message(STATUS "Windows target architecture: ${QARCHIVER_WINDOWS_ARCH}")

    # Windows ZIP packages are portable: put QArchiver.exe, 7zip/, Qt DLLs and
    # the Explorer scripts directly at the archive root instead of bin/.
    # Windows ZIP 为便携包：把 exe、7zip/、Qt DLL 与右键菜单脚本直接放在压缩包根目录。
    set(CMAKE_INSTALL_BINDIR "." CACHE PATH
        "Windows portable package root" FORCE)
endif()

# Project-wide helper modules.  Feature-specific setup lives in the child
# CMakeLists.txt files; the root only orchestrates the build.
# 项目级辅助模块。具体功能配置放在各子目录的 CMakeLists.txt 中，
# 根脚本只负责组织构建流程。
include(cmake/QArchiverWarnings.cmake)
include(cmake/QArchiverBundled7zip.cmake)
include(cmake/QArchiverWindows.cmake)
include(cmake/QArchiverLibArchive.cmake)

# Select the 7-Zip helper for the target platform before source directories run.
# 在进入源码目录前，为目标平台选择要随程序分发的 7-Zip 辅助程序。
qarchiver_select_bundled_7zip()

# Detect the optional libarchive backend once at the root, so src/ and tests/
# share the same result without passing values back up.
# 在根目录探测一次可选的 libarchive 后端，使 src/ 与 tests/ 共用同一结果，
# 无需逐层回传变量。
qarchiver_find_libarchive()

# Qt setup, application target and the layer libraries are handled by src/.
# Qt 配置、应用目标与分层静态库由 src/ 负责。
add_subdirectory(src)

# Icon, glyph and style-sheet resources (compiled into the executable).
# 图标、字形与样式表资源（编译进可执行文件）。
add_subdirectory(resources)

# Translations need the Qt Linguist tools and are embedded into QArchiver.
# 翻译需要 Qt Linguist 工具，并会被嵌入 QArchiver。
add_subdirectory(translations)

# Desktop entry and application icon installation.
# 安装桌面入口和应用图标。
add_subdirectory(packaging)

# Tests are optional and find their own Qt Test dependency.
# 测试是可选的，并由测试目录自行查找 Qt Test 依赖。
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Windows packages are plain ZIP archives containing QArchiver.exe, the
# bundled 7-Zip helper and the deployed Qt runtime.
# Windows 软件包是包含 QArchiver.exe、内置 7-Zip 与部署后的 Qt 运行库的
# 普通 ZIP 压缩包。
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_NAME "QArchiver")
    set(CPACK_PACKAGE_VENDOR "QArchiver")
    set(CPACK_PACKAGE_VERSION "${QARCHIVER_RELEASE_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME
        "QArchiver-${QARCHIVER_RELEASE_VERSION}-windows-${QARCHIVER_WINDOWS_ARCH}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "QArchiver")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "QArchiver (小Q解压) archive manager for Windows")
    include(CPack)
endif()
