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

#include <atomic>
#include <mutex>

#include "common/ArchiveConstants.h"

// Archive entries are not real files, so their thumbnails are extracted into a
// persistent cache by a small worker pool.  Local files are read directly and
// share the same cache.  FileListModel receives the result as an icon.
// 压缩包条目不是真实文件；这里用小型线程池把图片解压到持久缓存。本地文件
// 直接读取并复用同一缓存。FileListModel 把结果作为图标使用。
class QTimer;
class ArchiveBackend;

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

    // Drop the in-memory icon/failure cache when the archive or local
    // directory context changes.  The on-disk cache is kept: its keys include
    // the archive/content identity and can be reused later.
    // 归档或本地目录上下文变化时丢弃内存中的图标/失败缓存。磁盘缓存保留：
    // 其键包含压缩包/内容标识，之后仍可复用。
    void clearArchiveCache();

    // Remove "extract-*" directories left behind by crashed runs (startup).
    // 清理崩溃后残留的 "extract-*" 目录（启动时调用）。
    void cleanupStale();

    // Release the in-memory cache and wait for running workers (shutdown).
    // 释放内存缓存并等待运行中的任务（退出时调用）。
    void cleanupOwn();

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

    // Requests collected during one event-loop turn are extracted with a
    // single backend call instead of one process per image.
    // 收集同一事件循环内的请求，用一次后端调用解压，而不是每张图片起一个进程。
    struct ThumbnailRequest {
        QString key;
        QString cacheFile;
        QString archivePath;
        QString entryPath;
        QString normalized;
        ArchiveFormat format = ArchiveFormat::Unknown;
        QString password;
    };
    struct ThumbnailResult {
        QString key;
        QString cacheFile;
        QString archivePath;
        QString entryPath;
        bool success = false;
    };
    void dispatchThumbnailBatch();

    // Upper bound for one backend extraction call.  Large directories are
    // split into several batches so a context switch can cancel sooner and a
    // single huge member list never reaches the backend at once.
    // 单次后端解压的成员上限。大目录会拆成多批，使上下文切换能更早取消，
    // 也避免一次把超大成员列表交给后端。
    static constexpr int kMaxBatchMembers = 64;

    QHash<QString, QIcon> m_icons;
    QSet<QString> m_pending;
    QSet<QString> m_failed;
    QList<ThumbnailRequest> m_batchQueue;
    QTimer *m_batchTimer = nullptr;
    QThreadPool m_pool;
    // Bumped when the archive/local context changes; results from older
    // generations are discarded instead of populating the new context.
    // 归档/本地目录上下文变化时递增；旧 generation 的结果会被丢弃，
    // 不再填充新上下文。
    std::atomic<quint64> m_generation{0};

    // Backend currently extracting a thumbnail batch.  The mutex guards the
    // pointer while clearArchiveCache() requests cancellation, so the worker
    // cannot delete the backend between the load and requestCancel().
    // 当前正在解压缩略图批次的后端。mutex 保护指针，使 clearArchiveCache()
    // 请求取消时工作线程不会在读取与 requestCancel() 之间销毁后端。
    std::mutex m_activeBatchMutex;
    ArchiveBackend *m_activeBatchBackend = nullptr;
};

#endif // THUMBNAILMANAGER_H
