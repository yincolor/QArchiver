// QArchiver / 小Q解压
// MimeTypeHelper implementation, including compressed-tar stream probing.
// MimeTypeHelper 实现，包括压缩 tar 流探测。

#include "utils/MimeTypeHelper.h"

#include "common/FormatRegistry.h"
#include "utils/PlatformProcess.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cstdint>

#include "utils/Logger.h"
#include "utils/ProcessRunner.h"

// Magic Number signatures (design doc 4.3.2).  Compile-time byte arrays
// avoid runtime QByteArray::fromHex allocations.
// 魔数签名（设计文档 4.3.2）。使用编译期字节数组，
// 避免运行时 QByteArray::fromHex 分配。
namespace {

constexpr std::array<std::uint8_t, 6> kSig7z      = {0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c};
constexpr std::array<std::uint8_t, 8> kSigRar5    = {0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x01, 0x00};
constexpr std::array<std::uint8_t, 7> kSigRar4    = {0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x00};
constexpr std::array<std::uint8_t, 4> kSigZip     = {0x50, 0x4b, 0x03, 0x04}; // local file header
constexpr std::array<std::uint8_t, 4> kSigZipEmpty = {0x50, 0x4b, 0x05, 0x06}; // empty archive
constexpr std::array<std::uint8_t, 4> kSigZipSpan = {0x50, 0x4b, 0x07, 0x08}; // spanned archive
constexpr std::array<std::uint8_t, 2> kSigGzip    = {0x1f, 0x8b};
constexpr std::array<std::uint8_t, 3> kSigBzip2   = {0x42, 0x5a, 0x68};
constexpr std::array<std::uint8_t, 6> kSigXz      = {0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00};
constexpr std::array<std::uint8_t, 4> kSigZstd    = {0x28, 0xb5, 0x2f, 0xfd};
constexpr std::array<std::uint8_t, 5> kSigTar     = {0x75, 0x73, 0x74, 0x61, 0x72}; // "ustar" at 257

// Disc images keep their signature in sector 16 (offset 0x8000), far beyond
// the 512 bytes sampled for the other formats.
//   "CD001" at 0x8001 -> ISO9660 primary volume descriptor
//   "BEA01" at 0x8001 -> UDF volume recognition sequence
// 7-Zip uses the same two signatures (formats Iso and Udf).
// 光盘镜像的签名位于第 16 个扇区（偏移 0x8000），远超其它格式采样的
// 512 字节。
//   0x8001 处的 "CD001" -> ISO9660 主卷描述符
//   0x8001 处的 "BEA01" -> UDF 卷识别序列
// 7-Zip 的 Iso 与 Udf 格式使用同样两个签名。
constexpr int kDiscSignatureOffset = 0x8001;

// ISO9660 / UDF probe.  Only called after the 512-byte signatures and the
// tar probe failed, so ordinary archives never pay for it.
// ISO9660 / UDF 探测。仅在 512 字节签名与 tar 探测均未命中时调用，
// 普通压缩包不会为此付出开销。
bool looksLikeDiscImage(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    if (file.size() < kDiscSignatureOffset + 5) {
        return false;
    }
    if (!file.seek(kDiscSignatureOffset)) {
        return false;
    }
    const QByteArray signature = file.read(5);
    return signature == QByteArrayLiteral("CD001")
        || signature == QByteArrayLiteral("BEA01");
}

// List files in @p dir whose name matches @p regex, preserving the on-disk
// spelling (never lower-cases paths).
// 列出 @p dir 中名称匹配 @p regex 的文件，保留磁盘上的大小写，
// 绝不对路径做小写化。
QStringList matchingFiles(const QDir &dir, const QRegularExpression &regex)
{
    QStringList result;
    const QStringList entries = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (regex.match(entry).hasMatch()) {
            result << dir.absoluteFilePath(entry);
        }
    }
    return result;
}

// Stage 1.5: probe whether a compressed stream contains a tar (design doc 4.3.3).
// Decompress only the first block to stdout and inspect the tar header, so a
// multi-gigabyte stream is not extracted just to classify it.
// 阶段 1.5：探测压缩流中是否包含 tar（设计文档 4.3.3）。
// 仅将第一个块解压到 stdout 并检查 tar 头，避免为分类而解压数 GB 数据。
bool isTarHeader(const QByteArray &header)
{
    if (header.size() < 512) {
        return false;
    }
    if (header.mid(257, 5) == QByteArrayLiteral("ustar")) {
        return true;
    }

    // Old (v7) tar has no ustar magic; validate its octal checksum instead.
    // 旧式（v7）tar 没有 ustar 魔数；改为验证其八进制校验和。
    bool ok = false;
    const qint64 stored = header.mid(148, 8).trimmed().toLongLong(&ok, 8);
    if (!ok) {
        return false;
    }
    qint64 sum = 0;
    for (int i = 0; i < 512; ++i) {
        sum += (i >= 148 && i < 156)
            ? QLatin1Char(' ').unicode()
            : static_cast<unsigned char>(header.at(i));
    }
    return sum == stored;
}

bool isTarCompressedStream(const QString &sevenZip, const QString &filePath)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    qarchiverHideConsoleWindow(process);
    process.start(sevenZip,
                  {QStringLiteral("x"), QStringLiteral("-so"),
                   QStringLiteral("-y"), QStringLiteral("-sccUTF-8"),
                   QStringLiteral("--"), filePath});
    if (!process.waitForStarted(3000)) {
        return false;
    }

    QByteArray head;
    QElapsedTimer timer;
    timer.start();
    while (head.size() < 512 && timer.elapsed() < 3000) {
        if (process.bytesAvailable() == 0 && !process.waitForReadyRead(100)) {
            if (process.state() == QProcess::NotRunning) {
                break;
            }
            continue;
        }
        head += process.readAllStandardOutput();
        process.readAllStandardError();
    }

    process.kill();
    process.waitForFinished(1000);
    return isTarHeader(head);
}

ArchiveFormat resolveStreamFormat(const QString &filePath,
                                  ArchiveFormat tarFormat,
                                  ArchiveFormat streamFormat)
{
    // A compound tar suffix already tells us the payload, so no probing is
    // needed; the suffix table itself lives in FormatRegistry.
    // 复合 tar 后缀已能确定内容，无需探测；后缀表本身位于 FormatRegistry。
    const ArchiveFormat bySuffix = FormatRegistry::formatFromSuffix(filePath);
    const bool isTarSuffix = bySuffix == ArchiveFormat::Tar
        || bySuffix == ArchiveFormat::TarGz
        || bySuffix == ArchiveFormat::TarXz
        || bySuffix == ArchiveFormat::TarBz2
        || bySuffix == ArchiveFormat::TarZst;
    if (isTarSuffix) {
        return tarFormat;
    }

    const QString sevenZip = ProcessRunner::resolveToolPath(QStringLiteral("7z"));
    if (sevenZip.isEmpty()) {
        LOG_WARN("MimeTypeHelper", "7z tool not found; defaulting to stream format");
        return streamFormat;
    }

    return isTarCompressedStream(sevenZip, filePath) ? tarFormat : streamFormat;
}

} // namespace

QByteArray MimeTypeHelper::readMagic(const QString &filePath, int maxBytes)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.read(maxBytes);
}

bool MimeTypeHelper::matchMagic(const QByteArray &head, std::span<const std::uint8_t> signature, int offset)
{
    if (offset < 0 || head.size() < offset + static_cast<int>(signature.size())) {
        return false;
    }
    const auto *data = reinterpret_cast<const std::uint8_t *>(head.constData());
    return std::equal(signature.begin(), signature.end(), data + offset);
}

QString MimeTypeHelper::extensionOf(const QString &filePath)
{
    // Suffix knowledge lives in FormatRegistry; this is the detection-facing
    // accessor kept for readability at the call sites.
    // 后缀知识集中在 FormatRegistry；这里保留检测侧的可读访问器。
    return FormatRegistry::extensionOf(filePath);
}

bool MimeTypeHelper::isSplitVolume(const QString &filePath)
{
    return FormatRegistry::isSplitVolume(filePath);
}

QString MimeTypeHelper::resolveFirstVolume(const QString &filePath)
{
    const QFileInfo info(filePath);
    const QDir dir = info.dir();

    // 7z/zip split: archive.7z.001 (preserve the original case).
    // 7z/zip 分卷：archive.7z.001（保留原始大小写）。
    static const QRegularExpression zipVol(
        QStringLiteral("^(.*\\.(?:7z|zip))\\.\\d{3,}$"),
        QRegularExpression::CaseInsensitiveOption);
    auto match = zipVol.match(filePath);
    if (match.hasMatch()) {
        const QString base = QFileInfo(match.captured(1)).fileName();
        const QRegularExpression entryRe(
            QStringLiteral("^%1\\.\\d{3,}$").arg(QRegularExpression::escape(base)),
            QRegularExpression::CaseInsensitiveOption);
        QStringList volumes = matchingFiles(dir, entryRe);
        std::sort(volumes.begin(), volumes.end(), [](const QString &a, const QString &b) {
            static const QRegularExpression numberRe(QStringLiteral("\\.(\\d{3,})$"));
            const auto ma = numberRe.match(a);
            const auto mb = numberRe.match(b);
            if (ma.hasMatch() && mb.hasMatch()) {
                return ma.captured(1).toLongLong() < mb.captured(1).toLongLong();
            }
            return a < b;
        });
        if (!volumes.isEmpty()) {
            return volumes.first();
        }
        return filePath;
    }

    // RAR5 split: archive.partNN.rar / RAR5 分卷：archive.partNN.rar
    static const QRegularExpression rar5Vol(
        QStringLiteral("^(.*)\\.part\\d+\\.rar$"),
        QRegularExpression::CaseInsensitiveOption);
    match = rar5Vol.match(filePath);
    if (match.hasMatch()) {
        const QString base = QFileInfo(match.captured(1)).fileName();
        const QRegularExpression entryRe(
            QStringLiteral("^%1\\.part(\\d+)\\.rar$").arg(QRegularExpression::escape(base)),
            QRegularExpression::CaseInsensitiveOption);
        QStringList volumes = matchingFiles(dir, entryRe);
        std::sort(volumes.begin(), volumes.end(), [](const QString &a, const QString &b) {
            static const QRegularExpression numberRe(QStringLiteral("\\.part(\\d+)\\.rar$"));
            const auto ma = numberRe.match(a);
            const auto mb = numberRe.match(b);
            if (ma.hasMatch() && mb.hasMatch()) {
                return ma.captured(1).toInt() < mb.captured(1).toInt();
            }
            return a < b;
        });
        if (!volumes.isEmpty()) {
            return volumes.first();
        }
        return filePath;
    }

    // RAR4 split: archive.rar + archive.r00... / RAR4 分卷：archive.rar + archive.r00...
    static const QRegularExpression rar4Vol(
        QStringLiteral("^(.*)\\.r\\d{2,}$"),
        QRegularExpression::CaseInsensitiveOption);
    match = rar4Vol.match(filePath);
    if (match.hasMatch()) {
        const QString base = QFileInfo(match.captured(1)).fileName();
        const QRegularExpression entryRe(
            QStringLiteral("^%1\\.rar$").arg(QRegularExpression::escape(base)),
            QRegularExpression::CaseInsensitiveOption);
        const QStringList matches = matchingFiles(dir, entryRe);
        if (!matches.isEmpty()) {
            return matches.first();
        }
        return filePath;
    }

    return filePath;
}

ArchiveFormat MimeTypeHelper::detect(const QString &filePath)
{
    // Split archives carry their format header only in the first volume
    // (.001); .002/.003 have neither a magic signature nor a suffix QArchiver
    // recognises.  Forward detection to the first volume so opening any volume
    // works the same way as opening the first one.
    // 分卷压缩包只有首卷（.001）带格式头；.002/.003 既没有魔数也没有可识别的
    // 后缀。将检测转发到首卷，使打开任意一卷与打开首卷行为一致。
    if (FormatRegistry::isSplitVolume(filePath)) {
        const QString firstVolume = resolveFirstVolume(filePath);
        if (!firstVolume.isEmpty() && firstVolume != filePath) {
            return detect(firstVolume);
        }
    }

    const QFileInfo info(filePath);
    if (!info.exists() || !info.isReadable()) {
        LOG_WARN("MimeTypeHelper", "File not readable: %s", filePath.toUtf8().constData());
        return ArchiveFormat::Unknown;
    }

    const QByteArray head = readMagic(filePath);
    if (head.isEmpty()) {
        return ArchiveFormat::Unknown;
    }

    // Stage 1: Magic Number / 阶段 1：魔数
    if (matchMagic(head, kSigRar5, 0)) return ArchiveFormat::Rar5;
    if (matchMagic(head, kSigRar4, 0)) return ArchiveFormat::Rar4;
    if (matchMagic(head, kSig7z, 0))   return ArchiveFormat::SevenZip;
    if (matchMagic(head, kSigZip, 0)
        || matchMagic(head, kSigZipEmpty, 0)
        || matchMagic(head, kSigZipSpan, 0)) {
        return ArchiveFormat::Zip;
    }

    if (matchMagic(head, kSigGzip, 0)) {
        return resolveStreamFormat(filePath, ArchiveFormat::TarGz, ArchiveFormat::Gzip);
    }
    if (matchMagic(head, kSigBzip2, 0)) {
        return resolveStreamFormat(filePath, ArchiveFormat::TarBz2, ArchiveFormat::Bzip2);
    }
    if (matchMagic(head, kSigXz, 0)) {
        return resolveStreamFormat(filePath, ArchiveFormat::TarXz, ArchiveFormat::Xz);
    }
    if (matchMagic(head, kSigZstd, 0)) {
        return resolveStreamFormat(filePath, ArchiveFormat::TarZst, ArchiveFormat::Zstd);
    }
    if (matchMagic(head, kSigTar, 257)) {
        return ArchiveFormat::Tar;
    }
    if (looksLikeDiscImage(filePath)) {
        return ArchiveFormat::Iso;
    }

    // Stage 2: suffix fallback / 阶段 2：扩展名兜底
    const QString ext = extensionOf(filePath);
    LOG_DEBUG("MimeTypeHelper", "No magic match; falling back to suffix: %s",
              ext.toUtf8().constData());

    const ArchiveFormat bySuffix = FormatRegistry::formatFromSuffix(filePath);
    if (bySuffix != ArchiveFormat::Unknown) {
        return bySuffix;
    }

    LOG_WARN("MimeTypeHelper", "Unrecognized format: %s", filePath.toUtf8().constData());
    return ArchiveFormat::Unknown;
}
