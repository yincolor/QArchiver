#ifndef ARCHIVEERROR_H
#define ARCHIVEERROR_H

// QArchiver / 小Q解压
// Archive error codes reported through ArchiveBackend signals.
// 通过 ArchiveBackend 信号上报的压缩包错误码。

#include <QString>
#include "common/ArchiveConstants.h"

// Error code enumeration (matches design doc 4.5.1)
// 错误码枚举（与设计文档 4.5.1 对应）
enum class ArchiveErrorCode {
    Unknown,                // Unknown / unclassified
    OperationNotSupported,  // Read-only backend asked to write
    UnsupportedFormat,      // Format unrecognized / unsupported
    PasswordRequired,       // Password needed but not provided
    WrongPassword,          // Incorrect password
    CorruptedArchive,       // Archive corrupted / CRC error
    InsufficientDiskSpace,  // Not enough disk space
    PathTraversalBlocked,   // Zip Slip path traversal blocked
    VolumeMissing,          // Volume missing
    ToolNotFound,           // Bundled 7-Zip helper not available
    FileNotFound,           // Source/target not found
    PermissionDenied,       // Insufficient permissions
    EncodingError,          // Filename encoding unparseable
    PartialExtraction,      // Extraction partially moved to the destination
    HardLinkUnsupported,    // A required hard link cannot be created safely
    Cancelled,              // User cancelled
};

Q_DECLARE_METATYPE(ArchiveErrorCode)

#endif // ARCHIVEERROR_H
