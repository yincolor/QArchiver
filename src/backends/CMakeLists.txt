# Backend layer: archive backends used by the application and the CLI fallback.
# 后端层：应用与命令行回退共用的压缩包后端。
add_library(qarchiver_backends STATIC
    ArchiveBackend.h
    ExtractionUtils.cpp
    ExtractionUtils.h

    tar/TarBackend.cpp
    tar/TarBackend.h

    sevenzip/SevenZipBackend.cpp
    sevenzip/SevenZipBackend.h
    sevenzip/SevenZipListParser.cpp
    sevenzip/SevenZipListParser.h
    sevenzip/SevenZipUtils.cpp
    sevenzip/SevenZipUtils.h
    sevenzip/ZipBackend.h

    rar/RarBackend.cpp
    rar/RarBackend.h

    iso/IsoBackend.cpp
    iso/IsoBackend.h

    stream/StreamBackend.cpp
    stream/StreamBackend.h
)

target_include_directories(qarchiver_backends PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(qarchiver_backends PUBLIC
    qarchiver_utils
    Qt6::Core
)

set_target_properties(qarchiver_backends PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Add the in-process libarchive backend only when the library was found.
# 仅当找到 libarchive 时才加入进程内后端。
if(QARCHIVER_HAS_LIBARCHIVE)
    target_sources(qarchiver_backends PRIVATE
        libarchive/LibArchiveBackend.cpp
        libarchive/LibArchiveBackend.h
    )
    target_link_libraries(qarchiver_backends PRIVATE
        ${QARCHIVER_LIBARCHIVE_TARGET}
    )
    # Public: every consumer (core, ui, tests) compiles the same dispatch code.
    # 公开：所有使用方（core、ui、测试）编译同一份后端调度代码。
    target_compile_definitions(qarchiver_backends PUBLIC
        QARCHIVER_HAS_LIBARCHIVE
    )
endif()

qarchiver_set_warnings(qarchiver_backends)
