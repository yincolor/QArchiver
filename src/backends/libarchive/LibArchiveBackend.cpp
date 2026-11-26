// QArchiver / 小Q解压
// LibArchiveBackend listing, extraction and stream handling.
// LibArchiveBackend 的列表、解压与流处理。

#include "backends/libarchive/LibArchiveBackend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <memory>
#include <string>

#include <archive.h>
#include <archive_entry.h>

#ifndef Q_OS_WIN
#include <unistd.h>
#else
#include <windows.h>
#endif

#include "utils/EncodingDetector.h"
#include "utils/FileUtils.h"
#include "utils/Logger.h"

namespace {

// Read/write granularity handed to libarchive. 256 KiB keeps the syscall count
// low without hurting the progress/cancel latency.
// 交给 libarchive 的读写粒度。256 KiB 可减少系统调用次数，
// 同时不会明显影响进度更新与取消延迟。
constexpr size_t kBlockSize = 256 * 1024;

struct ReaderDeleter {
    void operator()(struct archive *reader) const
    {
        if (reader) {
            archive_read_free(reader);
        }
    }
};
using ReaderPtr = std::unique_ptr<struct archive, ReaderDeleter>;

// libarchive entry names are decoded by the backend itself: the zip reader
// only yields UTF-8 when the archive carries the UTF-8 flag, and legacy
// archives (GBK/Big5/CP437 names) hand out raw bytes that must not be
// interpreted with the process locale.
// 条目名由后端自行解码：zip 读取器只在压缩包带 UTF-8 标志时给出 UTF-8，
// 旧式压缩包（GBK/Big5/CP437 名称）给出的是原始字节，
// 不能用进程 locale 解释。
constexpr int kNameSampleBytes = 64 * 1024;

QString libarchiveError(struct archive *reader)
{
    if (const char *error = archive_error_string(reader)) {
        return QString::fromUtf8(error);
    }
    return QStringLiteral("unknown libarchive error");
}

ReaderPtr openReader(const QString &filePath, ArchiveFormat fmt,
                     const QString &password, QString *errorOut)
{
    ReaderPtr reader(archive_read_new());
    if (!reader) {
        *errorOut = QStringLiteral("libarchive: failed to allocate a reader");
        return {};
    }

    archive_read_support_filter_all(reader.get());
    archive_read_support_format_all(reader.get());
    // Pure gz/bz2/xz streams have no header of their own; the raw reader
    // exposes them as a single unnamed entry.
    // 纯 gz/bz2/xz 流没有自身头部；raw 读取器将其作为单个无名条目暴露。
    if (isStreamFamily(fmt)) {
        archive_read_support_format_raw(reader.get());
    }

    // Per-format "hdrcharset" is deliberately not used here: libarchive would
    // convert the names through the current locale and fail outright on a
    // non-UTF-8 locale.  Callers decode the raw bytes instead
    // (see LibArchiveBackend::decodeEntryString).
    // 此处刻意不使用按格式的 "hdrcharset"：libarchive 会按当前 locale 转换名称，
    // 在非 UTF-8 locale 下会直接失败。名称改由调用方自行解码
    // （见 LibArchiveBackend::decodeEntryString）。

    if (!password.isEmpty()) {
        archive_read_add_passphrase(reader.get(), password.toUtf8().constData());
    }

    const QByteArray nativePath = QFile::encodeName(filePath);
    if (archive_read_open_filename(reader.get(), nativePath.constData(), kBlockSize)
        != ARCHIVE_OK) {
        *errorOut = libarchiveError(reader.get());
        return {};
    }
    return reader;
}

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

#ifdef Q_OS_WIN
#ifndef SYMBOLIC_LINK_FLAG_DIRECTORY
#define SYMBOLIC_LINK_FLAG_DIRECTORY 0x1
#endif
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif

bool createSymbolicLink(const QString &target, const QString &linkPath)
{
    QFile::remove(linkPath);
    const QString nativeLink = QDir::toNativeSeparators(linkPath);
    const QString nativeTarget = QDir::toNativeSeparators(target);
    const std::wstring linkW = nativeLink.toStdWString();
    const std::wstring targetW = nativeTarget.toStdWString();
    const DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (CreateSymbolicLinkW(linkW.c_str(), targetW.c_str(), flags) != 0) {
        return true;
    }
    return CreateSymbolicLinkW(linkW.c_str(), targetW.c_str(),
                               flags | SYMBOLIC_LINK_FLAG_DIRECTORY) != 0;
}
#endif

bool createHardLink(const QString &target, const QString &linkPath)
{
#ifndef Q_OS_WIN
    QFile::remove(linkPath);
    const QByteArray nativeTarget = QFile::encodeName(target);
    const QByteArray nativeLink = QFile::encodeName(linkPath);
    return ::link(nativeTarget.constData(), nativeLink.constData()) == 0;
#else
    QFile::remove(linkPath);
    return CreateHardLinkW(
               reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(linkPath).utf16()),
               reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(target).utf16()),
               nullptr)
        != 0;
#endif
}

} // namespace

LibArchiveBackend::LibArchiveBackend(ArchiveFormat fmt, QObject *parent)
    : ArchiveBackend(parent)
    , m_format(fmt)
{
}

QString LibArchiveBackend::inferStreamName() const
{
    QFileInfo info(m_archivePath);
    QString baseName = info.fileName();
    const QStringList suffixes = {QStringLiteral(".gz"), QStringLiteral(".bz2"),
                                  QStringLiteral(".xz")};
    for (const QString &suffix : suffixes) {
        if (baseName.endsWith(suffix, Qt::CaseInsensitive)) {
            baseName.chop(suffix.size());
            break;
        }
    }
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("output");
    }
    return baseName;
}

void LibArchiveBackend::openArchive(const QString &filePath)
{
    const QFileInfo info(filePath);
    if (!info.exists()) {
        emit errorOccurred(ArchiveErrorCode::FileNotFound,
                           tr("File not found: %1").arg(filePath));
        return;
    }
    if (!info.isReadable()) {
        emit errorOccurred(ArchiveErrorCode::PermissionDenied,
                           tr("File is not readable: %1").arg(filePath));
        return;
    }

    m_archivePath = filePath;
    m_entries.clear();
    // The name encoding belongs to the archive, not to the backend instance.
    // 名称编码属于具体压缩包，而不是后端实例。
    m_nameEncoding.clear();
    m_isOpen = true;
    emit archiveOpened(filePath);
}

QString LibArchiveBackend::decodeEntryString(const QByteArray &raw) const
{
    if (raw.isEmpty()) {
        return QString();
    }
    if (m_nameEncoding.isEmpty()) {
        // extract() without a preceding listContents(): fall back to a per-name
        // guess instead of trusting the process locale.
        // 未经 listContents() 直接 extract()：退化为逐名猜测，
        // 而不是信任进程 locale。
        return EncodingDetector::decode(raw);
    }
    return EncodingDetector::decode(raw, m_nameEncoding);
}

QString LibArchiveBackend::decodeLinkTarget(const QByteArray &raw) const
{
    if (raw.isEmpty()) {
        return QString();
    }
    // Link targets are usually ASCII or already UTF-8 even in archives whose
    // member names use a legacy encoding, so valid UTF-8 is kept untouched.
    // 即使在成员名使用旧式编码的压缩包里，链接目标通常也是 ASCII 或已是 UTF-8，
    // 因此合法 UTF-8 原样保留。
    if (m_nameEncoding.compare(QStringLiteral("UTF-8"), Qt::CaseInsensitive) != 0
        && raw.isValidUtf8()) {
        return QString::fromUtf8(raw);
    }
    return decodeEntryString(raw);
}

QString LibArchiveBackend::entryPathString(struct archive_entry *entry) const
{
    if (const char *utf8 = archive_entry_pathname_utf8(entry)) {
        return QString::fromUtf8(utf8);
    }
    if (const char *raw = archive_entry_pathname(entry)) {
        return decodeEntryString(QByteArray(raw));
    }
    return QString();
}

void LibArchiveBackend::reportFailure(const QString &message,
                                      const QString &password,
                                      const QString &context)
{
    const QString lower = message.toLower();

    const bool needsPassword = lower.contains(QStringLiteral("passphrase required"))
        || lower.contains(QStringLiteral("requires a password"))
        || lower.contains(QStringLiteral("password required"));
    const bool wrongPassword = lower.contains(QStringLiteral("incorrect passphrase"))
        || lower.contains(QStringLiteral("wrong password"))
        || lower.contains(QStringLiteral("passphrase is incorrect"));

    if (needsPassword || wrongPassword) {
        if (password.isEmpty()) {
            emit passwordRequested();
            return;
        }
        emit errorOccurred(ArchiveErrorCode::WrongPassword, tr("Wrong password"));
        return;
    }

    // libarchive knows the container but not its crypto (e.g. encrypted 7z).
    // UnsupportedFormat lets the manager retry with the CLI backend.
    // libarchive 能识别容器但无法处理其加密（例如加密 7z）。
    // 返回 UnsupportedFormat 可让管理器改用 CLI 后端重试。
    if (lower.contains(QStringLiteral("encrypted"))) {
        emit errorOccurred(ArchiveErrorCode::UnsupportedFormat,
                           tr("%1: %2").arg(context, message));
        return;
    }
    if (lower.contains(QStringLiteral("cannot be converted from utf-8"))
        || lower.contains(QStringLiteral("iconv"))) {
        emit errorOccurred(ArchiveErrorCode::EncodingError,
                           tr("%1: %2").arg(context, message));
        return;
    }

    LOG_INFO("LibArchiveBackend", "%s failed: %s", context.toUtf8().constData(),
             message.toUtf8().constData());
    emit errorOccurred(ArchiveErrorCode::CorruptedArchive,
                       tr("%1: %2").arg(context, message));
}

void LibArchiveBackend::listContents(const ListOptions &options)
{
    if (!m_isOpen) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("Archive not opened"));
        return;
    }

    QString error;
    ReaderPtr reader = openReader(m_archivePath, m_format, options.password, &error);
    if (!reader) {
        reportFailure(error, options.password, tr("Cannot open archive"));
        return;
    }

    // Entry names are decoded after the whole archive has been walked: the
    // legacy encoding is a property of the archive, so a single vote over all
    // raw name bytes is far more reliable than a per-entry guess (which flips
    // between UTF-8 and GB18030 inside one archive).
    // 条目名在遍历完整包后再解码：旧式编码是整个压缩包的属性，
    // 对所有原始名称字节统一投一次票远比逐条猜测可靠
    // （逐条猜会在同一个包内于 UTF-8 与 GB18030 之间来回跳）。
    struct PendingEntry {
        ArchiveEntry item;
        QByteArray rawPath;     // empty when libarchive already had UTF-8
        QByteArray rawLinkTarget;
        QByteArray rawHardLinkTarget;
    };

    QList<PendingEntry> pending;
    struct archive_entry *entry = nullptr;
    int result = ARCHIVE_OK;

    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        PendingEntry pendingEntry;
        ArchiveEntry &item = pendingEntry.item;

        if (const char *utf8 = archive_entry_pathname_utf8(entry)) {
            item.path = QString::fromUtf8(utf8);
        } else if (const char *raw = archive_entry_pathname(entry)) {
            pendingEntry.rawPath = QByteArray(raw);
        }

        const auto type = archive_entry_filetype(entry);
        item.isDir = (type == AE_IFDIR);
        item.isSymLink = (type == AE_IFLNK);
        if (item.isSymLink) {
            if (const char *target = archive_entry_symlink(entry)) {
                pendingEntry.rawLinkTarget = QByteArray(target);
            }
        }
        if (const char *hardTarget = archive_entry_hardlink(entry)) {
            item.isHardLink = true;
            pendingEntry.rawHardLinkTarget = QByteArray(hardTarget);
        }
        if (archive_entry_size_is_set(entry)) {
            item.size = archive_entry_size(entry);
        }
        const auto mtime = archive_entry_mtime(entry);
        if (mtime > 0) {
            item.mtime = QDateTime::fromSecsSinceEpoch(mtime);
        }
        item.isEncrypted = archive_entry_is_encrypted(entry) != 0;

        pending.append(pendingEntry);
        archive_read_data_skip(reader.get());
    }

    if (result != ARCHIVE_EOF) {
        const QString message = libarchiveError(reader.get());
        reader.reset();
        reportFailure(message, options.password, tr("Cannot read archive"));
        return;
    }
    reader.reset();

    // An explicit hint from the caller (user preference) wins over detection.
    // 调用方显式给出的提示（用户偏好）优先于自动检测。
    if (!options.encodingHint.isEmpty()) {
        m_nameEncoding = options.encodingHint;
    } else {
        QByteArray sample;
        for (const PendingEntry &pendingEntry : pending) {
            if (pendingEntry.rawPath.isEmpty()) {
                continue;
            }
            if (sample.size() >= kNameSampleBytes) {
                break; // never cut a name in half / 不截断半个名称
            }
            sample.append(pendingEntry.rawPath);
            sample.append('\n');
        }
        m_nameEncoding = sample.isEmpty() ? QStringLiteral("UTF-8")
                                          : EncodingDetector::detect(sample);
    }

    QList<ArchiveEntry> entries;
    for (PendingEntry &pendingEntry : pending) {
        ArchiveEntry item = pendingEntry.item;
        if (!pendingEntry.rawPath.isEmpty()) {
            item.path = FileUtils::normalizeEntryPath(decodeEntryString(pendingEntry.rawPath));
        }
        item.path = FileUtils::normalizeEntryPath(item.path);
        item.linkTarget = decodeLinkTarget(pendingEntry.rawLinkTarget);
        item.hardLinkTarget = decodeEntryString(pendingEntry.rawHardLinkTarget);

        if (item.path.isEmpty() || item.path == QStringLiteral(".")) {
            continue;
        }

        if (isStreamFamily(m_format) && entries.isEmpty()) {
            // Raw streams either expose the stored name or the placeholder
            // "data"; only the placeholder needs to be replaced.
            // 原始流可能暴露已存储名称或占位符 "data"；仅需替换占位符。
            if (item.path.isEmpty() || item.path == QStringLiteral("data")) {
                item.path = inferStreamName();
            }
            item.isDir = false;
        }

        if (item.isDir && !item.path.endsWith(QLatin1Char('/'))) {
            item.path.append(QLatin1Char('/'));
        }

        entries.append(item);
    }

    m_entries = entries;
    LOG_INFO("LibArchiveBackend", "Listed %d entries from %s (name encoding %s)",
             static_cast<int>(entries.size()), m_archivePath.toUtf8().constData(),
             m_nameEncoding.toUtf8().constData());
    emit fileListReady(entries);
}

void LibArchiveBackend::extract(const ExtractOptions &options)
{
    if (!m_isOpen) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("Archive not opened"));
        return;
    }
    if (!options.encodingHint.isEmpty()) {
        // Honour an explicit user choice over the encoding detected while listing.
        // 用户显式选择优先于列表时检测到的编码。
        m_nameEncoding = options.encodingHint;
    }
    if (options.destDir.trimmed().isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("No destination directory selected"));
        return;
    }

    QSet<QString> selected;
    for (const QString &path : options.selectedFiles) {
        selected.insert(FileUtils::normalizeEntryPath(path));
    }
    const auto isSelected = [&selected](const QString &relative) {
        if (selected.isEmpty()) {
            return true;
        }
        if (selected.contains(relative)) {
            return true;
        }
        for (const QString &candidate : selected) {
            // Selecting a directory selects its subtree (same as passing the
            // member path to 7z), and selecting a child keeps the parent
            // directories needed to recreate the layout.
            // 选中目录会选中其整个子树（与把成员路径传给 7z 相同）；
            // 选中子项时会保留重建目录结构所需的父目录。
            if (relative.startsWith(candidate + QLatin1Char('/'))
                || candidate.startsWith(relative + QLatin1Char('/'))) {
                return true;
            }
        }
        return false;
    };

    const QString tempDir = FileUtils::createTempExtractDir(options.destDir);
    if (tempDir.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::PermissionDenied,
                           tr("Cannot create a temporary directory in %1")
                               .arg(options.destDir));
        return;
    }

    QString error;
    ReaderPtr reader = openReader(m_archivePath, m_format, options.password, &error);
    if (!reader) {
        FileUtils::removeTemporaryDirectory(tempDir);
        reportFailure(error, options.password, tr("Cannot open archive"));
        return;
    }

    // Progress model: prefer the entry sizes from the last listing; fall back
    // to a per-entry counter when the archive was never listed.
    // 进度模型：优先使用最近一次列表中的条目大小；
    // 若从未列表，则回退为按条目计数。
    int totalEntries = 0;
    qint64 totalBytes = 0;
    for (const ArchiveEntry &item : m_entries) {
        QString relative = item.path;
        if (relative.endsWith(QLatin1Char('/'))) {
            relative.chop(1);
        }
        if (!isSelected(relative)) {
            continue;
        }
        ++totalEntries;
        if (!item.isDir) {
            totalBytes += qMax<qint64>(0, item.size);
        }
    }

    QSet<QString> skippedSymlinks;
    for (const QString &path : options.skippedSymlinks) {
        const QString normalized = FileUtils::normalizeEntryPath(path);
        if (!normalized.isEmpty()) {
            skippedSymlinks.insert(normalized);
        }
    }
    QList<QPair<QString, QString>> pendingHardLinks;

    int currentEntry = 0;
    qint64 writtenBytes = 0;
    bool cancelled = false;
    QString failure;

    struct archive_entry *entry = nullptr;
    int result = ARCHIVE_OK;

    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        if (isCancelled()) {
            cancelled = true;
            break;
        }

        const QString relative = FileUtils::normalizeEntryPath(entryPathString(entry));
        if (relative.isEmpty() || relative == QStringLiteral(".")) {
            archive_read_data_skip(reader.get());
            continue;
        }
        if (!isSelected(relative) || isUnderSkipped(relative, skippedSymlinks)) {
            archive_read_data_skip(reader.get());
            continue;
        }
        if (FileUtils::isPathTraversal(relative, tempDir)) {
            failure = tr("Blocked unsafe path: %1").arg(relative);
            emit errorOccurred(ArchiveErrorCode::PathTraversalBlocked, failure);
            break;
        }

        ++currentEntry;

        const QString outPath = QDir(tempDir).filePath(relative);
        const auto type = archive_entry_filetype(entry);

        if (type == AE_IFDIR) {
            if (!QDir().mkpath(outPath)) {
                failure = tr("Cannot create directory: %1").arg(outPath);
                emit errorOccurred(ArchiveErrorCode::PermissionDenied, failure);
                break;
            }
            continue;
        }

        if (!QDir().mkpath(QFileInfo(outPath).absolutePath())) {
            failure = tr("Cannot create directory: %1")
                          .arg(QFileInfo(outPath).absolutePath());
            emit errorOccurred(ArchiveErrorCode::PermissionDenied, failure);
            break;
        }

        if (type == AE_IFLNK) {
            const char *linkTarget = archive_entry_symlink(entry);
            const QString target = linkTarget ? decodeLinkTarget(QByteArray(linkTarget)) : QString();
            if (isUnderSkipped(relative, skippedSymlinks)) {
                LOG_INFO("LibArchiveBackend", "Skipped unsafe symlink by user: %s",
                         relative.toUtf8().constData());
                emit outputReady(tr("Skipped unsafe symlink: %1").arg(relative));
                continue;
            }
            if (!FileUtils::isSafeSymlinkTarget(relative, target)) {
                FileUtils::removeTemporaryDirectory(tempDir);
                emit errorOccurred(ArchiveErrorCode::PathTraversalBlocked,
                                   tr("Blocked unsafe symbolic link: %1").arg(relative));
                return;
            }
#ifdef Q_OS_WIN
            const bool linkCreated = createSymbolicLink(target, outPath);
#else
            const bool linkCreated = QFile::link(target, outPath);
#endif
            if (!linkCreated) {
                FileUtils::removeTemporaryDirectory(tempDir);
                emit errorOccurred(ArchiveErrorCode::PermissionDenied,
                                   tr("Cannot create symbolic link: %1").arg(outPath));
                return;
            }
            continue;
        }

        if (const char *hardTarget = archive_entry_hardlink(entry)) {
            const QString target = FileUtils::normalizeEntryPath(
                decodeEntryString(QByteArray(hardTarget)));
            if (target.isEmpty()
                || FileUtils::isPathTraversal(target, tempDir)
                || FileUtils::isPathTraversal(relative, tempDir)) {
                FileUtils::removeTemporaryDirectory(tempDir);
                emit errorOccurred(ArchiveErrorCode::HardLinkUnsupported,
                                   tr("Blocked unsafe hard link: %1").arg(relative));
                return;
            }

            const QString targetPath = QDir(tempDir).filePath(target);
            const QFileInfo targetInfo(targetPath);
            if (targetInfo.exists() && !targetInfo.isSymLink()
                && targetInfo.isFile()) {
                if (!createHardLink(targetPath, outPath)) {
                    FileUtils::removeTemporaryDirectory(tempDir);
                    emit errorOccurred(
                        ArchiveErrorCode::HardLinkUnsupported,
                        tr("Cannot create hard link %1 -> %2")
                            .arg(relative, target));
                    return;
                }
            } else {
                // The target may appear later in the archive; defer it.
                // 目标可能稍后才出现在压缩包中；延迟创建。
                pendingHardLinks.append({relative, target});
            }
            continue;
        }

        QFile outFile(outPath);

        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            failure = tr("Cannot write file: %1").arg(outPath);
            emit errorOccurred(ArchiveErrorCode::PermissionDenied, failure);
            break;
        }

        const void *buffer = nullptr;
        size_t size = 0;
        la_int64_t offset = 0;
        int readResult = ARCHIVE_OK;

        while ((readResult = archive_read_data_block(reader.get(), &buffer, &size,
                                                    &offset)) == ARCHIVE_OK) {
            if (isCancelled()) {
                cancelled = true;
                break;
            }
            if (offset > outFile.pos() && !outFile.seek(offset)) {
                failure = tr("Cannot write file: %1").arg(outPath);
                break;
            }
            const char *data = static_cast<const char *>(buffer);
            size_t remaining = size;
            while (remaining > 0) {
                const qint64 chunk = outFile.write(data, static_cast<qint64>(remaining));
                if (chunk <= 0) {
                    failure = tr("Cannot write file: %1").arg(outPath);
                    break;
                }
                data += static_cast<qsizetype>(chunk);
                remaining -= static_cast<size_t>(chunk);
                writtenBytes += chunk;
            }
            if (!failure.isEmpty() || cancelled) {
                break;
            }

            int percent = 0;
            if (totalBytes > 0) {
                percent = static_cast<int>(qMin<qint64>(100, writtenBytes * 100 / totalBytes));
            } else if (totalEntries > 0) {
                percent = qMin(100, currentEntry * 100 / totalEntries);
            }
            emit progressChanged(percent, totalEntries, currentEntry, -1);
        }

        outFile.close();

        const auto mtime = archive_entry_mtime(entry);
        if (mtime > 0) {
            QFile written(outPath);
            written.open(QIODevice::ReadWrite);
            written.setFileTime(QDateTime::fromSecsSinceEpoch(mtime),
                                QFileDevice::FileModificationTime);
            written.close();
        }

        if (!failure.isEmpty() || cancelled) {
            break;
        }
        if (readResult != ARCHIVE_EOF) {
            failure = libarchiveError(reader.get());
            // Password failures must go through reportFailure() so the UI is
            // asked for a passphrase instead of seeing a corruption error.
            // 密码错误必须经过 reportFailure()，以便界面提示输入密码，
            // 而不是显示为压缩包损坏。
            reportFailure(failure, options.password, tr("Extraction failed"));
            break;
        }
    }

    if (!pendingHardLinks.isEmpty() && !cancelled && failure.isEmpty()) {
        QList<QPair<QString, QString>> remaining = pendingHardLinks;
        while (!remaining.isEmpty()) {
            QList<QPair<QString, QString>> next;
            bool progress = false;
            for (const auto &[relative, target] : remaining) {
                const QString targetPath = QDir(tempDir).filePath(target);
                const QFileInfo targetInfo(targetPath);
                if (!targetInfo.exists() || targetInfo.isSymLink()
                    || !targetInfo.isFile()) {
                    next.append({relative, target});
                    continue;
                }
                const QString linkPath = QDir(tempDir).filePath(relative);
                if (!createHardLink(targetPath, linkPath)) {
                    FileUtils::removeTemporaryDirectory(tempDir);
                    emit errorOccurred(
                        ArchiveErrorCode::HardLinkUnsupported,
                        tr("Cannot create hard link %1 -> %2")
                            .arg(relative, target));
                    return;
                }
                progress = true;
            }
            if (!progress) {
                FileUtils::removeTemporaryDirectory(tempDir);
                emit errorOccurred(
                    ArchiveErrorCode::HardLinkUnsupported,
                    tr("Hard link target is missing or unsafe: %1")
                        .arg(next.first().second));
                return;
            }
            remaining = next;
        }
    }

    if (cancelled) {
        FileUtils::removeTemporaryDirectory(tempDir);
        emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
        return;
    }
    if (failure.isEmpty() && result != ARCHIVE_EOF) {
        const QString message = libarchiveError(reader.get());
        reader.reset();
        FileUtils::removeTemporaryDirectory(tempDir);
        reportFailure(message, options.password, tr("Extraction failed"));
        return;
    }
    if (!failure.isEmpty()) {
        FileUtils::removeTemporaryDirectory(tempDir);
        return;
    }

    if (!FileUtils::unsafeExtractedSymlinks(tempDir).isEmpty()) {
        FileUtils::removeTemporaryDirectory(tempDir);
        emit errorOccurred(ArchiveErrorCode::PathTraversalBlocked,
                           tr("Blocked unsafe symbolic link during extraction"));
        return;
    }

    const int failedMoves = FileUtils::moveExtractedContents(
        tempDir, options.destDir, options.overwrite, options.preservePaths);
    if (failedMoves > 0) {
        FileUtils::removeTemporaryDirectory(tempDir);
        emit errorOccurred(ArchiveErrorCode::PartialExtraction,
                           tr("Failed to move %1 extracted entries to %2. "
                              "Files already moved were not rolled back.")
                               .arg(failedMoves)
                               .arg(options.destDir));
        return;
    }

    emit progressChanged(100, totalEntries > 0 ? totalEntries : 1,
                         totalEntries > 0 ? totalEntries : 1, -1);
    emit operationFinished(true, tr("Extraction completed"));
}

void LibArchiveBackend::compress(const CompressOptions &)
{
    emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                       tr("Creating archives is not supported by the libarchive backend"));
}

void LibArchiveBackend::addFiles(const AddFilesOptions &)
{
    emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                       tr("Adding files is not supported by the libarchive backend"));
}

void LibArchiveBackend::deleteFiles(const DeleteFilesOptions &)
{
    emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                       tr("Deleting files is not supported by the libarchive backend"));
}

void LibArchiveBackend::testArchive(const TestOptions &options)
{
    if (!m_isOpen) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("Archive not opened"));
        return;
    }

    QString error;
    ReaderPtr reader = openReader(m_archivePath, m_format, options.password, &error);
    if (!reader) {
        reportFailure(error, options.password, tr("Cannot open archive"));
        return;
    }

    struct archive_entry *entry = nullptr;
    int result = ARCHIVE_OK;
    int tested = 0;

    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        if (isCancelled()) {
            emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
            return;
        }

        const void *buffer = nullptr;
        size_t size = 0;
        la_int64_t offset = 0;
        int readResult = ARCHIVE_OK;
        while ((readResult = archive_read_data_block(reader.get(), &buffer, &size,
                                                    &offset)) == ARCHIVE_OK) {
            // Data is read (and therefore CRC-checked) but never stored.
            // 读取数据（因此会校验 CRC），但不写入磁盘。
            if (isCancelled()) {
                emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
                return;
            }
        }
        if (readResult != ARCHIVE_EOF) {
            const QString message = libarchiveError(reader.get());
            reader.reset();
            reportFailure(message, options.password, tr("Integrity test failed"));
            return;
        }
        ++tested;
    }

    if (result != ARCHIVE_EOF) {
        const QString message = libarchiveError(reader.get());
        reader.reset();
        reportFailure(message, options.password, tr("Integrity test failed"));
        return;
    }

    LOG_INFO("LibArchiveBackend", "Verified %d entries in %s", tested,
             m_archivePath.toUtf8().constData());
    emit operationFinished(true, tr("Archive integrity verified"));
}

void LibArchiveBackend::closeArchive()
{
    m_isOpen = false;
    m_archivePath.clear();
    m_entries.clear();
}
