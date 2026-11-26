#ifndef APPSETTINGS_H
#define APPSETTINGS_H

// QArchiver / 小Q解压
// Persistent application preferences backed by QSettings.
// 基于 QSettings 的全局应用偏好设置。

#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <Qt>

#include <atomic>
#include <memory>

#include "common/ArchiveConstants.h"

class QSettings;
class QTimer;

// Singleton wrapping QSettings for global preferences (design doc 4.6).
// 使用 QSettings 封装全局偏好设置的单例（设计文档 4.6）。
class AppSettings : public QObject {
    Q_OBJECT
public:
    static AppSettings &instance();
    ~AppSettings() override;

    // Getters / 读取接口
    ArchiveFormat defaultCompressFormat() const;
    int defaultCompressionLevel() const;
    OverwritePolicy overwritePolicy() const;
    bool preservePaths() const;
    bool rememberPasswords() const;
    bool openDestAfterFinish() const;
    qint64 maxPreviewSize() const;
    // Number of same-directory images above which continuous preview shows a
    // preparation dialog.  0 means any image batch.  Range 0..9999.
    // 同目录图片数量超过该值时，连续看图显示准备对话框。0 表示任意图片批次。
    // 范围 0..9999。
    int continuousImageDialogThreshold() const;
    // Watch files opened from an archive in the system application and ask
    // whether an external edit should be saved back into the archive.
    // 监视用系统程序打开的压缩包内文件，并询问是否把外部修改写回压缩包。
    bool externalEditWatch() const;
    bool followSymlinks() const;
    QStringList recentFiles() const;
    // Maximum entries kept in the recent-files list. Range 1..100.
    // 最近文件列表保留的最大条目数。范围 1..100。
    int recentFilesLimit() const;
    QString theme() const;
    // UI layout: "modern" (HaoZip-style, default) or "classic" (previous
    // toolbar + recent-files dock layout).
    // 界面布局："modern"（好压风格，默认）或 "classic"（原有的工具栏与最近文件
    // 停靠面板布局）。
    QString uiLayoutMode() const;
    QString contentViewMode() const;   // "grid", "tree" or "list"
    QString toolButtonStyle() const;  // textUnderIcon/textBesideIcon/textOnly/iconOnly
    bool showRecentFiles() const;      // Show the recent-files sidebar

    // Background image shown behind the archive content (design doc 10.5).
    // The image path is empty when no background image is configured.
    // 压缩包内容后方的背景图片（设计文档 10.5）。
    // 未配置背景图片时路径为空。
    // One-click extraction: destination root mode ("archive" = next to the
    // archive, "custom" = oneClickExtractDest) and whether the application
    // closes after a successful one-click extraction.
    // 一键解压：目标根目录模式（"archive" = 压缩包所在目录，
    // "custom" = oneClickExtractDest），以及成功后是否关闭程序。
    QString oneClickExtractDestMode() const;
    QString oneClickExtractDest() const;
    bool oneClickCloseAfterExtract() const;
    bool oneClickCloseAsked() const;

    QString backgroundImagePath() const;
    QString backgroundImageAlignment() const;  // topLeft/.../bottomRight
    QSize backgroundImageSize() const;         // 0 x 0 = natural size
    qreal backgroundImageOpacity() const;      // 0.0 .. 1.0
    // Last directory shown by the local file browser (start page fallback when
    // no archive is open).  Empty means "use the home directory".
    // 本地文件浏览器上次显示的目录（未打开压缩包时的起始位置）。
    // 为空表示使用用户主目录。
    QString localBrowsePath() const;
    // Sort state of the archive/local file list: column index and order.
    // 压缩包/本地文件列表的排序状态：列索引与顺序。
    int sortColumn() const;
    Qt::SortOrder sortOrder() const;
    // Icon size of the modern list/grid views (48..128, default 100).
    // 现代列表/网格视图的图标大小（48..128，默认 96）。
    int itemSize() const;
    // Read archives with the libarchive backend instead of the command line.
    // Cached in an atomic so worker threads (PreviewManager) can read it too.
    // 使用 libarchive 后端而不是命令行读取压缩包；缓存在原子变量中，
    // 以便工作线程（PreviewManager）也能安全读取。
    bool useLibArchiveBackend() const;

    // Setters (persist immediately) / 写入接口（立即持久化）
    void setDefaultCompressFormat(ArchiveFormat fmt);
    void setDefaultCompressionLevel(int level);
    void setOverwritePolicy(OverwritePolicy policy);
    void setPreservePaths(bool preserve);
    void setRememberPasswords(bool remember);
    void setOpenDestAfterFinish(bool open);
    void setMaxPreviewSize(qint64 size);
    void setContinuousImageDialogThreshold(int count);
    void setExternalEditWatch(bool watch);
    void setFollowSymlinks(bool follow);
    void setRecentFiles(const QStringList &files);
    void setRecentFilesLimit(int limit);
    void setTheme(const QString &theme);
    void setUiLayoutMode(const QString &mode);
    void setContentViewMode(const QString &mode);
    void setToolButtonStyle(const QString &style);
    void setShowRecentFiles(bool show);
    void setUseLibArchiveBackend(bool use);
    void setOneClickExtractDestMode(const QString &mode);
    void setOneClickExtractDest(const QString &path);
    void setOneClickCloseAfterExtract(bool closeAfter);
    void setOneClickCloseAsked(bool asked);
    void setBackgroundImagePath(const QString &path);
    void setBackgroundImageAlignment(const QString &alignment);
    void setBackgroundImageSize(const QSize &size);
    void setBackgroundImageOpacity(qreal opacity);
    void setLocalBrowsePath(const QString &path);
    void setSort(int column, Qt::SortOrder order);
    void setItemSize(int size);
    // Persist all background image options with one sync and one change signal.
    // 一次性持久化全部背景图片选项，只 sync 并发出一次变更信号。
    void setBackgroundImageOptions(const QString &path, const QString &alignment,
                                   const QSize &size, qreal opacity);

    // Group many setters into a single disk sync (e.g. the preferences
    // dialog).  Getters still return the new values immediately because
    // QSettings keeps them in memory.  endBatch() flushes once, so callers
    // should pair both calls (RAII guard recommended).
    // 把多次写入合并为一次落盘（例如偏好设置对话框）。读取接口仍立即返回新值，
    // 因为 QSettings 会在内存中保留；endBatch() 只落盘一次，建议用 RAII 配对。
    void beginBatch();
    void endBatch();

    // Write debounced values (currently the last-visited local directory) and
    // sync immediately.  Called by main() before the application exits so a
    // navigation in the last 500 ms is not lost.
    // 立即写入防抖中的值（当前是上次浏览的本地目录）并落盘。由 main() 在退出
    // 前调用，避免最后 500 ms 内的导航丢失。
    void flushPendingWrites();

signals:
    // Emitted whenever one of the background image options changes.
    // 任一背景图片选项变化时发出。
    void backgroundImageChanged();
    // Emitted after the recent-files limit was persisted and the stored list
    // was trimmed.  RecentFilesManager refreshes the sidebar in response.
    // 最近文件上限持久化并裁剪列表后发出；RecentFilesManager 据此刷新侧边栏。
    void recentFilesLimitChanged();

private:
    AppSettings();
    Q_DISABLE_COPY(AppSettings)

    // Read or write one settings key. / 读取或写入单个设置键。
    QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;
    void setValue(const QString &key, const QVariant &value);

    void syncIfNotBatching();
    void flushLocalBrowsePath();

    std::unique_ptr<QSettings> m_settings;
    std::atomic_bool m_useLibArchive{true};
    int m_batchDepth = 0;
    // Debounces the last-visited local directory: rapid navigation updates the
    // in-memory value (returned by the getter) but writes the file only once
    // the user stops moving for a moment.
    // 对上次浏览的本地目录做防抖：连续导航只更新内存值（getter 立即可见），
    // 停止操作片刻后才写一次设置文件。
    QTimer *m_localBrowseSyncTimer = nullptr;
    bool m_hasPendingLocalBrowsePath = false;
    QString m_pendingLocalBrowsePath;
};

#endif // APPSETTINGS_H
