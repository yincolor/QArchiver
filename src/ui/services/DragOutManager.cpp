// QArchiver / 小Q解压
// Drag-out staging, cache cleanup and cancellation.
// 拖出暂存、缓存清理与取消处理。

#include "core/ArchiveBackendFactory.h"
#include "ui/services/DragOutManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtConcurrent>

#include <algorithm>
#include <limits>
#include <memory>

#include "backends/ArchiveBackend.h"
#include "core/ArchiveManager.h"
#include "utils/FileUtils.h"
#include "utils/Logger.h"

namespace {
constexpr qint64 kDragSpaceMargin = 16LL * 1024 * 1024;

// The libarchive backend extracts into a temporary sub-directory inside the
// destination and moves the result afterwards, so the staging needs roughly
// twice the unpacked size of the selection.
constexpr int kStagingSpaceFactor = 2;
} // namespace

DragOutManager &DragOutManager::instance()
{
    static DragOutManager manager;
    return manager;
}

DragOutManager::DragOutManager()
    : QObject(nullptr)
{
}

bool DragOutManager::isRetryableWithCli(ArchiveErrorCode code)
{
    // Mirrors ArchiveManager::tryLibArchiveFallback(): everything that the
    // command line tools can plausibly read differently is worth one retry.
    // Password problems are excluded - the caller must ask the user first.
    switch (code) {
    case ArchiveErrorCode::UnsupportedFormat:
    case ArchiveErrorCode::EncodingError:
    case ArchiveErrorCode::CorruptedArchive:
    case ArchiveErrorCode::VolumeMissing:
    case ArchiveErrorCode::Unknown:
        return true;
    default:
        return false;
    }
}

QString DragOutManager::dragRoot() const
{
    const QString cache =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cache.isEmpty()) {
        return QString();
    }
    return QDir(cache).filePath(QStringLiteral("drag"));
}

QString DragOutManager::createStagingRoot() const
{
    const QString root = dragRoot();
    if (root.isEmpty()) {
        return QString();
    }

    const QString pidRoot =
        QDir(root).filePath(QString::number(QCoreApplication::applicationPid()));
    if (!QDir().mkpath(pidRoot)) {
        return QString();
    }

    QTemporaryDir dir(QDir(pidRoot).filePath(QStringLiteral("XXXXXX")));
    if (!dir.isValid()) {
        return QString();
    }
    // Keep the directory after the QTemporaryDir goes out of scope: the drop
    // target may still be copying while the QDrag lives on.
    dir.setAutoRemove(false);
    const QString path = dir.path();
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                    | QFileDevice::ExeOwner);
    return path;
}

QStringList DragOutManager::pruneNestedPaths(const QStringList &paths)
{
    struct Item {
        QString original;
        QString normalized;
    };

    QList<Item> items;
    QSet<QString> seen;
    for (const QString &path : paths) {
        const QString normalized = FileUtils::normalizeEntryPath(path);
        if (normalized.isEmpty() || normalized == QStringLiteral(".")
            || seen.contains(normalized)) {
            continue;
        }
        seen.insert(normalized);
        items.append({path, normalized});
    }

    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return a.normalized < b.normalized;
    });

    // Sorted order puts every parent immediately before its descendants, so
    // one linear pass suffices instead of comparing every pair.
    // 排序后父项紧邻其所有后代，因此只需一次线性扫描，无需两两比较。
    QStringList result;
    QString lastKept;
    for (const Item &item : items) {
        if (!lastKept.isEmpty()
            && item.normalized.startsWith(lastKept + QLatin1Char('/'))) {
            continue;
        }
        result << item.original;
        lastKept = item.normalized;
    }
    return result;
}

qint64 DragOutManager::selectedUncompressedSize(const QList<ArchiveEntry> &entries,
                                               const QStringList &paths)
{
    QSet<QString> selected;
    for (const QString &path : paths) {
        const QString normalized = FileUtils::normalizeEntryPath(path);
        if (!normalized.isEmpty() && normalized != QStringLiteral(".")) {
            selected.insert(normalized);
        }
    }
    if (selected.isEmpty()) {
        return 0;
    }

    qint64 total = 0;
    QSet<QString> counted;
    for (const ArchiveEntry &entry : entries) {
        if (entry.isDir) {
            continue;
        }
        const QString path = FileUtils::normalizeEntryPath(entry.path);
        if (path.isEmpty() || counted.contains(path)) {
            continue;
        }

        bool matched = selected.contains(path);
        if (!matched) {
            // A selected directory covers all of its descendants.  Checking
            // the path's ancestors is O(depth) and avoids scanning every
            // selected item for every entry.
            // 选中目录覆盖其所有后代。逐级检查祖先只需 O(深度)，
            // 避免为每个条目扫描全部选中项。
            const QStringList components =
                path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
            QString ancestor;
            for (int i = 0; i + 1 < components.size(); ++i) {
                ancestor = ancestor.isEmpty()
                    ? components.at(i)
                    : ancestor + QLatin1Char('/') + components.at(i);
                if (selected.contains(ancestor)) {
                    matched = true;
                    break;
                }
            }
        }
        if (matched) {
            counted.insert(path);
            if (!FileUtils::addSizeChecked(total, entry.size)) {
                return -1;
            }
        }
    }
    return total;
}

QString DragOutManager::findStagedPathFallback(const QString &stagingRoot,
                                              const QString &normalized)
{
    if (stagingRoot.isEmpty() || normalized.isEmpty()) {
        return QString();
    }

    const QString wantedName = QFileInfo(normalized).fileName();
    QDir root(stagingRoot);

    QString byName;
    int nameMatches = 0;

    QDirIterator it(stagingRoot,
                    QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QString relative = root.relativeFilePath(path);
        relative.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (relative == normalized) {
            return path;
        }
        if (!wantedName.isEmpty() && QFileInfo(path).fileName() == wantedName) {
            ++nameMatches;
            byName = path;
        }
    }
    return nameMatches == 1 ? byName : QString();
}

QStringList DragOutManager::mapStagedPaths(const QString &stagingRoot,
                                          const QStringList &selectedPaths)
{
    QStringList result;
    QSet<QString> seen;

    for (const QString &path : selectedPaths) {
        const QString normalized = FileUtils::normalizeEntryPath(path);
        if (normalized.isEmpty() || normalized == QStringLiteral(".")) {
            continue;
        }

        QString candidate = QDir(stagingRoot).filePath(normalized);
        QFileInfo info(candidate);
        if (!info.exists() && !info.isSymLink()) {
            candidate = findStagedPathFallback(stagingRoot, normalized);
            info = QFileInfo(candidate);
        }
        if (!info.exists() && !info.isSymLink()) {
            continue;
        }

        const QString absolute = info.absoluteFilePath();
        if (!seen.contains(absolute)) {
            seen.insert(absolute);
            result << absolute;
        }
    }

    return result;
}

QStringList DragOutManager::topLevelStagedPaths(const QString &stagingRoot)
{
    QStringList result;
    QDir dir(stagingRoot);
    const QStringList names =
        dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QString &name : names) {
        if (name.startsWith(QStringLiteral(".tmp_extract_"))) {
            continue;
        }
        result << dir.absoluteFilePath(name);
    }
    return result;
}

QString DragOutManager::cacheKey(const QString &archivePath,
                                 ArchiveFormat format,
                                 const QStringList &paths,
                                 const QString &password)
{
    // The archive modification time is part of the key: after a re-compress or
    // an add/delete operation the staged files may be stale.
    const qint64 mtime = QFileInfo(archivePath).lastModified().toMSecsSinceEpoch();
    const QByteArray passwordHash = QCryptographicHash::hash(
        password.toUtf8(), QCryptographicHash::Sha1);

    QStringList sorted = paths;
    sorted.sort();

    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(archivePath)
        .arg(mtime)
        .arg(static_cast<int>(format))
        .arg(QString::fromLatin1(passwordHash.toHex()))
        .arg(sorted.join(QLatin1Char('\n')));
}

void DragOutManager::storePrepared(const QString &key,
                                   const DragOutPayload &payload)
{
    // A handful of recent preparations is enough to make retries instant
    // without letting the staging grow unbounded during a session.
    constexpr int kMaxCacheEntries = 4;
    if (!m_cache.contains(key) && m_cache.size() >= kMaxCacheEntries) {
        m_cache.erase(m_cache.begin());
    }
    m_cache.insert(key, CacheEntry{payload.stagingRoot, payload.localPaths});
}

bool DragOutManager::hasPreparedFor(const QString &archivePath,
                                    ArchiveFormat format,
                                    const QStringList &paths,
                                    const QString &password) const
{
    const auto it = m_cache.constFind(
        cacheKey(archivePath, format, pruneNestedPaths(paths), password));
    if (it == m_cache.constEnd()) {
        return false;
    }
    for (const QString &path : it->localPaths) {
        if (!QFileInfo::exists(path)) {
            return false;   // the drop target removed/moved the staged tree
        }
    }
    return !it->localPaths.isEmpty();
}

DragOutPayload DragOutManager::takePreparedFor(const QString &archivePath,
                                               ArchiveFormat format,
                                               const QStringList &paths,
                                               const QString &password)
{
    DragOutPayload payload;
    const QString key =
        cacheKey(archivePath, format, pruneNestedPaths(paths), password);
    const auto it = m_cache.constFind(key);
    if (it == m_cache.constEnd()) {
        return payload;
    }
    payload.stagingRoot = it->stagingRoot;
    payload.localPaths = it->localPaths;
    payload.success = true;
    return payload;
}

void DragOutManager::clearCache()
{
    // Only the index: see the header for why the staged files stay on disk.
    // 只清索引：暂存文件保留在磁盘上的原因见头文件说明。
    m_cache.clear();
}

DragOutPayload DragOutManager::extractForDrag(const QString &archivePath,
                                              ArchiveFormat format,
                                              const QList<ArchiveEntry> &entries,
                                              const QStringList &selectedPaths,
                                              const QString &password,
                                              const QString &encodingHint)
{
    DragOutPayload payload;

    if (archivePath.isEmpty() || format == ArchiveFormat::Unknown) {
        payload.errorMessage = tr("No archive is open.");
        return payload;
    }

    const QStringList paths = pruneNestedPaths(selectedPaths);
    if (paths.isEmpty()) {
        payload.errorMessage = tr("No entries selected.");
        return payload;
    }

    // Already staged (e.g. the previous drag attempt finished after the user
    // had released the button): reuse it instead of extracting again.
    if (hasPreparedFor(archivePath, format, paths, password)) {
        LOG_INFO("DragOutManager", "cache hit for %lld path(s)",
                 static_cast<long long>(paths.size()));
        return takePreparedFor(archivePath, format, paths, password);
    }
    LOG_INFO("DragOutManager", "staging %lld path(s) from %s (format %d)",
             static_cast<long long>(paths.size()),
             archivePath.toUtf8().constData(), static_cast<int>(format));

    const QString stagingRoot = createStagingRoot();
    if (stagingRoot.isEmpty()) {
        payload.errorCode = ArchiveErrorCode::PermissionDenied;
        payload.errorMessage =
            tr("Cannot create a temporary directory for dragging.");
        return payload;
    }
    payload.stagingRoot = stagingRoot;

    // 1. Reject paths that would escape the staging directory (Zip Slip).
    for (const QString &path : paths) {
        if (FileUtils::isPathTraversal(FileUtils::normalizeEntryPath(path),
                                       stagingRoot)) {
            QDir(stagingRoot).removeRecursively();
            payload.stagingRoot.clear();
            payload.errorCode = ArchiveErrorCode::PathTraversalBlocked;
            payload.errorMessage = tr("Blocked unsafe path: %1").arg(path);
            return payload;
        }
    }

    // 2. Reject unsafe symlinks in the selected subtrees before invoking a CLI
    //    extractor that could otherwise write through them.
    // 2. 在调用可能通过链接写入的命令行解压器之前，拒绝选中子树中的危险符号链接。
    for (const ArchiveEntry &entry : entries) {
        if (!entry.isSymLink
            || !FileUtils::isSafeSymlinkTarget(entry.path, entry.linkTarget)) {
            if (entry.isSymLink) {
                const QString normalized =
                    FileUtils::normalizeEntryPath(entry.path);
                for (const QString &path : paths) {
                    const QString selected =
                        FileUtils::normalizeEntryPath(path);
                    if (normalized == selected
                        || normalized.startsWith(selected + QLatin1Char('/'))
                        || selected.startsWith(normalized + QLatin1Char('/'))) {
                        QDir(stagingRoot).removeRecursively();
                        payload.stagingRoot.clear();
                        payload.errorCode =
                            ArchiveErrorCode::PathTraversalBlocked;
                        payload.errorMessage =
                            tr("Blocked unsafe symlink: %1").arg(entry.path);
                        return payload;
                    }
                }
            }
            continue;
        }
    }

    // 3. Disk space check (the staging holds the unpacked selection).
    const qint64 required = selectedUncompressedSize(entries, paths);
    const qint64 available = FileUtils::estimateFreeSpace(stagingRoot);
    constexpr qint64 maxSize = std::numeric_limits<qint64>::max();
    if (required < 0
        || (required > 0
            && required > (maxSize - kDragSpaceMargin) / kStagingSpaceFactor)) {
        QDir(stagingRoot).removeRecursively();
        payload.stagingRoot.clear();
        payload.errorCode = ArchiveErrorCode::InsufficientDiskSpace;
        payload.errorMessage =
            tr("Archive size is abnormal or too large to prepare safely.");
        return payload;
    }
    if (required > 0 && available >= 0
        && available < required * kStagingSpaceFactor + kDragSpaceMargin) {
        QDir(stagingRoot).removeRecursively();
        payload.stagingRoot.clear();
        payload.errorCode = ArchiveErrorCode::InsufficientDiskSpace;
        payload.errorMessage =
            tr("Insufficient disk space for drag preparation.");
        return payload;
    }

    auto context = std::make_shared<ExtractionContext>();
    m_context = context;

    struct ExtractResult {
        bool ok = false;
        bool cancelled = false;
        ArchiveErrorCode code = ArchiveErrorCode::Unknown;
        QString message;
    };

    emit stageMessage(tr("Extracting selected files..."));

    // Decided up front so the retry below only runs when the first attempt
    // really used libarchive (otherwise it would repeat the CLI work).
    const bool viaLibArchive =
        ArchiveBackendFactory::usesLibArchiveForRead(format, archivePath);

    QFuture<ExtractResult> future = QtConcurrent::run(
        [this, archivePath, format, paths, password, encodingHint, stagingRoot,
         context, viaLibArchive]() {
            // One full "open -> list -> extract" attempt.  A failed backend is
            // never reused, matching ArchiveManager's behaviour.
            auto runOnce = [&](std::unique_ptr<ArchiveBackend> backend) {
                ExtractResult result;
                if (!backend) {
                    result.code = ArchiveErrorCode::UnsupportedFormat;
                    result.message = QObject::tr("No backend available.");
                    return result;
                }

                context->backend.store(backend.get());
                if (context->cancelRequested.load()) {
                    backend->requestCancel();
                }

                // Progress signals originate in this worker thread; the
                // connection to the (GUI thread) manager is queued, so the
                // nested event loop can deliver them to the progress dialog.
                QObject::connect(backend.get(), &ArchiveBackend::progressChanged,
                                 this, &DragOutManager::progressChanged,
                                 Qt::QueuedConnection);
                QObject::connect(backend.get(), &ArchiveBackend::outputReady,
                                 backend.get(),
                                 [](const QString &line) {
                                     LOG_DEBUG("DragOutManager", "%s",
                                               line.toUtf8().constData());
                                 },
                                 Qt::DirectConnection);
                QObject::connect(backend.get(), &ArchiveBackend::operationFinished,
                                 backend.get(),
                                 [&result](bool ok, const QString &msg) {
                                     result.ok = ok;
                                     if (!msg.isEmpty()) {
                                         result.message = msg;
                                     }
                                 },
                                 Qt::DirectConnection);
                QObject::connect(backend.get(), &ArchiveBackend::errorOccurred,
                                 backend.get(),
                                 [&result](ArchiveErrorCode code,
                                           const QString &msg) {
                                     result.ok = false;
                                     result.code = code;
                                     result.message = msg;
                                     result.cancelled =
                                         (code == ArchiveErrorCode::Cancelled);
                                 },
                                 Qt::DirectConnection);
                QObject::connect(backend.get(), &ArchiveBackend::passwordRequested,
                                 backend.get(),
                                 [&result]() {
                                     if (result.cancelled) {
                                         return;
                                     }
                                     result.ok = false;
                                     result.code = ArchiveErrorCode::PasswordRequired;
                                     result.message =
                                         QObject::tr("Password is required.");
                                 },
                                 Qt::DirectConnection);

                backend->openArchive(archivePath);

                // Fill the progress totals: the libarchive backend sizes its
                // progress from the entry list of the previous listing.
                ListOptions listOptions;
                listOptions.password = password;
                listOptions.encodingHint = encodingHint;
                if (!context->cancelRequested.load()) {
                    backend->listContents(listOptions);
                }

                ExtractOptions options;
                options.destDir = stagingRoot;
                options.selectedFiles = paths;
                options.preservePaths = true;
                options.overwrite = OverwritePolicy::Always;
                options.password = password;
                options.encodingHint = encodingHint;

                backend->extract(options);
                backend->closeArchive();
                context->backend.store(nullptr);
                return result;
            };

            // Read-path factory: decides between the in-process libarchive
            // backend and the command line tools (preference, environment
            // variable, format whitelist, split volumes).
            ExtractResult result = runOnce(std::unique_ptr<ArchiveBackend>(
                ArchiveBackendFactory::createForRead(format, archivePath)));

            // Private backends do not get ArchiveManager's automatic fallback,
            // so archives libarchive cannot read (encrypted 7z, exotic
            // filename encodings, ...) are retried once with the CLI backend.
            if (viaLibArchive && !result.ok && !result.cancelled
                && isRetryableWithCli(result.code)) {
                LOG_INFO("DragOutManager",
                         "libarchive could not extract %s (%s); retrying with CLI",
                         archivePath.toUtf8().constData(),
                         result.message.toUtf8().constData());
                emit stageMessage(
                    QObject::tr("Retrying with the command line backend..."));
                // Drop anything a partially successful attempt left behind.
                QDir(stagingRoot).removeRecursively();
                QDir().mkpath(stagingRoot);
                result = runOnce(std::unique_ptr<ArchiveBackend>(
                    ArchiveBackendFactory::create(format)));
            }
            return result;
        });

    // 3. Wait on the GUI thread, keeping the event loop alive so the progress
    //    dialog can update and the cancel button works.
    {
        QFutureWatcher<ExtractResult> watcher;
        QEventLoop loop;
        QObject::connect(&watcher, &QFutureWatcher<ExtractResult>::finished,
                         &loop, &QEventLoop::quit);
        watcher.setFuture(future);
        if (!watcher.isFinished()) {
            loop.exec();
        }
    }

    const ExtractResult result = future.result();
    m_context.reset();

    if (!result.ok) {
        QDir(stagingRoot).removeRecursively();
        payload.stagingRoot.clear();
        payload.errorCode = result.cancelled ? ArchiveErrorCode::Cancelled
                                            : result.code;
        payload.errorMessage = result.message.isEmpty()
            ? tr("Unable to prepare the selected files for dragging.")
            : result.message;
        payload.cancelled = result.cancelled;
        return payload;
    }

    emit stageMessage(tr("Finalizing drag data..."));

    // 4. Map archive paths to the staged files.
    payload.localPaths = mapStagedPaths(stagingRoot, paths);
    if (payload.localPaths.isEmpty()
        && (format == ArchiveFormat::Gzip || format == ArchiveFormat::Bzip2
            || format == ArchiveFormat::Xz || format == ArchiveFormat::Zstd)) {
        // Plain streams have no entry names: the backend writes a single file
        // with its own naming, so use whatever landed in the staging root.
        payload.localPaths = topLevelStagedPaths(stagingRoot);
    }
    if (payload.localPaths.isEmpty()) {
        QDir(stagingRoot).removeRecursively();
        payload.stagingRoot.clear();
        payload.errorCode = ArchiveErrorCode::Unknown;
        payload.errorMessage = tr("The selected entries were not extracted.");
        return payload;
    }
    if (payload.localPaths.size() != paths.size()) {
        LOG_WARN("DragOutManager", "Only %lld of %lld selected entries staged",
                 static_cast<long long>(payload.localPaths.size()),
                 static_cast<long long>(paths.size()));
    }

    payload.success = true;
    storePrepared(cacheKey(archivePath, format, paths, password), payload);
    m_retainedRoots.insert(stagingRoot);
    return payload;
}

void DragOutManager::cancel()
{
    const auto context = m_context;
    if (!context) {
        return;
    }
    context->cancelRequested.store(true);
    if (ArchiveBackend *backend = context->backend.load()) {
        backend->requestCancel();
    }
}

void DragOutManager::retire(const DragOutPayload &payload)
{
    if (payload.stagingRoot.isEmpty()) {
        return;
    }
    // Only remember it: the drop target may still be copying.  Everything is
    // removed in cleanupOwn() when the application exits.
    m_retainedRoots.insert(payload.stagingRoot);
}

void DragOutManager::cleanupStale()
{
    const QString root = dragRoot();
    if (root.isEmpty()) {
        return;
    }

    QDir dir(root);
    if (!dir.exists()) {
        return;
    }

    const QStringList pidDirs =
        dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : pidDirs) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);
        if (ok && pid > 0 && !FileUtils::isProcessRunning(pid)) {
            QDir(dir.absoluteFilePath(entry)).removeRecursively();
            LOG_DEBUG("DragOutManager", "Removed stale drag staging for PID %lld",
                      static_cast<long long>(pid));
        }
    }
}

void DragOutManager::cleanupOwn()
{
    const QString root = dragRoot();
    if (!root.isEmpty()) {
        const QString pidRoot = QDir(root).filePath(
            QString::number(QCoreApplication::applicationPid()));
        QDir(pidRoot).removeRecursively();
    }
    m_retainedRoots.clear();
    m_cache.clear();
}
