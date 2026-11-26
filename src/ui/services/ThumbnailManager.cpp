// QArchiver / 小Q解压
// Thumbnail extraction/cache implementation.
// 缩略图解压与缓存实现。

#include "ThumbnailManager.h"

#include "backends/ArchiveBackend.h"
#include "core/ArchiveBackendFactory.h"
#include "common/ArchiveOptions.h"
#include "ui/icons/IconUtils.h"
#include "utils/FileUtils.h"
#include "utils/Logger.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMetaObject>
#include <QPainter>
#include <QPixmap>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>
#include <utility>

namespace {
constexpr int kThumbnailSize = 128;
constexpr int kMaxSourceSize = 512;
// Bump when the thumbnail rendering changes so stale disk caches are ignored.
// 缩略图渲染方式变化时递增，避免复用旧缓存。
constexpr int kThumbnailCacheVersion = 2;

QString cacheRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("thumbnails"));
}

QString archiveIdentity(const QString &archivePath)
{
    const QFileInfo info(archivePath);
    return archivePath + QLatin1Char('|')
        + QString::number(info.size()) + QLatin1Char('|')
        + QString::number(info.lastModified().toMSecsSinceEpoch());
}

QString thumbnailKey(const QString &archivePath, const QString &entryPath,
                     qint64 entrySize)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(
            (archiveIdentity(archivePath) + QLatin1Char('|') + entryPath
             + QLatin1Char('|') + QString::number(entrySize)
             + QLatin1Char('|') + QString::number(kThumbnailCacheVersion))
                .toUtf8(),
            QCryptographicHash::Md5).toHex());
}

QString localThumbnailKey(const QString &filePath, qint64 fileSize,
                          const QDateTime &modified)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(
            (QStringLiteral("local|") + filePath + QLatin1Char('|')
             + QString::number(fileSize) + QLatin1Char('|')
             + QString::number(modified.toMSecsSinceEpoch()) + QLatin1Char('|')
             + QString::number(kThumbnailCacheVersion))
                .toUtf8(),
            QCryptographicHash::Md5).toHex());
}

bool isSvgFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix();
    return suffix.compare(QLatin1String("svg"), Qt::CaseInsensitive) == 0
        || suffix.compare(QLatin1String("svgz"), Qt::CaseInsensitive) == 0;
}

// Render a raster image through QImageReader.  The reader is also used as a
// fallback for mislabelled SVG files.
// 通过 QImageReader 读取位图；同时作为后缀写成 SVG 的位图文件的兜底。
QImage readRasterImage(const QString &sourcePath)
{
    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()
        && (sourceSize.width() > kMaxSourceSize
            || sourceSize.height() > kMaxSourceSize)) {
        reader.setScaledSize(sourceSize.scaled(
            kMaxSourceSize, kMaxSourceSize, Qt::KeepAspectRatio));
    }
    return reader.read();
}

// SVG is vector data, so render it directly at thumbnail resolution instead of
// going through the Qt SVG image-format plugin.  This keeps thumbnails working
// on deployments that ship Qt6Svg but forget imageformats/libqsvg.
// SVG 是矢量数据，直接按缩略图尺寸渲染，不依赖 Qt SVG 图片格式插件。
// 这样即使部署时遗漏 imageformats/libqsvg，缩略图仍然可用。
QImage readSvgImage(const QString &sourcePath)
{
    QSvgRenderer renderer(sourcePath);
    if (!renderer.isValid()) {
        return {};
    }

    QSize sourceSize = renderer.defaultSize();
    if (sourceSize.isEmpty()) {
        sourceSize = renderer.viewBoxF().size().toSize();
    }
    if (sourceSize.isEmpty()) {
        sourceSize = QSize(kThumbnailSize, kThumbnailSize);
    }

    // Render at the final box size; the common scaling below then only keeps
    // the aspect ratio.  Clamp both dimensions so fractional view boxes and
    // extreme panoramas never produce a zero-sized image.
    // 直接按最终缩略图框渲染；下方通用缩放只负责保持比例。宽高至少为 1，
    // 避免小数 viewBox 或极扁全景图产生 0 尺寸图片。
    QSize renderSize = sourceSize.scaled(
        kThumbnailSize, kThumbnailSize, Qt::KeepAspectRatio);
    renderSize.setWidth(qMax(1, renderSize.width()));
    renderSize.setHeight(qMax(1, renderSize.height()));

    QImage image(renderSize, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return {};
    }
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(renderSize)));
    painter.end();
    return image;
}

QImage readThumbnailSource(const QString &sourcePath)
{
    if (isSvgFile(sourcePath)) {
        const QImage image = readSvgImage(sourcePath);
        if (!image.isNull()) {
            return image;
        }
        // A file named .svg can still be a raster image with a wrong suffix,
        // so let QImageReader try before reporting failure.
        // 后缀为 .svg 的文件也可能实际是位图，失败时再交给 QImageReader 尝试。
    }
    return readRasterImage(sourcePath);
}

// Render @p sourcePath into the persistent cache.  Shared by archive entries
// (after extraction) and local files (read directly).
// 把 @p sourcePath 渲染到持久缓存；压缩包条目（解压后）与本地文件（直接读取）
// 共用这段逻辑。
bool writeThumbnail(const QString &sourcePath, const QString &cacheFile)
{
    const QImage image = readThumbnailSource(sourcePath);
    if (image.isNull()) {
        return false;
    }

    // Keep the original aspect ratio: fit inside the thumbnail box instead of
    // cropping/expanding it into a square.
    // 保持原始比例：缩放至缩略图框内，而不是裁剪成正方形。
    const QImage thumbnail = image.scaled(
        kThumbnailSize, kThumbnailSize, Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    const QString temporary = cacheFile + QStringLiteral(".tmp");
    if (thumbnail.isNull() || !thumbnail.save(temporary, "PNG")) {
        return false;
    }
    QFile::remove(cacheFile);
    return QFile::rename(temporary, cacheFile);
}
} // namespace

ThumbnailManager::ThumbnailManager(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(2);
}

ThumbnailManager &ThumbnailManager::instance()
{
    static ThumbnailManager *manager = nullptr;
    if (!manager) {
        manager = new ThumbnailManager;
        // Destroy the manager while QCoreApplication (and the GUI platform
        // integration) still exists: the worker pool and the cached QIcons
        // must not outlive it on shutdown.
        // 在 QCoreApplication（及 GUI 平台集成）仍存在时销毁管理器：工作线程池
        // 与缓存的 QIcon 不能比它更晚析构。
        if (QCoreApplication::instance()) {
            qAddPostRoutine([] {
                delete manager;
                manager = nullptr;
            });
        }
    }
    return *manager;
}

QIcon ThumbnailManager::icon(const QString &archivePath, ArchiveFormat format,
                             const QString &entryPath, qint64 entrySize,
                             const QString &password)
{
    if (archivePath.isEmpty() || entryPath.isEmpty()) {
        return Icons::fileType(QStringLiteral("file-image.png"));
    }

    const QString key = thumbnailKey(archivePath, entryPath, entrySize);
    if (const auto it = m_icons.constFind(key); it != m_icons.constEnd()) {
        return it.value();
    }
    if (m_failed.contains(key)) {
        return Icons::fileType(QStringLiteral("file-image.png"));
    }

    const QString diskFile = QDir(cacheRoot()).filePath(key
                                                        + QStringLiteral(".png"));
    if (QFileInfo::exists(diskFile)) {
        QPixmap pixmap;
        if (pixmap.load(diskFile)) {
            const QIcon icon(pixmap);
            m_icons.insert(key, icon);
            return icon;
        }
    }

    if (!m_pending.contains(key)) {
        m_pending.insert(key);
        requestThumbnail(key, diskFile, archivePath, entryPath, entrySize,
                         format, password);
    }
    return Icons::fileType(QStringLiteral("file-image.png"));
}

QIcon ThumbnailManager::localIcon(const QString &filePath, qint64 fileSize,
                                  const QDateTime &modified)
{
    if (filePath.isEmpty()) {
        return Icons::fileType(QStringLiteral("file-image.png"));
    }

    const QString key = localThumbnailKey(filePath, fileSize, modified);
    if (const auto it = m_icons.constFind(key); it != m_icons.constEnd()) {
        return it.value();
    }
    if (m_failed.contains(key)) {
        return Icons::fileType(QStringLiteral("file-image.png"));
    }

    const QString diskFile = QDir(cacheRoot()).filePath(key
                                                        + QStringLiteral(".png"));
    if (QFileInfo::exists(diskFile)) {
        QPixmap pixmap;
        if (pixmap.load(diskFile)) {
            const QIcon icon(pixmap);
            m_icons.insert(key, icon);
            return icon;
        }
    }

    if (!m_pending.contains(key)) {
        m_pending.insert(key);
        requestLocalThumbnail(key, diskFile, filePath);
    }
    return Icons::fileType(QStringLiteral("file-image.png"));
}

void ThumbnailManager::requestThumbnail(const QString &key,
                                        const QString &cacheFile,
                                        const QString &archivePath,
                                        const QString &entryPath,
                                        qint64 entrySize,
                                        ArchiveFormat format,
                                        const QString &password)
{
    Q_UNUSED(entrySize);
    const QString normalized = FileUtils::normalizeEntryPath(entryPath);
    if (normalized.isEmpty()) {
        m_pending.remove(key);
        m_failed.insert(key);
        return;
    }

    m_batchQueue.append({key, cacheFile, archivePath, entryPath, normalized,
                         format, password});
    if (!m_batchTimer) {
        m_batchTimer = new QTimer(this);
        m_batchTimer->setSingleShot(true);
        m_batchTimer->setInterval(0);
        connect(m_batchTimer, &QTimer::timeout, this,
                &ThumbnailManager::dispatchThumbnailBatch);
    }
    if (!m_batchTimer->isActive()) {
        m_batchTimer->start();
    }
}

void ThumbnailManager::dispatchThumbnailBatch()
{
    if (m_batchQueue.isEmpty()) {
        return;
    }
    const QList<ThumbnailRequest> requests = m_batchQueue;
    m_batchQueue.clear();
    const quint64 generation = m_generation.load();

    // QThreadPool::start() is the right API here: the task has no result and
    // QtConcurrent::run() would return a nodiscard QFuture.  The pool keeps the
    // existing two-thread limit.
    // 任务没有返回值；这里用 QThreadPool::start()，避免忽略
    // QtConcurrent::run() 的 nodiscard QFuture；线程池保留原有两线程上限。
    m_pool.start([this, requests, generation]() {
        // One backend call per archive/password combination.
        // 每个“归档 + 密码”组合只调用一次后端。
        QHash<QString, QList<ThumbnailRequest>> groups;
        for (const ThumbnailRequest &request : requests) {
            const QString groupKey = request.archivePath + QLatin1Char('|')
                + QString::number(static_cast<int>(request.format))
                + QLatin1Char('|') + request.password;
            groups[groupKey].append(request);
        }

        QList<ThumbnailResult> results;
        results.reserve(requests.size());

        for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
            const QList<ThumbnailRequest> &group = it.value();
            for (int offset = 0; offset < group.size();
                 offset += kMaxBatchMembers) {
                // A context switch cancels the remaining batches early instead
                // of extracting thumbnails the user no longer sees.
                // 上下文切换后提前结束剩余批次，不再解压用户已经看不到的缩略图。
                if (generation != m_generation.load()) {
                    break;
                }

                const QList<ThumbnailRequest> chunk =
                    group.mid(offset, kMaxBatchMembers);
                const ThumbnailRequest &first = chunk.first();

                bool backendSuccess = false;
                QString errorMessage;
                QTemporaryDir tempDir(QDir(cacheRoot()).filePath(
                    QStringLiteral("extract-XXXXXX")));

                if (tempDir.isValid()) {
                    std::unique_ptr<ArchiveBackend> backend(
                        ArchiveBackendFactory::createForRead(
                            first.format, first.archivePath));
                    if (backend) {
                        {
                            std::lock_guard<std::mutex> lock(m_activeBatchMutex);
                            m_activeBatchBackend = backend.get();
                        }
                        QObject::connect(
                            backend.get(), &ArchiveBackend::operationFinished,
                            backend.get(),
                            [&backendSuccess, &errorMessage](
                                bool ok, const QString &message) {
                                backendSuccess = ok;
                                if (!ok) {
                                    errorMessage = message;
                                }
                            },
                            Qt::DirectConnection);
                        QObject::connect(
                            backend.get(), &ArchiveBackend::errorOccurred,
                            backend.get(),
                            [&backendSuccess, &errorMessage](
                                ArchiveErrorCode, const QString &message) {
                                backendSuccess = false;
                                errorMessage = message;
                            },
                            Qt::DirectConnection);

                        backend->openArchive(first.archivePath);
                        ExtractOptions options;
                        options.destDir = tempDir.path();
                        options.preservePaths = true;
                        options.overwrite = OverwritePolicy::Always;
                        options.password = first.password;
                        for (const ThumbnailRequest &request : chunk) {
                            options.selectedFiles << request.entryPath;
                        }
                        backend->extract(options);
                        backend->closeArchive();

                        // Detach before the unique_ptr destroys the backend;
                        // clearArchiveCache() may only cancel a live object.
                        // 在 unique_ptr 销毁后端前解除登记；clearArchiveCache()
                        // 只能对仍存活的对象请求取消。
                        {
                            std::lock_guard<std::mutex> lock(m_activeBatchMutex);
                            m_activeBatchBackend = nullptr;
                        }
                    }
                }
                Q_UNUSED(errorMessage);

                QHash<QString, QString> byBaseName;
                bool scannedForBaseNames = false;
                for (const ThumbnailRequest &request : chunk) {
                    QDir().mkpath(QFileInfo(request.cacheFile).absolutePath());
                    bool success = false;
                    if (tempDir.isValid() && backendSuccess
                        && generation == m_generation.load()) {
                        QString extracted =
                            QDir(tempDir.path()).filePath(request.normalized);
                        if (!QFileInfo::exists(extracted)) {
                            // Legacy-codepage names may not round-trip; fall
                            // back to the base name, scanning the tree only once
                            // per batch.
                            // 旧代码页文件名可能无法往返，退回按文件名匹配；
                            // 每批只扫描一次目录树。
                            if (!scannedForBaseNames) {
                                QDirIterator scan(
                                    tempDir.path(), QDir::Files,
                                    QDirIterator::Subdirectories);
                                while (scan.hasNext()) {
                                    const QString candidate = scan.next();
                                    byBaseName.insert(
                                        QFileInfo(candidate).fileName(),
                                        candidate);
                                }
                                scannedForBaseNames = true;
                            }
                            extracted = byBaseName.value(
                                QFileInfo(request.normalized).fileName());
                        }
                        if (!extracted.isEmpty()
                            && QFileInfo::exists(extracted)) {
                            success =
                                writeThumbnail(extracted, request.cacheFile);
                        }
                    }
                    results.append({request.key, request.cacheFile,
                                    request.archivePath, request.entryPath,
                                    success});
                }
            }
        }

        QMetaObject::invokeMethod(
            this,
            [this, results, generation]() {
                const quint64 current = m_generation.load();
                for (const ThumbnailResult &result : results) {
                    m_pending.remove(result.key);
                    if (generation != current) {
                        continue;
                    }
                    if (!result.success) {
                        m_failed.insert(result.key);
                        continue;
                    }
                    QPixmap pixmap;
                    if (pixmap.load(result.cacheFile)) {
                        m_icons.insert(result.key, QIcon(pixmap));
                        emit thumbnailReady(result.archivePath, result.entryPath);
                    } else {
                        m_failed.insert(result.key);
                    }
                }
            },
            Qt::QueuedConnection);
    });
}

void ThumbnailManager::requestLocalThumbnail(const QString &key,
                                             const QString &cacheFile,
                                             const QString &filePath)
{
    const quint64 generation = m_generation.load();
    m_pool.start([this, key, cacheFile, filePath, generation]() {
        QDir().mkpath(QFileInfo(cacheFile).absolutePath());
        const bool success = writeThumbnail(filePath, cacheFile);

        QMetaObject::invokeMethod(
            this,
            [this, key, cacheFile, filePath, success, generation]() {
                m_pending.remove(key);
                if (generation != m_generation.load()) {
                    return;
                }
                if (success) {
                    QPixmap pixmap;
                    if (pixmap.load(cacheFile)) {
                        m_icons.insert(key, QIcon(pixmap));
                        emit localThumbnailReady(filePath);
                    } else {
                        m_failed.insert(key);
                    }
                } else {
                    m_failed.insert(key);
                }
            },
            Qt::QueuedConnection);
    });
}

void ThumbnailManager::clearFailures()
{
    m_failed.clear();
}

void ThumbnailManager::clearArchiveCache()
{
    m_generation.fetch_add(1);
    // Stop the batch that is already running: the user switched context, so
    // extracting the rest of the old archive only wastes a helper process.
    // 终止已经在运行的批次：用户已切换上下文，继续解压旧压缩包只会浪费
    // 辅助进程。
    {
        std::lock_guard<std::mutex> lock(m_activeBatchMutex);
        if (m_activeBatchBackend) {
            m_activeBatchBackend->requestCancel();
        }
    }
    // Drop requests that have not been dispatched yet; their keys must not
    // stay in m_pending or they would never be retried.
    // 丢弃尚未派发的请求；它们的键不能留在 m_pending，否则再也不会重试。
    for (const ThumbnailRequest &request : std::as_const(m_batchQueue)) {
        m_pending.remove(request.key);
    }
    m_batchQueue.clear();
    m_icons.clear();
    m_failed.clear();
    // m_pending is kept: a worker started for the previous context still needs
    // its deduplication slot until it posts its completion event.
    // 保留 m_pending：上一上下文启动的任务在投递完成事件前仍需去重占位。
}

void ThumbnailManager::cleanupStale()
{
    QDir root(cacheRoot());
    if (!root.exists()) {
        return;
    }
    const QStringList leftovers = root.entryList(
        {QStringLiteral("extract-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : leftovers) {
        if (QDir(root.filePath(entry)).removeRecursively()) {
            LOG_DEBUG("ThumbnailManager", "Removed stale extraction dir: %s",
                      entry.toUtf8().constData());
        }
    }
}

void ThumbnailManager::cleanupOwn()
{
    clearArchiveCache();
    // Thumbnail tasks are short; waiting avoids a worker posting into a
    // half-destroyed manager during shutdown.
    // 缩略图任务很短；等待可避免关闭期间工作线程向已析构的管理器投递事件。
    m_pool.waitForDone(2000);
}
