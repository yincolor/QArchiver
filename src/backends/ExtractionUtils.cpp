// QArchiver / 小Q解压
// Implementation of helpers shared by the command-line backends.
// 命令行后端共用辅助函数的实现。

#include "backends/ExtractionUtils.h"

#include <QSet>

#include "common/ArchiveEntry.h"
#include "utils/FileUtils.h"

namespace ExtractionUtils {

bool isUnderSkipped(const QString &path, const QSet<QString> &skipped)
{
    if (skipped.isEmpty()) {
        return false;
    }
    const QString normalized = FileUtils::normalizeEntryPath(path);
    if (skipped.contains(normalized)) {
        return true;
    }
    for (const QString &prefix : skipped) {
        if (normalized.startsWith(prefix + QLatin1Char('/'))) {
            return true;
        }
    }
    return false;
}

QStringList buildExtractionSelection(const QList<ArchiveEntry> &entries,
                                     const ExtractOptions &options)
{
    QSet<QString> skipped;
    for (const QString &path : options.skippedSymlinks) {
        const QString normalized = FileUtils::normalizeEntryPath(path);
        if (!normalized.isEmpty()) {
            skipped.insert(normalized);
        }
    }

    QStringList selected;
    QSet<QString> seen;
    auto add = [&](const QString &rawPath) {
        const QString normalized = FileUtils::normalizeEntryPath(rawPath);
        // The archive-root "." entry expands to the whole tree in 7-Zip and
        // would defeat the member filter, including skipped symlinks.
        // 归档根目录的 "." 条目会让 7-Zip 展开整棵树，从而绕过成员过滤
        //（包括被跳过的符号链接），因此必须排除。
        if (normalized.isEmpty() || normalized == QStringLiteral(".")
            || isUnderSkipped(normalized, skipped)
            || seen.contains(normalized)) {
            return;
        }
        seen.insert(normalized);
        selected << rawPath;
    };

    if (!options.selectedFiles.isEmpty()) {
        for (const QString &path : options.selectedFiles) {
            add(path);
        }
        return selected;
    }
    if (skipped.isEmpty()) {
        return selected; // empty means "all entries" / 空表示“全部条目”
    }
    for (const ArchiveEntry &entry : entries) {
        add(entry.path);
    }
    return selected;
}

bool looksLikeCorruption(const QString &errorText)
{
    return errorText.contains(QStringLiteral("Can not open"), Qt::CaseInsensitive)
        || errorText.contains(QStringLiteral("Cannot open"), Qt::CaseInsensitive)
        || errorText.contains(QStringLiteral("is not archive"), Qt::CaseInsensitive)
        || errorText.contains(QStringLiteral("Unexpected end"), Qt::CaseInsensitive)
        || errorText.contains(QStringLiteral("Data Error"), Qt::CaseInsensitive)
        || errorText.contains(QStringLiteral("CRC"), Qt::CaseInsensitive)
        || errorText.contains(QStringLiteral("Headers Error"), Qt::CaseInsensitive);
}

} // namespace ExtractionUtils
