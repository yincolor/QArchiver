#ifndef THUMBNAILMANAGER_H
#define THUMBNAILMANAGER_H

// QArchiver / 小Q解压
// Lazy image thumbnails for archive entries and local files.
// 压缩包条目与本地文件的按需缩略图缓存。

#include <QDateTime>
#include <QHash>
#include <QIcon>
#include <QObject>
#include <QSet>
#include <QThreadPool>

#include "common/ArchiveConstants.h"

// Archive entries are not real files, so their thumbnails are extracted into a
// persistent cache by a small worker pool.  Local files are read directly and
// share the same cache.  FileListModel receives the result as an icon.
// 压缩包条目不是真实文件；这里用小型线程池把图片解压到持久缓存。本地文件
// 直接读取并复用同一缓存。FileListModel 把结果作为图标使用。
class ThumbnailManager : public QObject {
    Q_OBJECT
public:
    static ThumbnailManager &instance();

    // Return a cached thumbnail immediately, or a generic image icon while
    // the extraction request runs in the background.
    // 命中缓存时立即返回缩略图；否则返回通用图片图标并在后台解压。
    QIcon icon(const QString &archivePath, ArchiveFormat format,
               const QString &entryPath, qint64 entrySize,
               const QString &password);

    // Same lazy thumbnail for a file that already exists on disk.  Size and
    // modification time are part of the cache key, so edited images are
    // re-rendered instead of reusing a stale thumbnail.
    // 磁盘上真实文件的按需缩略图。文件大小与修改时间参与缓存键，图片被修改后
    // 会重新生成而不是复用旧缩略图。
    QIcon localIcon(const QString &filePath, qint64 fileSize,
                    const QDateTime &modified);

    // Allow previously failed entries (wrong password, unsupported image) to
    // be retried after the archive/password changes.
    // 在压缩包或密码变化后允许重试此前失败的条目。
    void clearFailures();

signals:
    void thumbnailReady(const QString &archivePath, const QString &entryPath);
    void localThumbnailReady(const QString &filePath);

private:
    explicit ThumbnailManager(QObject *parent = nullptr);

    void requestThumbnail(const QString &key, const QString &cacheFile,
                          const QString &archivePath,
                          const QString &entryPath, qint64 entrySize,
                          ArchiveFormat format, const QString &password);
    void requestLocalThumbnail(const QString &key, const QString &cacheFile,
                               const QString &filePath);
    QHash<QString, QIcon> m_icons;
    QSet<QString> m_pending;
    QSet<QString> m_failed;
    QThreadPool m_pool;
};

#endif // THUMBNAILMANAGER_H
