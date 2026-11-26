// QArchiver / 小Q解压
// Single source of truth for archive file-name suffixes.
// 压缩包文件名后缀的唯一事实来源。

#include "common/FormatRegistry.h"

#include <QRegularExpression>

namespace {

// Compound suffixes that QFileInfo would otherwise split at the last dot.
// Longest forms first so ".tar.gz" wins over ".gz".
// 复合后缀，避免 QFileInfo 在最后一个点处截断；较长的形式在前，使
// ".tar.gz" 优先于 ".gz"。
const QStringList &compoundSuffixes()
{
    static const QStringList suffixes = {
        QStringLiteral(".tar.gz"),
        QStringLiteral(".tar.xz"),
        QStringLiteral(".tar.bz2"),
        QStringLiteral(".tar.zst"),
        QStringLiteral(".tgz"),
        QStringLiteral(".txz"),
        QStringLiteral(".tbz2"),
        QStringLiteral(".tzst"),
        QStringLiteral(".7z.001"),
        QStringLiteral(".zip.001")
    };
    return suffixes;
}

// Archive suffixes QArchiver can open, longest first, used to strip a name.
// QArchiver 可以打开的后缀（较长的在前），用于截断文件名。
const QStringList &openableSuffixes()
{
    static const QStringList suffixes = {
        QStringLiteral(".tar.gz"),
        QStringLiteral(".tar.xz"),
        QStringLiteral(".tar.bz2"),
        QStringLiteral(".tar.zst"),
        QStringLiteral(".tgz"),
        QStringLiteral(".txz"),
        QStringLiteral(".tbz2"),
        QStringLiteral(".tzst"),
        QStringLiteral(".tar"),
        QStringLiteral(".zip"),
        QStringLiteral(".7z"),
        QStringLiteral(".rar"),
        QStringLiteral(".gz"),
        QStringLiteral(".bz2"),
        QStringLiteral(".xz"),
        QStringLiteral(".zst"),
        QStringLiteral(".iso"),
        QStringLiteral(".001"),
        QStringLiteral(".part1")
    };
    return suffixes;
}

} // namespace

namespace FormatRegistry {

QString extensionOf(const QString &filePath)
{
    const QString lower = filePath.toLower();

    for (const QString &suffix : compoundSuffixes()) {
        if (lower.endsWith(suffix)) {
            return suffix;
        }
    }

    static const QRegularExpression partRarRe(
        QStringLiteral("\\.part\\d+\\.rar$"));
    static const QRegularExpression rarOldVolumeRe(
        QStringLiteral("\\.r\\d{2,}$"));
    if (partRarRe.match(lower).hasMatch()) {
        return QStringLiteral(".partNN.rar");
    }
    if (rarOldVolumeRe.match(lower).hasMatch()) {
        return lower.mid(lower.lastIndexOf(QLatin1Char('.')));
    }

    const int dot = lower.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) {
        return QString();
    }
    return lower.mid(dot);
}

QString kindSuffix(const QString &filePath)
{
    QString suffix = extensionOf(filePath);
    if (suffix.startsWith(QLatin1Char('.'))) {
        suffix.remove(0, 1);
    }
    return suffix;
}

ArchiveFormat formatFromSuffix(const QString &filePath)
{
    const QString ext = extensionOf(filePath);
    if (ext.isEmpty()) {
        return ArchiveFormat::Unknown;
    }

    if (ext == QLatin1String(".tar")) {
        return ArchiveFormat::Tar;
    }
    if (ext == QLatin1String(".tar.gz") || ext == QLatin1String(".tgz")) {
        return ArchiveFormat::TarGz;
    }
    if (ext == QLatin1String(".tar.xz") || ext == QLatin1String(".txz")) {
        return ArchiveFormat::TarXz;
    }
    if (ext == QLatin1String(".tar.bz2") || ext == QLatin1String(".tbz2")) {
        return ArchiveFormat::TarBz2;
    }
    if (ext == QLatin1String(".tar.zst") || ext == QLatin1String(".tzst")) {
        return ArchiveFormat::TarZst;
    }
    if (ext == QLatin1String(".zip") || ext == QLatin1String(".zip.001")) {
        return ArchiveFormat::Zip;
    }
    if (ext == QLatin1String(".7z") || ext == QLatin1String(".7z.001")) {
        return ArchiveFormat::SevenZip;
    }
    if (ext == QLatin1String(".rar") || ext == QLatin1String(".partNN.rar")) {
        return ArchiveFormat::Rar5;
    }
    // Old RAR volumes are .r00, .r01, ... (at least two digits).  Do not treat
    // every .r?? suffix as RAR: .rtf, .rpm and .rst are ordinary files.
    // 旧式 RAR 分卷是 .r00、.r01 等（至少两位数字）；不能把所有 .r?? 都当作
    // RAR，否则 .rtf、.rpm、.rst 等普通文件会被误判。
    static const QRegularExpression rar4SuffixRe(
        QStringLiteral("^\\.r\\d{2,}$"));
    if (rar4SuffixRe.match(ext).hasMatch()) {
        return ArchiveFormat::Rar4;
    }
    if (ext == QLatin1String(".gz")) {
        return ArchiveFormat::Gzip;
    }
    if (ext == QLatin1String(".bz2")) {
        return ArchiveFormat::Bzip2;
    }
    if (ext == QLatin1String(".xz")) {
        return ArchiveFormat::Xz;
    }
    if (ext == QLatin1String(".zst")) {
        return ArchiveFormat::Zstd;
    }
    if (ext == QLatin1String(".iso")) {
        return ArchiveFormat::Iso;
    }
    return ArchiveFormat::Unknown;
}

ArchiveFormat formatFromName(const QString &name)
{
    QString token = name.trimmed().toLower();
    if (token.startsWith(QLatin1Char('.'))) {
        token.remove(0, 1);
    }
    if (token.isEmpty()) {
        return ArchiveFormat::Unknown;
    }

    // Longest / most specific names first.
    // 先匹配最长、最具体的名称。
    if (token == QLatin1String("tar.gz") || token == QLatin1String("tgz")) {
        return ArchiveFormat::TarGz;
    }
    if (token == QLatin1String("tar.xz") || token == QLatin1String("txz")) {
        return ArchiveFormat::TarXz;
    }
    if (token == QLatin1String("tar.bz2") || token == QLatin1String("tbz2")
        || token == QLatin1String("tbz")) {
        return ArchiveFormat::TarBz2;
    }
    if (token == QLatin1String("tar.zst") || token == QLatin1String("tzst")) {
        return ArchiveFormat::TarZst;
    }
    if (token == QLatin1String("tar")) {
        return ArchiveFormat::Tar;
    }
    if (token == QLatin1String("zip")) {
        return ArchiveFormat::Zip;
    }
    if (token == QLatin1String("7z") || token == QLatin1String("sevenzip")) {
        return ArchiveFormat::SevenZip;
    }
    if (token == QLatin1String("rar") || token == QLatin1String("rar5")) {
        return ArchiveFormat::Rar5;
    }
    if (token == QLatin1String("gz") || token == QLatin1String("gzip")) {
        return ArchiveFormat::Gzip;
    }
    if (token == QLatin1String("bz2") || token == QLatin1String("bzip2")) {
        return ArchiveFormat::Bzip2;
    }
    if (token == QLatin1String("xz")) {
        return ArchiveFormat::Xz;
    }
    if (token == QLatin1String("zst") || token == QLatin1String("zstd")) {
        return ArchiveFormat::Zstd;
    }
    if (token == QLatin1String("iso")) {
        return ArchiveFormat::Iso;
    }
    return ArchiveFormat::Unknown;
}

bool isSplitVolume(const QString &filePath)
{
    static const QRegularExpression zipVol(
        QStringLiteral("\\.(?:7z|zip)\\.\\d{3,}$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rar4Vol(
        QStringLiteral("\\.r\\d{2,}$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rar5Vol(
        QStringLiteral("\\.part\\d+\\.rar$"),
        QRegularExpression::CaseInsensitiveOption);

    return zipVol.match(filePath).hasMatch()
        || rar4Vol.match(filePath).hasMatch()
        || rar5Vol.match(filePath).hasMatch();
}

bool isArchivePath(const QString &filePath)
{
    return isSplitVolume(filePath)
        || formatFromSuffix(filePath) != ArchiveFormat::Unknown;
}

QString stripArchiveExtension(const QString &fileName)
{
    QString name = fileName;
    QString removedSuffix;
    for (const QString &suffix : openableSuffixes()) {
        if (name.endsWith(suffix, Qt::CaseInsensitive)) {
            removedSuffix = suffix;
            name.chop(suffix.size());
            break;
        }
    }

    // RAR5 split volumes are named "archive.part1.rar"; after removing .rar the
    // part number is not part of the archive name.  Drop it so one-click
    // extraction uses "archive" rather than "archive.part1".
    // RAR5 分卷名为 "archive.part1.rar"；去掉 .rar 后 part 编号不属于压缩包
    // 名称，需要一并去掉，否则一键解压目录会变成 "archive.part1"。
    if (removedSuffix.compare(QLatin1String(".rar"), Qt::CaseInsensitive) == 0) {
        static const QRegularExpression rarPartRe(
            QStringLiteral("\\.part\\d+$"),
            QRegularExpression::CaseInsensitiveOption);
        name.remove(rarPartRe);
    } else if (removedSuffix.isEmpty()) {
        // Old RAR volumes have no openable suffix in the table above; their
        // .r00/.r01 suffix is still part of the volume name, not the base name.
        // 旧式 RAR 分卷不在上面的可打开后缀表中；.r00/.r01 仍是分卷后缀，
        // 不属于压缩包基名。
        static const QRegularExpression rar4PartRe(
            QStringLiteral("\\.r\\d{2,}$"),
            QRegularExpression::CaseInsensitiveOption);
        name.remove(rar4PartRe);
    }

    // Split-volume names resolve to "archive.7z" / "archive.zip" after the
    // numeric suffix was removed; drop the inner extension as well.
    // 分卷名去掉数字后缀后可能变成 "archive.7z" / "archive.zip"，
    // 再移除内层扩展名。
    if (name.endsWith(QLatin1String(".7z"), Qt::CaseInsensitive)
        || name.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) {
        name.chop(3);
    }
    return name;
}

const QStringList &openGlobPatterns()
{
    static const QStringList patterns = {
        QStringLiteral("*.tar"),
        QStringLiteral("*.tar.gz"),
        QStringLiteral("*.tgz"),
        QStringLiteral("*.tar.xz"),
        QStringLiteral("*.txz"),
        QStringLiteral("*.tar.bz2"),
        QStringLiteral("*.tbz2"),
        QStringLiteral("*.tar.zst"),
        QStringLiteral("*.tzst"),
        QStringLiteral("*.zip"),
        QStringLiteral("*.zip.*"),
        QStringLiteral("*.7z"),
        QStringLiteral("*.7z.*"),
        QStringLiteral("*.rar"),
        QStringLiteral("*.part*.rar"),
        QStringLiteral("*.r??"),
        QStringLiteral("*.gz"),
        QStringLiteral("*.bz2"),
        QStringLiteral("*.xz"),
        QStringLiteral("*.zst"),
        QStringLiteral("*.iso")
    };
    return patterns;
}

const QStringList &associationSuffixes()
{
    static const QStringList suffixes = {
        QStringLiteral(".zip"),
        QStringLiteral(".7z"),
        QStringLiteral(".rar"),
        QStringLiteral(".tar"),
        QStringLiteral(".tar.gz"),
        QStringLiteral(".tgz"),
        QStringLiteral(".tar.xz"),
        QStringLiteral(".txz"),
        QStringLiteral(".tar.bz2"),
        QStringLiteral(".tbz2"),
        QStringLiteral(".tar.zst"),
        QStringLiteral(".tzst"),
        QStringLiteral(".gz"),
        QStringLiteral(".bz2"),
        QStringLiteral(".xz"),
        QStringLiteral(".zst"),
        QStringLiteral(".iso")
    };
    return suffixes;
}

const QStringList &archiveLikeSuffixes()
{
    static const QStringList suffixes = {
        // Openable by QArchiver / QArchiver 可以打开
        QStringLiteral("zip"), QStringLiteral("7z"), QStringLiteral("rar"),
        QStringLiteral("tar"), QStringLiteral("gz"), QStringLiteral("tgz"),
        QStringLiteral("xz"), QStringLiteral("txz"), QStringLiteral("bz2"),
        QStringLiteral("tbz2"), QStringLiteral("zst"), QStringLiteral("tzst"),
        QStringLiteral("001"),
        QStringLiteral("tar.gz"), QStringLiteral("tar.xz"),
        QStringLiteral("tar.bz2"), QStringLiteral("tar.zst"),
        QStringLiteral("7z.001"), QStringLiteral("zip.001"),
        // Recognized archive types that need an external tool / 需要外部工具的压缩包类型
        QStringLiteral("cab"), QStringLiteral("arj"), QStringLiteral("lzh"),
        QStringLiteral("lha"), QStringLiteral("ace")
    };
    return suffixes;
}

} // namespace FormatRegistry
