# Shared layer: value types and error codes used by every other layer.
# 共享层：所有其他层共用的值类型与错误码。
#
# This layer has no dependencies inside QArchiver.  Keeping the shared value
# types here is what makes the dependency direction acyclic:
# 该层不依赖 QArchiver 内的任何其他层。把共用值类型放在这里，
# 才使依赖方向保持无环：
#
#   common  <-  utils  <-  backends  <-  core  <-  ui  <-  app/main
add_library(qarchiver_common STATIC
    ArchiveConstants.h
    ArchiveEntry.h
    ArchiveOptions.h
    ArchiveError.h
    FormatRegistry.cpp
    FormatRegistry.h
)

target_include_directories(qarchiver_common PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(qarchiver_common PUBLIC
    Qt6::Core
)

set_target_properties(qarchiver_common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

qarchiver_set_warnings(qarchiver_common)
