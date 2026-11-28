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

// Stable, human-readable name for logs and diagnostics.
// 供日志与诊断使用的稳定可读名称。
inline const char *errorCodeName(ArchiveErrorCode code)
{
    switch (code) {
    case ArchiveErrorCode::Unknown:                return "Unknown";
    case ArchiveErrorCode::OperationNotSupported:  return "OperationNotSupported";
    case ArchiveErrorCode::UnsupportedFormat:      return "UnsupportedFormat";
    case ArchiveErrorCode::PasswordRequired:       return "PasswordRequired";
    case ArchiveErrorCode::WrongPassword:          return "WrongPassword";
    case ArchiveErrorCode::CorruptedArchive:       return "CorruptedArchive";
    case ArchiveErrorCode::InsufficientDiskSpace:  return "InsufficientDiskSpace";
    case ArchiveErrorCode::PathTraversalBlocked:   return "PathTraversalBlocked";
    case ArchiveErrorCode::VolumeMissing:          return "VolumeMissing";
    case ArchiveErrorCode::ToolNotFound:           return "ToolNotFound";
    case ArchiveErrorCode::FileNotFound:           return "FileNotFound";
    case ArchiveErrorCode::PermissionDenied:       return "PermissionDenied";
    case ArchiveErrorCode::EncodingError:          return "EncodingError";
    case ArchiveErrorCode::PartialExtraction:      return "PartialExtraction";
    case ArchiveErrorCode::HardLinkUnsupported:    return "HardLinkUnsupported";
    case ArchiveErrorCode::Cancelled:              return "Cancelled";
    }
    return "Unknown";
}

Q_DECLARE_METATYPE(ArchiveErrorCode)

#endif // ARCHIVEERROR_H
