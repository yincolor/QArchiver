#ifndef PREVIEWMANAGER_H
#define PREVIEWMANAGER_H

// QArchiver / 小Q解压
// Asynchronous preview extraction and text/image loading.
// 异步预览解压以及文本/图片加载。

#include <QHash>
#include <QObject>
#include <QString>

#include "common/ArchiveConstants.h"

class ProgressDialog;
struct PreviewJob;

// Manages temporary extraction for file preview (design doc 5.6).
// Preview extraction never touches the main backend: it creates a private
// backend inside a worker thread, extracts the clicked entry (and image
// siblings for continuous viewing), then destroys it.
// 管理文件预览的临时解压（设计文档 5.6）。
// 预览解压不触碰主后端：在工作线程内创建私有后端，解压点击的条目
// （连续看图时连同同目录图片），完成后销毁。
class PreviewManager : public QObject {
    Q_OBJECT
public:
    static PreviewManager &instance();

    // @p imagePaths contains the clicked image and its same-directory image
    // siblings.  They are extracted together so a system image viewer that
    // browses the containing folder can switch between them.
    // @p imagePaths 包含点击的图片及其同目录图片；会一起解压，
    // 便于支持目录浏览的系统图片查看器连续切换。
    void preview(const QString &archivePath, ArchiveFormat format,
                 const QString &entryPath, qint64 entrySize,
                 const QStringList &imagePaths, const QString &password,
                 bool openWith = false);

    // Cleanup stale preview directories (called at startup)
    // 清理陈旧预览目录（启动时调用）
    void cleanupStale();

    // Cleanup this process's preview directory (called at exit)
    // 清理本进程的预览目录（退出时调用）
    void cleanupOwn();

    // Remove the cached previews of one archive (archive closed/replaced) and
    // cancel a running extraction batch for it.
    // 删除某个压缩包的预览缓存（关闭/替换压缩包时），并取消其正在进行的
    // 解压批次。
    void clearArchiveCache(const QString &archivePath);

private:
    PreviewManager();
    Q_DISABLE_COPY(PreviewManager)

    QString previewRoot() const;
    QString ownPreviewDir() const;
    QString archivePreviewDir(const QString &archivePath) const;
    void finishPreview(const QString &previewDir, const QString &destPath,
                       bool success, bool cancelled,
                       const QString &errorMessage);
    // Remember an opened file so an external edit can be written back into the
    // archive (see ExternalEditManager).
    // 记录被打开的文件，使外部修改可以写回压缩包（见 ExternalEditManager）。
    void trackExternalEdit(const QString &previewDir, const QString &targetPath,
                           const QString &archivePath, ArchiveFormat format,
                           const QString &password);
    void cancelPreview(const QString &previewDir);
    void updatePreviewProgress(const QString &previewDir, int percent,
                               int total, int current, int etaSec);

    QHash<QString, PreviewJob *> m_jobs;

    // The most recent target clicked while a directory batch is still running.
    // 目录批次仍在运行时最近点击的目标。
    QHash<QString, QString> m_pendingTargets;
};

#endif // PREVIEWMANAGER_H
