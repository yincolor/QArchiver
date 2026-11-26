#ifndef DRAGOUTMANAGER_H
#define DRAGOUTMANAGER_H

// QArchiver / 小Q解压
// Stages selected entries for native drag-and-drop out of QArchiver.
// 为从 QArchiver 拖出原生拖放准备所选条目。

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

#include "common/ArchiveConstants.h"
#include "common/ArchiveEntry.h"
#include "common/ArchiveError.h"

class ArchiveBackend;

// Result of a "prepare files for dragging" request.
// “准备拖拽文件”请求的结果。
struct DragOutPayload {
    QString stagingRoot;      // Per-drag staging directory (kept until exit)
    QStringList localPaths;   // Real local paths handed to QMimeData
    QString errorMessage;
    ArchiveErrorCode errorCode = ArchiveErrorCode::Unknown;
    bool success = false;
    bool cancelled = false;
};

// Prepares real local files for a drag-out gesture (design doc "drag out").
//
// Archive entries do not exist on disk, but the window manager / file manager
// only understands "text/uri-list" with real file URLs.  This class extracts
// the selected entries into a per-drag staging directory and maps the archive
// paths back to the staged files.  Staging is kept until the application
// exits because the drop target may still be copying asynchronously.
//
// 为拖出操作准备真实本地文件（设计文档“拖出”）。
// 压缩包条目在磁盘上并不存在，而窗口管理器/文件管理器只识别带真实文件 URL 的
// "text/uri-list"。本类将选中条目解压到每次拖拽独立的暂存目录，
// 并把压缩包路径映射到暂存文件。由于放置目标可能仍在异步复制，
// 暂存内容会保留到应用退出。
class DragOutManager : public QObject {
    Q_OBJECT
public:
    static DragOutManager &instance();

    // Synchronous: runs the extraction in a worker thread and spins a nested
    // event loop so status bar updates stay responsive.  The caller must start
    // the QDrag immediately afterwards, while the mouse button is still down.
    // Successful preparations are cached, so a second drag of the same
    // selection (and password) starts without any delay.
    // 同步调用：在工作线程中解压，并运行嵌套事件循环以保持状态栏响应。
    // 调用方必须在鼠标按键仍按下时立即启动 QDrag。
    // 成功的准备结果会缓存，因此同一选择（及密码）的第二次拖拽无需等待。
    DragOutPayload extractForDrag(const QString &archivePath,
                                  ArchiveFormat format,
                                  const QList<ArchiveEntry> &entries,
                                  const QStringList &selectedPaths,
                                  const QString &password,
                                  const QString &encodingHint = QString());

    // Cancel the preparation in progress (Esc while dragging).
    // 取消正在进行的准备（拖拽时按 Esc）。
    void cancel();

    // Keep the staging of a finished drag until the application exits.
    // 已完成拖拽的暂存内容保留到应用退出。
    void retire(const DragOutPayload &payload);

    // A previously prepared staging for this archive/selection/password.  The
    // drag gesture cannot wait for a slow extraction without breaking the
    // mouse press, so a finished preparation is reused on the next attempt.
    // 查询此前为相同压缩包/选择/密码准备的暂存内容。
    // 拖拽手势不能等待慢速解压而不破坏鼠标按压状态，
    // 因此下一次尝试会复用已完成的准备结果。
    bool hasPreparedFor(const QString &archivePath, ArchiveFormat format,
                        const QStringList &paths,
                        const QString &password) const;
    DragOutPayload takePreparedFor(const QString &archivePath,
                                   ArchiveFormat format,
                                   const QStringList &paths,
                                   const QString &password);

    // Forget the in-memory index for the closed/replaced archive.  The staged
    // directories on disk are intentionally kept: a drag may already be in
    // flight and the system target reads the files after the drop.  They are
    // removed by cleanupStale()/cleanupOwn().
    // 丢弃已关闭/被替换压缩包的内存索引。磁盘上的暂存目录有意保留：拖拽可能
    // 仍在进行，系统目标会在放下后读取这些文件；它们由
    // cleanupStale()/cleanupOwn() 清理。
    void clearCache();

    // Remove staging left behind by processes that no longer exist.
    // 清理已不存在进程遗留的暂存内容。
    void cleanupStale();

    // Remove this process' staging (called on normal shutdown).
    // 清理本进程的暂存内容（正常退出时调用）。
    void cleanupOwn();

    // Helpers, exposed as public statics so they can be unit tested.
    // 辅助函数，作为 public static 暴露以便单元测试。
    static QStringList pruneNestedPaths(const QStringList &paths);
    static QStringList mapStagedPaths(const QString &stagingRoot,
                                      const QStringList &selectedPaths);
    static QString findStagedPathFallback(const QString &stagingRoot,
                                          const QString &normalized);
    // Top-level files of a staging directory; used for plain stream formats
    // (.gz/.bz2/.xz), where the backend ignores selectedFiles.
    // 暂存目录的顶层文件；用于后端忽略 selectedFiles 的纯流格式
    // （.gz/.bz2/.xz）。
    static QStringList topLevelStagedPaths(const QString &stagingRoot);
    // Returns -1 when an entry has a negative size or the sum overflows.
    // 条目大小为负或总和溢出时返回 -1。
    static qint64 selectedUncompressedSize(const QList<ArchiveEntry> &entries,
                                           const QStringList &paths);
    // Only errors that the command line tools can plausibly fix are retried.
    // 仅重试命令行工具可能修复的错误。
    static bool isRetryableWithCli(ArchiveErrorCode code);

signals:
    void progressChanged(int percent, int total, int current, int etaSec);
    void stageMessage(const QString &message);

private:
    DragOutManager();
    Q_DISABLE_COPY(DragOutManager)

    struct ExtractionContext {
        std::atomic_bool cancelRequested{false};
        std::atomic<ArchiveBackend *> backend{nullptr};
    };

    QString dragRoot() const;
    QString createStagingRoot() const;

    static QString cacheKey(const QString &archivePath, ArchiveFormat format,
                            const QStringList &paths, const QString &password);
    void storePrepared(const QString &key, const DragOutPayload &payload);

    std::shared_ptr<ExtractionContext> m_context;
    QSet<QString> m_retainedRoots;

    struct CacheEntry {
        QString stagingRoot;
        QStringList localPaths;
    };
    QHash<QString, CacheEntry> m_cache;  // most recent last; size is capped
};

#endif // DRAGOUTMANAGER_H
