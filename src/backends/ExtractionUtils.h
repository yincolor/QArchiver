#ifndef EXTRACTIONUTILS_H
#define EXTRACTIONUTILS_H

// QArchiver / 小Q解压
// Helpers shared by the command-line backends (7z/zip/tar).
// 命令行后端（7z/zip/tar）共用的辅助函数。

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "common/ArchiveOptions.h"

struct ArchiveEntry;

namespace ExtractionUtils {

// Whether @p path equals or lives below one of the skipped symlink paths.
// @p path 是否等于某个被跳过的符号链接路径或位于其下。
bool isUnderSkipped(const QString &path, const QSet<QString> &skipped);

// Build the member list passed to the CLI helper.  An empty list means "all
// entries"; entries below a skipped symlink and the archive-root "." entry are
// excluded so the member filter cannot defeat the preflight.
// 构造传给命令行助手的成员列表。空列表表示“全部条目”；被跳过符号链接下的
// 条目与归档根 "." 条目会被排除，避免成员过滤绕过预检。
QStringList buildExtractionSelection(const QList<ArchiveEntry> &entries,
                                     const ExtractOptions &options);

// Heuristic for 7-Zip error text that indicates corruption rather than a
// missing tool or a permission problem.
// 判断 7-Zip 错误文本是否更像“压缩包损坏”而不是缺少工具或权限问题。
bool looksLikeCorruption(const QString &errorText);

} // namespace ExtractionUtils

#endif // EXTRACTIONUTILS_H
