// QArchiver / 小Q解压
// Thumbnail extraction/cache implementation.
// 缩略图解压与缓存实现。

#include "ThumbnailManager.h"

#include "backends/ArchiveBackend.h"
#include "core/ArchiveBackendFactory.h"
#include "common/ArchiveOptions.h"
#include "ui/icons/IconUtils.h"
#include "utils/FileUtils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMetaObject>
#include <QPixmap>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <memory>

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

// Render @p sourcePath into the persistent cache.  Shared by archive entries
// (after extraction) and local files (read directly).
// 把 @p sourcePath 渲染到持久缓存；压缩包条目（解压后）与本地文件（直接读取）
// 共用这段逻辑。
bool writeThumbnail(const QString &sourcePath, const QString &cacheFile)
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
    const QImage image = reader.read();
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
    static ThumbnailManager manager;
    return manager;
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

    // QThreadPool::start() is the right API here: the task has no result and
    // QtConcurrent::run() would return a nodiscard QFuture.  Using the same
    // pool keeps the existing two-thread limit.
    // 任务没有返回值；这里用 QThreadPool::start()，避免忽略
    // QtConcurrent::run() 的 nodiscard QFuture，并保留原有两线程上限。
    m_pool.start([this, key, cacheFile, archivePath, entryPath,
                  normalized, format, password]() {
        bool success = false;
        bool backendSuccess = false;
        QString errorMessage;

        QDir().mkpath(QFileInfo(cacheFile).absolutePath());
        QTemporaryDir tempDir(
            QDir(cacheRoot()).filePath(QStringLiteral("extract-XXXXXX")));
        if (tempDir.isValid()) {
            std::unique_ptr<ArchiveBackend> backend(
                ArchiveBackendFactory::createForRead(format, archivePath));
            if (backend) {
                QObject::connect(
                    backend.get(), &ArchiveBackend::operationFinished,
                    backend.get(),
                    [&backendSuccess, &errorMessage](bool ok,
                                                     const QString &message) {
                        backendSuccess = ok;
                        if (!ok) {
                            errorMessage = message;
                        }
                    },
                    Qt::DirectConnection);
                QObject::connect(
                    backend.get(), &ArchiveBackend::errorOccurred,
                    backend.get(),
                    [&backendSuccess, &errorMessage](ArchiveErrorCode,
                                                     const QString &message) {
                        backendSuccess = false;
                        errorMessage = message;
                    },
                    Qt::DirectConnection);

                backend->openArchive(archivePath);
                ExtractOptions options;
                options.destDir = tempDir.path();
                options.selectedFiles = {entryPath};
                options.preservePaths = true;
                options.overwrite = OverwritePolicy::Always;
                options.password = password;
                backend->extract(options);
                backend->closeArchive();
            }
        }

        if (tempDir.isValid() && backendSuccess) {
            QString extracted = QDir(tempDir.path()).filePath(normalized);
            if (!QFileInfo::exists(extracted)) {
                const QString baseName =
                    QFileInfo(normalized).fileName();
                QDirIterator it(tempDir.path(), QDir::Files,
                                QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    const QString candidate = it.next();
                    if (QFileInfo(candidate).fileName() == baseName) {
                        extracted = candidate;
                        break;
                    }
                }
            }
            if (QFileInfo::exists(extracted)) {
                success = writeThumbnail(extracted, cacheFile);
            }
        }
        Q_UNUSED(errorMessage);

        QMetaObject::invokeMethod(
            this,
            [this, key, cacheFile, archivePath, entryPath, success]() {
                m_pending.remove(key);
                if (success) {
                    QPixmap pixmap;
                    if (pixmap.load(cacheFile)) {
                        m_icons.insert(key, QIcon(pixmap));
                        emit thumbnailReady(archivePath, entryPath);
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

void ThumbnailManager::requestLocalThumbnail(const QString &key,
                                             const QString &cacheFile,
                                             const QString &filePath)
{
    m_pool.start([this, key, cacheFile, filePath]() {
        QDir().mkpath(QFileInfo(cacheFile).absolutePath());
        const bool success = writeThumbnail(filePath, cacheFile);

        QMetaObject::invokeMethod(
            this,
            [this, key, cacheFile, filePath, success]() {
                m_pending.remove(key);
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
